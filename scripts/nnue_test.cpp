// nnue_test.cpp — load the shared weight blob (training/ref_gen.py output) and
// run the C++ HalfKP inference on the same FEN list, printing raw output + cp.
// Compared against the numpy reference by scripts/run_nnue_parity.sh.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/nn/nnue.h"

static const char* FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 b kq - 0 1",
    "r2q1rk1/pp1nbppp/2p1pn2/3p4/2PP1B2/2N1PN2/PP3PPP/R2Q1RK1 w - - 0 1",
    "8/8/4k3/8/8/3K4/4P3/8 w - - 0 1",
    "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
};

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
    if (!net.load(blob, len)) { printf("net.load failed (format/size)\n"); return 1; }

    for (const char* fen : FENS) {
        Position p;
        if (!p.set_fen(fen, strlen(fen))) { printf("BAD FEN %s\n", fen); continue; }
        float raw = net.evaluate_raw(p);
        int cp = net.evaluate(p);
        printf("%+.5f\t%d\t%s\n", raw, cp, fen);
    }
    free(blob);
    return 0;
}
