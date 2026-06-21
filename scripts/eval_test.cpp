// eval_test.cpp — native harness to verify the hand-crafted eval (Phase 8).
// Compiles the engine sources natively (no wasm) and prints the per-term
// breakdown for a set of FENs. Run via scripts/run_eval_test.sh.
#include <cstdio>
#include <cstring>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/eval/hce.h"

static void show(const char* label, const char* fen) {
    Position pos;
    if (!pos.set_fen(fen, (int)strlen(fen))) { printf("BAD FEN: %s\n", fen); return; }
    int terms[Eval::TERM_NB], phase = 0, scale = 64;
    int total = Eval::evaluate_breakdown(pos, terms, &phase, &scale);
    int stm   = Eval::evaluate(pos);
    printf("\n== %s\n   %s\n", label, fen);
    printf("   phase=%d/24  scale=%d/64  total(W-B)=%+d cp  stm=%+d cp\n",
           phase, scale, total, stm);
    for (int i = 0; i < Eval::TERM_NB; ++i)
        if (terms[i] != 0)
            printf("     %-20s %+5d\n", Eval::TERM_NAMES[i], terms[i]);
}

int main() {
    Zobrist::init();
    Attacks::init();

    show("Start position (expect ~0)",
         "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    show("White up a queen (expect large +)",
         "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    show("Black up a rook (expect large -)",
         "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/1NBQKBNR w KQkq - 0 1");
    show("White: knight on rim a3 vs centralized (mirror, expect small -)",
         "rnbqkbnr/pppppppp/8/8/8/N7/PPPPPPPP/R1BQKBNR b KQkq - 0 1");
    show("Open e-file rook for White (expect rook activity +)",
         "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 1");
    show("White passed pawn on e6 (expect passed-pawn +)",
         "8/8/4P3/8/8/8/k7/K7 w - - 0 1");
    show("King exposed, open files (expect king-safety swing)",
         "r3k2r/ppp2ppp/8/8/8/8/PPP2PPP/R3K2R w KQkq - 0 1");
    show("Endgame: centralized White king (expect king-activity +)",
         "8/8/8/4K3/8/8/8/4k3 w - - 0 1");
    show("Bishop pair for White (expect bishop-pair +)",
         "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/2B1KB2 w kq - 0 1");

    // Symmetry sanity: a mirrored position must evaluate to the exact negation.
    {
        Position a, b;
        const char* f1 = "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 0 1";
        const char* f2 = "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R b KQkq - 0 1";
        a.set_fen(f1, (int)strlen(f1));
        b.set_fen(f2, (int)strlen(f2));
        int ea = Eval::evaluate_white(a), eb = Eval::evaluate_white(b);
        printf("\n== Symmetry check: identical material/structure\n");
        printf("   W-to-move W-B=%+d   B-to-move W-B=%+d  (diff only tempo)\n", ea, eb);
    }
    return 0;
}
