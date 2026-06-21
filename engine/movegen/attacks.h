// attacks.h — precomputed leaper tables + on-the-fly sliding attacks.
#pragma once
#include "../board/bitboard.h"

namespace Attacks {
    extern U64 knight[SQUARE_NB];
    extern U64 king[SQUARE_NB];
    extern U64 pawn[COLOR_NB][SQUARE_NB];  // squares a pawn of <color> attacks

    void init();   // build leaper tables; call once at startup

    // Sliding attacks computed against a blocker set `occ` (ray-walk).
    U64 bishop(int sq, U64 occ);
    U64 rook(int sq, U64 occ);
    inline U64 queen(int sq, U64 occ) { return bishop(sq, occ) | rook(sq, occ); }
}
