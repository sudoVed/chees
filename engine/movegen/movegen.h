// movegen.h — pseudo-legal generation + the single legality filter.
#pragma once
#include "move.h"
#include "../board/position.h"

namespace MoveGen {
    // All pseudo-legal moves (may leave own king in check).
    void generate_pseudo(const Position& pos, MoveList& list);

    // The ONE definition of "legal" used everywhere (UI highlights, human
    // moves, AI search): pseudo-legal moves that do NOT leave the own king
    // in check. Castling through/into check is filtered at generation time.
    void generate_legal(Position& pos, MoveList& list);
}
