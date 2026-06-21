// position.h — complete board state + make()/unmake() with full restore.
#pragma once
#include <stdint.h>
#include "bitboard.h"
#include "../movegen/move.h"

// Castling-rights bit flags.
enum CastleRight : int {
    WHITE_OO  = 1, WHITE_OOO = 2,
    BLACK_OO  = 4, BLACK_OOO = 8
};

// Per-move state needed to undo (everything make() can't trivially reverse).
struct Undo {
    int  captured;     // PieceType captured, or NO_PIECE_TYPE
    int  castling;     // rights before the move
    int  epSquare;     // ep square before the move (-1 = none)
    int  halfmove;     // halfmove clock before the move
    U64  key;          // zobrist key before the move
};

constexpr int MAX_PLY = 1024;

struct Position {
    U64 byType[PIECE_TYPE_NB];   // occupancy per piece type (both colors)
    U64 byColor[COLOR_NB];       // occupancy per color
    int board[SQUARE_NB];        // piece type on each square, or NO_PIECE_TYPE
    Color side;                  // side to move
    int castling;                // CastleRight bitmask
    int epSquare;                // en-passant target square, or -1
    int halfmoveClock;           // for the 50-move rule
    int fullmoveNumber;
    U64 key;                     // current zobrist key

    // Undo stack + repetition history (zobrist keys of every position reached).
    Undo undoStack[MAX_PLY];
    int  ply;                    // number of moves made (top of undo stack)
    U64  history[MAX_PLY];       // history[i] = key after i half-moves
    int  histLen;

    // ---- queries ----
    inline U64 occupied() const { return byColor[WHITE] | byColor[BLACK]; }
    inline U64 pieces(Color c, PieceType pt) const { return byType[pt] & byColor[c]; }
    inline int piece_on(int sq) const { return board[sq]; }
    inline Color color_on(int sq) const {
        return (byColor[WHITE] & sq_bb(sq)) ? WHITE : BLACK;
    }
    inline int king_square(Color c) const { return lsb(byType[KING] & byColor[c]); }

    // ---- setup ----
    void clear();
    void set_startpos();
    bool set_fen(const char* fen, int len);

    // ---- core ----
    void make(Move m);
    void unmake(Move m);

    // Is square `sq` attacked by any piece of color `by`?
    bool attacked_by(int sq, Color by) const;
    bool in_check(Color c) const;

    // threefold repetition (current position appeared >= 3 times)
    bool is_repetition() const;

private:
    void put_piece(Color c, PieceType pt, int sq);
    void remove_piece(int sq);
    void move_piece(int from, int to);
};
