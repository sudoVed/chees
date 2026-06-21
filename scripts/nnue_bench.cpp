// nnue_bench.cpp — verify + benchmark the NNUE efficiency tricks against the
// shipped NNU2/NNU3 (pre-quantized) net. Loads via load_q (no float needed).
//   (1) incremental accumulator EXACTLY equals a from-scratch refresh.
//   (2) timing: incremental update vs full refresh (the speedup the trick buys).
//   (3) full quantized leaf-eval throughput (evals/sec).
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/movegen/movegen.h"
#include "../engine/nn/nnue.h"

using clk = std::chrono::high_resolution_clock;

static bool acc_equal(const NN::Accumulator& a, const NN::Accumulator& b) {
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < NN::ACC; ++i)
            if (a.acc[c][i] != b.acc[c][i]) return false;
    return true;
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "frontend/models/net.nnue";
    Zobrist::init();
    Attacks::init();

    FILE* f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* blob = (unsigned char*)malloc(len);
    if (fread(blob, 1, len, f) != (size_t)len) { printf("read fail\n"); return 1; }
    fclose(f);

    NN::Network net;
    bool ok = (blob[3]=='2'||blob[3]=='3') ? net.load_q(blob, len) : (net.load(blob,len) && (net.quantize(), true));
    if (!ok) { printf("load failed (header %c%c%c%c)\n", blob[0],blob[1],blob[2],blob[3]); return 1; }
    printf("loaded %s  (%ld bytes, format NNU%c)\n", path, len, blob[3]);

    // (1) incremental == refresh over a played sequence
    Position pos; pos.set_startpos();
    NN::Accumulator cur; net.refresh(cur, pos);
    int plies=0, mism=0, kingMoves=0, captures=0;
    for (int step = 0; step < 120; ++step) {
        MoveList legal; MoveGen::generate_legal(pos, legal);
        if (legal.count == 0) break;
        Move m = legal.moves[(step*7+3) % legal.count];
        if (pos.board[move_from(m)] == KING) kingMoves++;
        if (is_capture(m)) captures++;
        pos.make(m);
        NN::Accumulator inc;   net.update(inc, cur, pos);
        NN::Accumulator fresh; net.refresh(fresh, pos);
        if (!acc_equal(inc,fresh) || net.eval_acc(inc,pos.side)!=net.eval_acc(fresh,pos.side)) {
            mism++; printf("  PLY %d MISMATCH\n", step);
        }
        cur = inc; plies++;
    }
    printf("(1) incremental==refresh : plies=%d kingMoves=%d captures=%d mismatches=%d -> %s\n",
           plies, kingMoves, captures, mism, mism==0 ? "PASS":"FAIL");

    // (2) timing: refresh vs incremental update on a typical quiet move
    // Build two adjacent positions; time each path in a tight loop.
    pos.set_startpos();
    MoveList l0; MoveGen::generate_legal(pos, l0);
    Position after = pos; after.make(l0.moves[0]);       // 1 quiet pawn move
    NN::Accumulator base; net.refresh(base, pos);
    const int ITER = 200000;
    volatile int sink = 0;
    auto t0 = clk::now();
    for (int i=0;i<ITER;i++){ NN::Accumulator a; net.refresh(a, after); sink ^= a.acc[0][0]; }
    auto t1 = clk::now();
    for (int i=0;i<ITER;i++){ NN::Accumulator a; net.update(a, base, after); sink ^= a.acc[0][0]; }
    auto t2 = clk::now();
    double refMs = std::chrono::duration<double,std::milli>(t1-t0).count();
    double incMs = std::chrono::duration<double,std::milli>(t2-t1).count();
    printf("(2) accumulator update  : refresh %.1f ns/op   incremental %.1f ns/op   speedup %.2fx (scalar; wasm SIMD adds more)\n",
           refMs*1e6/ITER, incMs*1e6/ITER, refMs/incMs);

    // (3) leaf-eval throughput (the dense layers — NOT SIMD-accelerated)
    NN::Accumulator a; net.refresh(a, pos);
    auto t3 = clk::now();
    for (int i=0;i<ITER;i++){ sink ^= net.eval_acc(a, pos.side); }
    auto t4 = clk::now();
    double evMs = std::chrono::duration<double,std::milli>(t4-t3).count();
    printf("(3) quantized leaf eval : %.1f ns/op  (%.2f M evals/sec)  [dense L1/L2/out, scalar int]\n",
           evMs*1e6/ITER, ITER/(evMs/1000.0)/1e6);

    printf("sink=%d\n", sink);
    free(blob);
    return mism==0 ? 0 : 1;
}
