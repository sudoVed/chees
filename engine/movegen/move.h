// move.h — 16-bit move encoding: from(6) | to(6) | flags(4).
// Flag layout follows the common "promotion bit / capture bit" scheme.
#pragma once
#include <stdint.h>
#include "../board/bitboard.h"

using Move = uint16_t;

enum MoveFlag : int {
    FLAG_QUIET        = 0,
    FLAG_DOUBLE_PUSH  = 1,
    FLAG_CASTLE_KING  = 2,
    FLAG_CASTLE_QUEEN = 3,
    FLAG_CAPTURE      = 4,
    FLAG_EP_CAPTURE   = 5,
    FLAG_PROMO_N      = 8,   // bit3 set => promotion
    FLAG_PROMO_B      = 9,
    FLAG_PROMO_R      = 10,
    FLAG_PROMO_Q      = 11,
    FLAG_PROMO_N_CAP  = 12,  // promotion + capture
    FLAG_PROMO_B_CAP  = 13,
    FLAG_PROMO_R_CAP  = 14,
    FLAG_PROMO_Q_CAP  = 15
};

inline Move make_move(int from, int to, int flag) {
    return Move((flag << 12) | (to << 6) | from);
}
inline int move_from(Move m) { return m & 0x3F; }
inline int move_to(Move m)   { return (m >> 6) & 0x3F; }
inline int move_flag(Move m) { return (m >> 12) & 0xF; }

inline bool is_promotion(Move m) { return move_flag(m) & 0x8; }
inline bool is_capture(Move m) {
    int f = move_flag(m);
    return f == FLAG_CAPTURE || f == FLAG_EP_CAPTURE || f >= FLAG_PROMO_N_CAP;
}
inline bool is_ep(Move m)     { return move_flag(m) == FLAG_EP_CAPTURE; }
inline bool is_castle(Move m) {
    int f = move_flag(m);
    return f == FLAG_CASTLE_KING || f == FLAG_CASTLE_QUEEN;
}

// PieceType produced by a promotion flag (KNIGHT..QUEEN)
inline PieceType promo_type(Move m) {
    switch (move_flag(m)) {
        case FLAG_PROMO_N: case FLAG_PROMO_N_CAP: return KNIGHT;
        case FLAG_PROMO_B: case FLAG_PROMO_B_CAP: return BISHOP;
        case FLAG_PROMO_R: case FLAG_PROMO_R_CAP: return ROOK;
        default:                                  return QUEEN;
    }
}

constexpr int MAX_MOVES = 256;

struct MoveList {
    Move moves[MAX_MOVES];
    int  count = 0;
    inline void add(Move m) { moves[count++] = m; }
};
