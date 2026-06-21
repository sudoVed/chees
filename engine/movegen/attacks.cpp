#include "attacks.h"

namespace Attacks {
    U64 knight[SQUARE_NB];
    U64 king[SQUARE_NB];
    U64 pawn[COLOR_NB][SQUARE_NB];

    // Build a leaper attack set from (file,rank) offset deltas, clamping to board.
    static U64 leaper(int sq, const int (*deltas)[2], int n) {
        U64 bb = 0;
        int f = file_of(sq), r = rank_of(sq);
        for (int i = 0; i < n; ++i) {
            int nf = f + deltas[i][0];
            int nr = r + deltas[i][1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                bb |= sq_bb(make_square(nf, nr));
        }
        return bb;
    }

    void init() {
        const int kDeltas[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
        const int gDeltas[8][2] = {{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
        for (int sq = 0; sq < 64; ++sq) {
            knight[sq] = leaper(sq, kDeltas, 8);
            king[sq]   = leaper(sq, gDeltas, 8);

            // pawn attacks (the two forward diagonals for each color)
            U64 w = 0, b = 0;
            int f = file_of(sq), r = rank_of(sq);
            if (f > 0 && r < 7) w |= sq_bb(make_square(f - 1, r + 1));
            if (f < 7 && r < 7) w |= sq_bb(make_square(f + 1, r + 1));
            if (f > 0 && r > 0) b |= sq_bb(make_square(f - 1, r - 1));
            if (f < 7 && r > 0) b |= sq_bb(make_square(f + 1, r - 1));
            pawn[WHITE][sq] = w;
            pawn[BLACK][sq] = b;
        }
    }

    // Ray-walk a single direction (df,dr) until edge or blocker (inclusive).
    static U64 ray(int sq, int df, int dr, U64 occ) {
        U64 bb = 0;
        int f = file_of(sq), r = rank_of(sq);
        for (;;) {
            f += df; r += dr;
            if (f < 0 || f > 7 || r < 0 || r > 7) break;
            int t = make_square(f, r);
            bb |= sq_bb(t);
            if (occ & sq_bb(t)) break;   // blocker stops the ray (square is attackable)
        }
        return bb;
    }

    U64 bishop(int sq, U64 occ) {
        return ray(sq, 1, 1, occ) | ray(sq, -1, 1, occ)
             | ray(sq, 1, -1, occ) | ray(sq, -1, -1, occ);
    }
    U64 rook(int sq, U64 occ) {
        return ray(sq, 1, 0, occ) | ray(sq, -1, 0, occ)
             | ray(sq, 0, 1, occ) | ray(sq, 0, -1, occ);
    }
}
