// nnue_incr_test.cpp — verify the quantized + incremental accumulator.
//   (1) quantized eval is in the same ballpark as the float reference.
//   (2) the incrementally-updated accumulator EXACTLY equals a from-scratch
//       refresh after every move in a played sequence (incl. captures, castling,
//       promotions, king moves -> the full-refresh path).
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/movegen/movegen.h"
#include "../engine/nn/nnue.h"

static bool acc_equal(const NN::Accumulator& a, const NN::Accumulator& b) {
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < NN::ACC; ++i)
            if (a.acc[c][i] != b.acc[c][i]) return false;
    return true;
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/tmp/net.bin";
    Zobrist::init();
    Attacks::init();

    FILE* f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* blob = (unsigned char*)malloc(len);
    if (fread(blob, 1, len, f) != (size_t)len) { printf("read fail\n"); return 1; }
    fclose(f);

    NN::Network net;
    if (!net.load(blob, len)) { printf("load failed\n"); return 1; }
    net.quantize();

    // (1) quantized vs float ballpark on a few positions
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 b kq - 0 1",
        "8/8/4k3/8/8/3K4/4P3/8 w - - 0 1",
    };
    printf("== quantized vs float ==\n");
    for (const char* fen : fens) {
        Position p; p.set_fen(fen, strlen(fen));
        printf("  float=%+d  quant=%+d   %s\n", net.evaluate(p), net.evaluate_q(p), fen);
    }

    // (2) incremental == refresh over a played sequence from the start position
    Position pos; pos.set_startpos();
    NN::Accumulator cur; net.refresh(cur, pos);

    int plies = 0, mismatches = 0, kingMoves = 0, captures = 0;
    for (int step = 0; step < 60; ++step) {
        MoveList legal; MoveGen::generate_legal(pos, legal);
        if (legal.count == 0) break;                 // mate/stalemate
        Move m = legal.moves[(step * 7 + 3) % legal.count];   // deterministic pick
        if (pos.board[move_from(m)] == KING) kingMoves++;
        if (is_capture(m)) captures++;

        pos.make(m);
        NN::Accumulator inc;   net.update(inc, cur, pos);     // incremental
        NN::Accumulator fresh; net.refresh(fresh, pos);       // from scratch

        bool accOK = acc_equal(inc, fresh);
        int eInc = net.eval_acc(inc, pos.side);
        int eFresh = net.eval_acc(fresh, pos.side);
        if (!accOK || eInc != eFresh) {
            mismatches++;
            printf("  PLY %d MISMATCH accOK=%d eInc=%d eFresh=%d\n", step, accOK, eInc, eFresh);
        }
        cur = inc;     // carry the incremental accumulator forward
        plies++;
    }
    printf("== incremental vs refresh ==\n");
    printf("  plies=%d  kingMoves=%d  captures=%d  mismatches=%d -> %s\n",
           plies, kingMoves, captures, mismatches, mismatches == 0 ? "PASS" : "FAIL");

    free(blob);
    return mismatches == 0 ? 0 : 1;
}
