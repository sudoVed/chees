// tt.h - transposition table: cache of already-searched positions, keyed by the
// Zobrist hash. Lets the search reuse work when the same position is reached by
// a different move order (a "transposition"), and supplies a best-move hint for
// move ordering.
#pragma once
#include <stdint.h>
#include "../movegen/move.h"

namespace TT {

enum Bound : uint8_t { B_NONE = 0, B_EXACT = 1, B_LOWER = 2, B_UPPER = 3 };

struct Entry {
    uint64_t key;     // full zobrist key (collision guard)
    int16_t  score;   // stored score (mate scores are ply-adjusted by search)
    uint16_t move;    // best move found here
    int16_t  depth;   // search depth this result is valid for
    uint8_t  bound;   // EXACT / LOWER / UPPER
};

void init();                 // allocate + clear (call once)
void clear();                // wipe all entries (new game)
bool probe(uint64_t key, Entry& out);
void store(uint64_t key, int depth, int score, Bound bound, Move move);

} // namespace TT
