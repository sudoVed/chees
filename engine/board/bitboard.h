// bitboard.h — 64-bit board type, square/piece enums, bit helpers.
// Single source of truth for the low-level board representation.
#pragma once
#include <stdint.h>

using U64 = uint64_t;

// ---- Colors -------------------------------------------------------------
enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
inline Color operator~(Color c) { return Color(c ^ 1); }

// ---- Piece types --------------------------------------------------------
enum PieceType : int {
    PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5,
    PIECE_TYPE_NB = 6, NO_PIECE_TYPE = 6
};

// ---- Squares (a1 = 0 ... h8 = 63), file-major within a rank -------------
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    SQ_NONE = 64, SQUARE_NB = 64
};

inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }
inline int make_square(int file, int rank) { return rank * 8 + file; }

// ---- Bit helpers --------------------------------------------------------
inline U64 sq_bb(int sq) { return U64(1) << sq; }

inline int popcount(U64 b) { return __builtin_popcountll(b); }

// index of least-significant set bit (assumes b != 0)
inline int lsb(U64 b) { return __builtin_ctzll(b); }
inline int msb(U64 b) { return 63 - __builtin_clzll(b); }

// pop & return the lsb index
inline int pop_lsb(U64& b) {
    int s = lsb(b);
    b &= b - 1;
    return s;
}

inline bool more_than_one(U64 b) { return b & (b - 1); }

// ---- File / rank masks --------------------------------------------------
constexpr U64 FILE_A_BB = 0x0101010101010101ULL;
constexpr U64 FILE_B_BB = FILE_A_BB << 1;
constexpr U64 FILE_G_BB = FILE_A_BB << 6;
constexpr U64 FILE_H_BB = FILE_A_BB << 7;
constexpr U64 RANK_1_BB = 0xFFULL;
constexpr U64 RANK_2_BB = RANK_1_BB << 8;
constexpr U64 RANK_4_BB = RANK_1_BB << (8 * 3);
constexpr U64 RANK_5_BB = RANK_1_BB << (8 * 4);
constexpr U64 RANK_7_BB = RANK_1_BB << (8 * 6);
constexpr U64 RANK_8_BB = RANK_1_BB << (8 * 7);

// ---- Directional shifts (board-safe; mask off wrap-around) --------------
enum Dir : int { NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
                 NE = 9, NW = 7, SE = -7, SW = -9 };

inline U64 shift_north(U64 b) { return b << 8; }
inline U64 shift_south(U64 b) { return b >> 8; }
inline U64 shift_east (U64 b) { return (b & ~FILE_H_BB) << 1; }
inline U64 shift_west (U64 b) { return (b & ~FILE_A_BB) >> 1; }
inline U64 shift_ne(U64 b) { return (b & ~FILE_H_BB) << 9; }
inline U64 shift_nw(U64 b) { return (b & ~FILE_A_BB) << 7; }
inline U64 shift_se(U64 b) { return (b & ~FILE_H_BB) >> 7; }
inline U64 shift_sw(U64 b) { return (b & ~FILE_A_BB) >> 9; }
