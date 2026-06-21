// gamestate.h — check / checkmate / stalemate / draws.
#pragma once
#include "../board/position.h"

enum Status : int {
    STATUS_ONGOING        = 0,
    STATUS_CHECK          = 1,   // side to move is in check (game continues)
    STATUS_CHECKMATE      = 2,   // side to move is mated (loses)
    STATUS_STALEMATE      = 3,
    STATUS_DRAW_50        = 4,
    STATUS_DRAW_REPETITION= 5,
    STATUS_DRAW_MATERIAL  = 6
};

namespace Rules {
    bool insufficient_material(const Position& pos);
    // Computes the full status of the position for the side to move.
    Status status(Position& pos);
}
