#include "tt.h"
#include <stddef.h>

namespace TT {

// 1<<20 entries * 16 bytes = 16 MB. Index by the low bits of the key.
static const int    BITS = 20;
static const size_t SIZE = (size_t)1 << BITS;
static const size_t MASK = SIZE - 1;
static Entry table[SIZE];

void init()  { clear(); }
void clear() { for (size_t i = 0; i < SIZE; ++i) table[i] = Entry{0, 0, 0, 0, B_NONE}; }

bool probe(uint64_t key, Entry& out) {
    const Entry& e = table[key & MASK];
    if (e.bound != B_NONE && e.key == key) { out = e; return true; }
    return false;
}

void store(uint64_t key, int depth, int score, Bound bound, Move move) {
    Entry& e = table[key & MASK];
    // depth-preferred, but always replace a stale/other-key slot
    if (e.bound == B_NONE || e.key != key || depth >= e.depth) {
        e.key = key; e.score = (int16_t)score; e.move = move;
        e.depth = (int16_t)depth; e.bound = (uint8_t)bound;
    }
}

} // namespace TT
