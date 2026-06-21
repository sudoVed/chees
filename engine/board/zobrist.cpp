#include "zobrist.h"

namespace Zobrist {
    U64 psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];
    U64 castling[16];
    U64 epFile[8];
    U64 side;

    // splitmix64 — deterministic, high-quality fill. No <random> in freestanding.
    static U64 next(U64& s) {
        s += 0x9E3779B97F4A7C15ULL;
        U64 z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    void init() {
        U64 s = 0x1234567890ABCDEFULL;
        for (int c = 0; c < COLOR_NB; ++c)
            for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
                for (int sq = 0; sq < SQUARE_NB; ++sq)
                    psq[c][pt][sq] = next(s);
        for (int i = 0; i < 16; ++i) castling[i] = next(s);
        for (int f = 0; f < 8; ++f) epFile[f] = next(s);
        side = next(s);
    }
}
