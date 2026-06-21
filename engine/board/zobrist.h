// zobrist.h — hash keys for repetition detection (and future TT).
#pragma once
#include <stdint.h>
#include "bitboard.h"

namespace Zobrist {
    extern U64 psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];
    extern U64 castling[16];   // indexed by castling-rights bitmask
    extern U64 epFile[8];      // indexed by file of ep square
    extern U64 side;           // XORed when it is black to move

    void init();               // fill all keys deterministically
}
