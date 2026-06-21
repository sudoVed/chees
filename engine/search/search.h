// search.h - alpha-beta search with iterative deepening, transposition table,
// move ordering and quiescence. Returns the best move for the side to move.
#pragma once
#include "../board/position.h"
#include "../movegen/move.h"
#include "../eval/evaluator.h"

namespace Search {

struct Result {
    Move best;    // best move found
    int  score;   // score (centipawns, side-to-move POV; mate scores near +/-30000)
    int  depth;   // depth reached
    long nodes;   // nodes searched
};

// Swap the evaluation function (defaults to the hand-crafted eval; point this
// at the NNUE evaluator once a net is loaded). Pointer must outlive the search.
void set_evaluator(Evaluator* e);

// Reset TT + heuristics for a fresh game.
void new_game();

// Iterative deepening to `maxDepth`. Mutates `pos` during search but restores it.
Result think(Position& pos, int maxDepth, long maxNodes = 0);

} // namespace Search
