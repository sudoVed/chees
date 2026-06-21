#include "gamestate.h"
#include "../movegen/movegen.h"

namespace Rules {

bool insufficient_material(const Position& pos) {
    // Any pawn, rook, or queen => mate is possible.
    if (pos.byType[PAWN] | pos.byType[ROOK] | pos.byType[QUEEN]) return false;

    int wN = popcount(pos.pieces(WHITE, KNIGHT));
    int bN = popcount(pos.pieces(BLACK, KNIGHT));
    int wB = popcount(pos.pieces(WHITE, BISHOP));
    int bB = popcount(pos.pieces(BLACK, BISHOP));
    int minors = wN + bN + wB + bB;

    if (minors == 0) return true;                 // K vs K
    if (minors == 1) return true;                 // K+minor vs K
    if (minors == 2 && wN + bN == 0) {            // bishops only
        // K+B vs K+B is drawn only if same-colored bishops; otherwise mate
        // is technically possible, so treat opposite-color as sufficient.
        // Two bishops on one side (wB==2 or bB==2) is a win.
        if (wB == 1 && bB == 1) {
            U64 b = pos.byType[BISHOP];
            const U64 dark = 0xAA55AA55AA55AA55ULL;
            bool a = b & dark, c = b & ~dark;
            return !(a && c);                     // same color => draw
        }
    }
    return false;
}

Status status(Position& pos) {
    bool inCheck = pos.in_check(pos.side);

    MoveList legal;
    MoveGen::generate_legal(pos, legal);

    if (legal.count == 0)
        return inCheck ? STATUS_CHECKMATE : STATUS_STALEMATE;

    // draws (only when the game isn't already decided by mate)
    if (pos.halfmoveClock >= 100) return STATUS_DRAW_50;
    if (pos.is_repetition())      return STATUS_DRAW_REPETITION;
    if (insufficient_material(pos)) return STATUS_DRAW_MATERIAL;

    return inCheck ? STATUS_CHECK : STATUS_ONGOING;
}

} // namespace Rules
