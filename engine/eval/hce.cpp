// hce.cpp — hand-crafted evaluation. See hce.h.
//
// Layout:
//   1. Constants: material, piece-square tables, weights.
//   2. EvalInfo: per-position scratch (attack maps, king zones, phase, ...)
//      computed once and shared by every term (no recomputation per term).
//   3. score_* term functions (Tiers 1-3 + additional), each returning a
//      tapered white-minus-black centipawn contribution.
//   4. evaluate / evaluate_white / evaluate_breakdown.
//
// Sign convention everywhere: positive = good for White. evaluate() negates
// for Black so the search gets a side-to-move-relative score.
#include "hce.h"
#include "../movegen/attacks.h"

namespace Eval {

// ============================================================
// 1. Constants
// ============================================================

// Phased material values (PeSTO). Index by PieceType.
static const int MAT_MG[6] = {  82, 337, 365, 477, 1025, 0 };
static const int MAT_EG[6] = {  94, 281, 297, 512,  936, 0 };
// Simple values for threat/exchange reasoning (king = "infinite").
static const int VAL[6]     = { 100, 320, 330, 500,  900, 20000 };

// Phase weights per piece type; total of a full board = 24.
static const int PHASE_W[6] = { 0, 1, 1, 2, 4, 0 };

// ---- Piece-square tables (positional only, written rank-8-first) -----------
// White reads pst[sq ^ 56]; Black reads pst[sq]; result subtracted (W - B).
static const int PST_PAWN_MG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
static const int PST_PAWN_EG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    80, 80, 80, 80, 80, 80, 80, 80,
    50, 50, 50, 50, 50, 50, 50, 50,
    30, 30, 30, 30, 30, 30, 30, 30,
    20, 20, 20, 20, 20, 20, 20, 20,
    10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
     0,  0,  0,  0,  0,  0,  0,  0
};
static const int PST_KNIGHT[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};
static const int PST_BISHOP[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};
static const int PST_ROOK[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};
static const int PST_QUEEN[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};
static const int PST_KING_MG[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
};
static const int PST_KING_EG[64] = {
   -50,-40,-30,-20,-20,-30,-40,-50,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,  0,  0,  0,  0,-30,-30,
   -50,-30,-30,-30,-30,-30,-30,-50
};

// ---- Tunable weights (hand-set now; Texel-tunable later) -------------------
// Mobility bonus per reachable safe square, by piece type.
static const int MOB[6] = { 0, 4, 4, 3, 2, 0 };
// King-zone attacker weight by piece type.
static const int KATT_W[6] = { 0, 2, 2, 3, 5, 0 };

// Term names for the debug breakdown (order matches enum Term).
const char* const TERM_NAMES[TERM_NB] = {
    "Material", "PST", "Center", "Defense/hanging", "Mobility", "Threats",
    "Pins", "King safety", "Pawn structure", "Rook activity", "Safe forks",
    "Space", "Coordination", "Outposts", "King activity", "Tactical motifs",
    "Tempo", "Bad bishop", "Trapped pieces", "Rook behind passer",
    "Battery", "King tropism", "Pawn storm", "Castling", "Piece on rim",
    "Threat by pawn", "Minor behind pawn", "Bishop pair"
};

// ============================================================
// helpers
// ============================================================

static const U64 FILE_BB[8] = {
    FILE_A_BB, FILE_A_BB<<1, FILE_A_BB<<2, FILE_A_BB<<3,
    FILE_A_BB<<4, FILE_A_BB<<5, FILE_A_BB<<6, FILE_A_BB<<7
};
static inline U64 rank_bb(int r) { return RANK_1_BB << (8 * r); }

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iabs(int a)        { return a < 0 ? -a : a; }
static inline int chebyshev(int a, int b) {
    return imax(iabs(file_of(a) - file_of(b)), iabs(rank_of(a) - rank_of(b)));
}

// Light/dark square masks (for bishop color logic).
static const U64 DARK_SQ  = 0xAA55AA55AA55AA55ULL;
static const U64 LIGHT_SQ = ~0xAA55AA55AA55AA55ULL;

// Squares "in front of" a pawn on `sq` for color c (the file ahead).
static inline U64 front_file(int sq, Color c) {
    U64 f = FILE_BB[file_of(sq)];
    if (c == WHITE) return f & ~((sq_bb(sq) << 1) - 1);     // ranks strictly above
    else            return f & (sq_bb(sq) - 1);             // ranks strictly below
}
// Three-file "passed-pawn span" ahead of `sq` for color c.
static inline U64 passed_span(int sq, Color c) {
    int f = file_of(sq);
    U64 files = FILE_BB[f];
    if (f > 0) files |= FILE_BB[f - 1];
    if (f < 7) files |= FILE_BB[f + 1];
    U64 ahead = (c == WHITE) ? ~((sq_bb(sq) << 1) - 1) : (sq_bb(sq) - 1);
    // restrict "ahead" to whole ranks ahead of sq
    U64 mask = 0;
    if (c == WHITE) for (int r = rank_of(sq) + 1; r < 8; ++r) mask |= rank_bb(r);
    else            for (int r = rank_of(sq) - 1; r >= 0; --r) mask |= rank_bb(r);
    (void)ahead;
    return files & mask;
}

// ============================================================
// 2. EvalInfo
// ============================================================
struct EvalInfo {
    const Position* pos;
    U64 occ;
    U64 col[2];
    U64 pawns[2];
    int ksq[2];
    U64 pawnAtt[2];
    U64 pawnAtt2[2];
    U64 att[2];            // union of all attacks by color
    U64 att2[2];           // squares attacked >= twice
    U64 attBy[2][6];       // attacks by piece type (union)
    U64 kingZone[2];       // ring around each king
    U64 mobArea[2];        // safe mobility area (not own, not enemy-pawn-attacked)
    int kAttCnt[2];        // # enemy pieces attacking color's king zone
    int kAttW[2];          // weighted king-zone pressure
    int phase;             // 0..24 (24 = full middlegame)
    int cnt[2][6];         // piece counts
};

static void merge(EvalInfo& ei, Color c, U64 a, PieceType pt) {
    ei.att2[c] |= ei.att[c] & a;
    ei.att[c]  |= a;
    ei.attBy[c][pt] |= a;
}

static void fill(EvalInfo& ei, const Position& pos) {
    ei.pos = &pos;
    ei.occ = pos.occupied();
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        ei.col[c]   = pos.byColor[c];
        ei.pawns[c] = pos.pieces(c, PAWN);
        ei.ksq[c]   = pos.king_square(c);
        for (int pt = 0; pt < 6; ++pt) {
            ei.cnt[c][pt] = popcount(pos.pieces(c, (PieceType)pt));
            ei.attBy[c][pt] = 0;
        }
        ei.att[c] = ei.att2[c] = 0;
        ei.kAttCnt[c] = ei.kAttW[c] = 0;
    }

    // pawn attacks
    U64 wp = ei.pawns[WHITE], bp = ei.pawns[BLACK];
    U64 wne = shift_ne(wp), wnw = shift_nw(wp);
    U64 bse = shift_se(bp), bsw = shift_sw(bp);
    ei.pawnAtt[WHITE]  = wne | wnw;  ei.pawnAtt2[WHITE] = wne & wnw;
    ei.pawnAtt[BLACK]  = bse | bsw;  ei.pawnAtt2[BLACK] = bse & bsw;

    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        ei.kingZone[c] = Attacks::king[ei.ksq[c]] | sq_bb(ei.ksq[c]);
        ei.mobArea[c]  = ~ei.col[c] & ~ei.pawnAtt[~c];
        // seed attack maps with pawns and king
        ei.att2[c] = ei.pawnAtt2[c];
        ei.att[c]  = ei.pawnAtt[c];
        ei.attBy[c][PAWN] = ei.pawnAtt[c];
        merge(ei, c, Attacks::king[ei.ksq[c]], KING);
    }

    // phase
    int ph = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1))
        for (int pt = KNIGHT; pt <= QUEEN; ++pt)
            ph += PHASE_W[pt] * ei.cnt[c][pt];
    ei.phase = imin(ph, 24);

    // per-piece attacks (knights, bishops, rooks, queens) + king-zone pressure
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        Color them = ~c;
        U64 b;
        b = pos.pieces(c, KNIGHT);
        while (b) { int s = pop_lsb(b); U64 a = Attacks::knight[s];
            merge(ei, c, a, KNIGHT);
            U64 z = a & ei.kingZone[them];
            if (z) { ei.kAttCnt[them]++; ei.kAttW[them] += KATT_W[KNIGHT] * popcount(z); } }
        b = pos.pieces(c, BISHOP);
        while (b) { int s = pop_lsb(b); U64 a = Attacks::bishop(s, ei.occ);
            merge(ei, c, a, BISHOP);
            U64 z = a & ei.kingZone[them];
            if (z) { ei.kAttCnt[them]++; ei.kAttW[them] += KATT_W[BISHOP] * popcount(z); } }
        b = pos.pieces(c, ROOK);
        while (b) { int s = pop_lsb(b); U64 a = Attacks::rook(s, ei.occ);
            merge(ei, c, a, ROOK);
            U64 z = a & ei.kingZone[them];
            if (z) { ei.kAttCnt[them]++; ei.kAttW[them] += KATT_W[ROOK] * popcount(z); } }
        b = pos.pieces(c, QUEEN);
        while (b) { int s = pop_lsb(b); U64 a = Attacks::queen(s, ei.occ);
            merge(ei, c, a, QUEEN);
            U64 z = a & ei.kingZone[them];
            if (z) { ei.kAttCnt[them]++; ei.kAttW[them] += KATT_W[QUEEN] * popcount(z); } }
    }
}

static inline int taper(const EvalInfo& ei, int mg, int eg) {
    return (mg * ei.phase + eg * (24 - ei.phase)) / 24;
}

// pst lookup helper (white flips rank)
static inline int pst(const int* t, Color c, int sq) {
    return (c == WHITE) ? t[sq ^ 56] : t[sq];
}

// ============================================================
// 3. Term functions  (each returns tapered White-minus-Black)
// ============================================================

// --- Tier 1 -----------------------------------------------------------------
static int score_material(const EvalInfo& ei) {
    int mg = 0, eg = 0;
    for (int pt = 0; pt < 6; ++pt) {
        mg += MAT_MG[pt] * (ei.cnt[WHITE][pt] - ei.cnt[BLACK][pt]);
        eg += MAT_EG[pt] * (ei.cnt[WHITE][pt] - ei.cnt[BLACK][pt]);
    }
    return taper(ei, mg, eg);
}

static int score_pst(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int mg = 0, eg = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 b;
        b = pos.pieces(c, PAWN);
        while (b) { int s = pop_lsb(b); mg += sgn*pst(PST_PAWN_MG,c,s); eg += sgn*pst(PST_PAWN_EG,c,s); }
        b = pos.pieces(c, KNIGHT);
        while (b) { int s = pop_lsb(b); int v = pst(PST_KNIGHT,c,s); mg += sgn*v; eg += sgn*v; }
        b = pos.pieces(c, BISHOP);
        while (b) { int s = pop_lsb(b); int v = pst(PST_BISHOP,c,s); mg += sgn*v; eg += sgn*v; }
        b = pos.pieces(c, ROOK);
        while (b) { int s = pop_lsb(b); int v = pst(PST_ROOK,c,s); mg += sgn*v; eg += sgn*v; }
        b = pos.pieces(c, QUEEN);
        while (b) { int s = pop_lsb(b); int v = pst(PST_QUEEN,c,s); mg += sgn*v; eg += sgn*v; }
        int k = ei.ksq[c];
        mg += sgn*pst(PST_KING_MG,c,k); eg += sgn*pst(PST_KING_EG,c,k);
    }
    return taper(ei, mg, eg);
}

static int score_center(const EvalInfo& ei) {
    const U64 CENTER = sq_bb(D4)|sq_bb(E4)|sq_bb(D5)|sq_bb(E5);
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        s += sgn * 25 * popcount(ei.pawns[c] & CENTER);
        s += sgn * 12 * popcount(ei.col[c] & ~ei.pawns[c] & CENTER);
        s += sgn *  6 * popcount(ei.att[c] & CENTER);
    }
    return s;  // not phase-dependent
}

static int score_mobility(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 b;
        b = pos.pieces(c, KNIGHT);
        while (b) { int sq = pop_lsb(b); s += sgn*MOB[KNIGHT]*popcount(Attacks::knight[sq] & ei.mobArea[c]); }
        b = pos.pieces(c, BISHOP);
        while (b) { int sq = pop_lsb(b); s += sgn*MOB[BISHOP]*popcount(Attacks::bishop(sq,ei.occ) & ei.mobArea[c]); }
        b = pos.pieces(c, ROOK);
        while (b) { int sq = pop_lsb(b); s += sgn*MOB[ROOK]*popcount(Attacks::rook(sq,ei.occ) & ei.mobArea[c]); }
        b = pos.pieces(c, QUEEN);
        while (b) { int sq = pop_lsb(b); s += sgn*MOB[QUEEN]*popcount(Attacks::queen(sq,ei.occ) & ei.mobArea[c]); }
    }
    return s;
}

static int score_threats(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    // Bonus for attacking an enemy piece with a strictly cheaper attacker.
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        // minor attacks on enemy rooks/queens
        U64 minorAtt = ei.attBy[c][KNIGHT] | ei.attBy[c][BISHOP];
        s += sgn * 45 * popcount(minorAtt & pos.pieces(them, ROOK));
        s += sgn * 60 * popcount(minorAtt & pos.pieces(them, QUEEN));
        // rook attacks on enemy queen
        s += sgn * 50 * popcount(ei.attBy[c][ROOK] & pos.pieces(them, QUEEN));
    }
    return s;
}

// between-squares (exclusive) along the line a..b, if aligned; else 0.
static U64 between_bb(int a, int b) {
    int fa = file_of(a), ra = rank_of(a), fb = file_of(b), rb = rank_of(b);
    int df = (fb > fa) - (fb < fa);
    int dr = (rb > ra) - (rb < ra);
    if (fa != fb && ra != rb && iabs(fb-fa) != iabs(rb-ra)) return 0; // not aligned
    U64 bb = 0; int f = fa + df, r = ra + dr;
    while (f != fb || r != rb) {
        if (f < 0 || f > 7 || r < 0 || r > 7) return 0;
        bb |= sq_bb(make_square(f, r));
        f += df; r += dr;
    }
    return bb;
}

static int score_pins(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        // penalty applies to color c whose piece is pinned to its king/queen
        int sgn = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        int targets[2] = { ei.ksq[c], -1 };
        U64 q = pos.pieces(c, QUEEN);
        if (q) targets[1] = lsb(q);
        for (int ti = 0; ti < 2; ++ti) {
            int k = targets[ti];
            if (k < 0) continue;
            U64 snipers =
                (Attacks::bishop(k, 0) & (pos.pieces(them,BISHOP) | pos.pieces(them,QUEEN))) |
                (Attacks::rook(k, 0)   & (pos.pieces(them,ROOK)   | pos.pieces(them,QUEEN)));
            U64 sn = snipers;
            while (sn) {
                int sp = pop_lsb(sn);
                U64 btw = between_bb(k, sp) & ei.occ;
                if (popcount(btw) == 1 && (btw & ei.col[c])) {
                    int pinned = lsb(btw);
                    int pv = VAL[pos.board[pinned]];
                    // pin to king: full penalty; to queen: lighter
                    int pen = (ti == 0) ? (18 + pv / 24) : (8 + pv / 48);
                    s -= sgn * pen;
                }
            }
        }
    }
    return s;
}

static int score_defense(const EvalInfo& ei) {
    // "Defense at equal value": hanging / under-defended non-pawn pieces.
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        for (int pt = KNIGHT; pt <= QUEEN; ++pt) {
            U64 b = pos.pieces(c, (PieceType)pt);
            while (b) {
                int sq = pop_lsb(b);
                bool attacked = ei.att[them] & sq_bb(sq);
                bool defended = ei.att[c]   & sq_bb(sq);
                if (!attacked) continue;
                // least valuable enemy attacker type
                int least = KING;
                for (int apt = PAWN; apt <= QUEEN; ++apt)
                    if (ei.attBy[them][apt] & sq_bb(sq)) { least = apt; break; }
                if (!defended)
                    s -= sgn * (VAL[pt] / 12 + 10);         // hanging
                else if (VAL[least] < VAL[pt])
                    s -= sgn * ((VAL[pt] - VAL[least]) / 16); // attacked by cheaper
            }
        }
    }
    return s;
}

// --- Tier 2 -----------------------------------------------------------------
static int score_king_safety(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int mg = 0;  // king safety is a middlegame concern; eg ~ 0
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        int k = ei.ksq[c];
        int danger = ei.kAttW[c];               // pressure from enemy pieces
        // pawn shield: friendly pawns on the 3 files around the king, ahead of it
        int kf = file_of(k);
        for (int f = imax(0, kf - 1); f <= imin(7, kf + 1); ++f) {
            U64 fp = ei.pawns[c] & FILE_BB[f];
            if (!fp) danger += 12;               // missing shield pawn on this file
        }
        // open / semi-open files pointing at the king
        for (int f = imax(0, kf - 1); f <= imin(7, kf + 1); ++f) {
            bool ownP   = ei.pawns[c]    & FILE_BB[f];
            bool enemyP = ei.pawns[~c]   & FILE_BB[f];
            if (!ownP && !enemyP) danger += 14;  // fully open
            else if (!ownP)       danger += 7;   // semi-open toward king
        }
        if (ei.kAttCnt[c] < 2) danger /= 3;      // a lone attacker is not scary
        int pen = danger * danger / 40;          // quadratic ramp
        if (pen > 600) pen = 600;
        mg -= sgn * pen;
    }
    return taper(ei, mg, 0);
}

static int score_pawn_structure(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int mg = 0, eg = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 own = ei.pawns[c], enemy = ei.pawns[~c];
        // doubled (per file: extra pawns beyond the first)
        for (int f = 0; f < 8; ++f) {
            int n = popcount(own & FILE_BB[f]);
            if (n > 1) { mg -= sgn * 12 * (n - 1); eg -= sgn * 24 * (n - 1); }
        }
        U64 b = own;
        while (b) {
            int sq = pop_lsb(b);
            int f = file_of(sq);
            U64 adj = 0;
            if (f > 0) adj |= FILE_BB[f - 1];
            if (f < 7) adj |= FILE_BB[f + 1];
            // isolated
            if (!(own & adj)) { mg -= sgn * 14; eg -= sgn * 18; }
            // passed (rank-scaled bonus)
            if (!(enemy & passed_span(sq, c)) && !(enemy & front_file(sq, c))) {
                int rel = (c == WHITE) ? rank_of(sq) : 7 - rank_of(sq); // 1..6
                static const int PASS_MG[8] = {0,5,10,20,35,60,100,0};
                static const int PASS_EG[8] = {0,10,20,35,60,100,160,0};
                mg += sgn * PASS_MG[rel];
                eg += sgn * PASS_EG[rel];
            }
            // connected / phalanx: friendly pawn beside or defending
            U64 neighbors = ei.pawnAtt[c] & sq_bb(sq);     // defended by own pawn
            U64 beside = adj & rank_bb(rank_of(sq)) & own;  // phalanx
            if (neighbors || beside) { mg += sgn * 8; eg += sgn * 6; }
            // backward: stop square attacked by enemy pawn, no friendly support
            int stop = sq + (c == WHITE ? 8 : -8);
            if (stop >= 0 && stop < 64) {
                bool stopAttacked = ei.pawnAtt[~c] & sq_bb(stop);
                if (stopAttacked && !(own & adj)) { mg -= sgn * 10; eg -= sgn * 12; }
            }
        }
    }
    return taper(ei, mg, eg);
}

static int score_rook_activity(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int mg = 0, eg = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 rooks = pos.pieces(c, ROOK);
        U64 b = rooks;
        int seventh = (c == WHITE) ? 6 : 1;
        while (b) {
            int sq = pop_lsb(b);
            int f = file_of(sq);
            bool ownP   = ei.pawns[c]  & FILE_BB[f];
            bool enemyP = ei.pawns[~c] & FILE_BB[f];
            if (!ownP && !enemyP) { mg += sgn * 25; eg += sgn * 12; }      // open
            else if (!ownP)       { mg += sgn * 12; eg += sgn * 6; }       // semi-open
            if (rank_of(sq) == seventh) { mg += sgn * 20; eg += sgn * 32; } // 7th rank
        }
        // connected rooks (see each other along rank/file)
        if (popcount(rooks) >= 2) {
            int r1 = lsb(rooks), r2 = msb(rooks);
            if ((file_of(r1)==file_of(r2) || rank_of(r1)==rank_of(r2)) &&
                !(between_bb(r1, r2) & ei.occ)) { mg += sgn * 12; eg += sgn * 8; }
        }
    }
    return taper(ei, mg, eg);
}

static int score_safe_forks(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        U64 valuable = pos.pieces(them, ROOK) | pos.pieces(them, QUEEN) | sq_bb(ei.ksq[them]);
        // knights already forking
        U64 b = pos.pieces(c, KNIGHT);
        while (b) {
            int sq = pop_lsb(b);
            if (popcount(Attacks::knight[sq] & valuable) >= 2) s += sgn * 35;
        }
        // safe landing squares that would fork (square not attacked by enemy)
        U64 land = ei.attBy[c][KNIGHT] & ei.mobArea[c] & ~ei.att[them];
        U64 l = land;
        while (l) {
            int t = pop_lsb(l);
            if (popcount(Attacks::knight[t] & valuable) >= 2) { s += sgn * 18; break; }
        }
    }
    return s;
}

// --- Tier 3 -----------------------------------------------------------------
static int score_space(const EvalInfo& ei) {
    // Safe squares controlled in the enemy half, central files c-f.
    const U64 CFILES = FILE_BB[2]|FILE_BB[3]|FILE_BB[4]|FILE_BB[5];
    int mg = 0;
    U64 whiteHalf = rank_bb(2)|rank_bb(3)|rank_bb(4)|rank_bb(5); // ranks 3-6 (0-idx 2-5)
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 region = CFILES & whiteHalf;
        U64 safe = region & ~ei.pawnAtt[~c] & ~ei.col[c];
        // only count squares on the side's "advanced" half
        U64 half = (c == WHITE) ? (rank_bb(3)|rank_bb(4)|rank_bb(5))
                                : (rank_bb(2)|rank_bb(3)|rank_bb(4));
        mg += sgn * 2 * popcount(safe & half);
    }
    return taper(ei, mg, 0);
}

static int score_coordination(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        // non-pawn pieces that are defended by a friendly piece/pawn
        U64 minorsMajors = ei.col[c] & ~ei.pawns[c] & ~sq_bb(ei.ksq[c]);
        s += sgn * 4 * popcount(minorsMajors & ei.att[c]);
    }
    return s;
}

static int score_outposts(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 minors = pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP);
        U64 b = minors;
        while (b) {
            int sq = pop_lsb(b);
            int rel = (c == WHITE) ? rank_of(sq) : 7 - rank_of(sq);
            if (rel < 3 || rel > 5) continue;                  // must be advanced
            if (!(ei.pawnAtt[c] & sq_bb(sq))) continue;        // pawn-protected
            if (ei.pawns[~c] & passed_span(sq, c)) continue;   // can be challenged
            bool knight = pos.board[sq] == KNIGHT;
            s += sgn * (knight ? 28 : 18);
        }
    }
    return s;
}

static int score_king_activity(const EvalInfo& ei) {
    // Endgame: reward a centralized king (tapered to the endgame).
    int eg = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        int k = ei.ksq[c];
        int distCenter = imin(chebyshev(k, D4), imin(chebyshev(k, E4),
                          imin(chebyshev(k, D5), chebyshev(k, E5))));
        eg += sgn * (3 - distCenter) * 8;
    }
    return taper(ei, 0, eg);
}

static int score_tactics(const EvalInfo& ei) {
    // Light static skewer/x-ray: our slider attacks an enemy piece that has a
    // strictly more valuable enemy piece directly behind it on the same ray.
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        int sliders[3] = { BISHOP, ROOK, QUEEN };
        for (int si = 0; si < 3; ++si) {
            int pt = sliders[si];
            U64 b = pos.pieces(c, (PieceType)pt);
            while (b) {
                int from = pop_lsb(b);
                U64 a = (pt == BISHOP) ? Attacks::bishop(from, ei.occ)
                      : (pt == ROOK)   ? Attacks::rook(from, ei.occ)
                                       : Attacks::queen(from, ei.occ);
                U64 hits = a & ei.col[them];
                U64 h = hits;
                while (h) {
                    int v = pop_lsb(h);
                    // square just beyond v from the slider's view
                    int df = (file_of(v) > file_of(from)) - (file_of(v) < file_of(from));
                    int dr = (rank_of(v) > rank_of(from)) - (rank_of(v) < rank_of(from));
                    int bf = file_of(v) + df, br = rank_of(v) + dr;
                    if (bf < 0 || bf > 7 || br < 0 || br > 7) continue;
                    int behind = make_square(bf, br);
                    if ((ei.col[them] & sq_bb(behind)) &&
                        VAL[pos.board[behind]] > VAL[pos.board[v]])
                        s += sgn * 18;  // skewer/x-ray pressure
                }
            }
        }
    }
    return s;
}

static int score_tempo(const EvalInfo& ei) {
    int mg = (ei.pos->side == WHITE) ? 18 : -18;
    return taper(ei, mg, 0);
}

// --- Additional terms -------------------------------------------------------
static int score_bad_bishop(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 b = pos.pieces(c, BISHOP);
        while (b) {
            int sq = pop_lsb(b);
            U64 sameColor = (sq_bb(sq) & DARK_SQ) ? DARK_SQ : LIGHT_SQ;
            int blocked = popcount(ei.pawns[c] & sameColor);
            s -= sgn * 3 * blocked;
        }
    }
    return s;
}

static int score_trapped(const EvalInfo& ei) {
    // A minor with no safe squares is only "trapped" in the bad sense when it
    // has ventured into enemy territory (classic Bxh7 / Na8 traps). An
    // undeveloped minor on its home rank legitimately has zero mobility in the
    // opening, so we require the piece to be advanced (relative rank >= 4)
    // before penalizing it. The search handles back-rank rook problems.
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 b;
        b = pos.pieces(c, KNIGHT);
        while (b) { int sq = pop_lsb(b);
            int rel = (c == WHITE) ? rank_of(sq) : 7 - rank_of(sq);
            if (rel >= 4 && popcount(Attacks::knight[sq] & ei.mobArea[c]) == 0) s -= sgn * 45; }
        b = pos.pieces(c, BISHOP);
        while (b) { int sq = pop_lsb(b);
            int rel = (c == WHITE) ? rank_of(sq) : 7 - rank_of(sq);
            if (rel >= 4 && popcount(Attacks::bishop(sq,ei.occ) & ei.mobArea[c]) == 0) s -= sgn * 45; }
    }
    return s;
}

static int score_rook_behind_passer(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 own = ei.pawns[c], enemy = ei.pawns[~c];
        U64 p = own;
        while (p) {
            int sq = pop_lsb(p);
            if (enemy & passed_span(sq, c)) continue;          // not passed
            if (enemy & front_file(sq, c)) continue;
            // friendly rook on same file, behind the pawn
            U64 behind = FILE_BB[file_of(sq)] & ~front_file(sq, c) & ~sq_bb(sq);
            if (pos.pieces(c, ROOK) & behind) s += sgn * 20;
        }
    }
    return s;
}

static int score_battery(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 rooks = pos.pieces(c, ROOK);
        U64 heavy = rooks | pos.pieces(c, QUEEN);
        // doubled rooks on a (semi-)open file
        for (int f = 0; f < 8; ++f) {
            if (popcount(rooks & FILE_BB[f]) >= 2 && !(ei.pawns[c] & FILE_BB[f]))
                s += sgn * 16;
        }
        // rook+queen aligned on a file or rank with nothing between
        if (rooks && pos.pieces(c, QUEEN)) {
            U64 q = pos.pieces(c, QUEEN);
            U64 r = rooks;
            while (r) {
                int rs = pop_lsb(r);
                U64 qq = q;
                while (qq) {
                    int qs = pop_lsb(qq);
                    if ((file_of(rs)==file_of(qs) || rank_of(rs)==rank_of(qs)) &&
                        !(between_bb(rs, qs) & ei.occ))
                        s += sgn * 14;
                }
            }
        }
        (void)heavy;
    }
    return s;
}

static int score_king_tropism(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int mg = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        int ek = ei.ksq[~c];
        int trop = 0;
        for (int pt = KNIGHT; pt <= QUEEN; ++pt) {
            int w = (pt == QUEEN) ? 4 : (pt == ROOK ? 2 : 2);
            U64 b = pos.pieces(c, (PieceType)pt);
            while (b) { int sq = pop_lsb(b); trop += w * (7 - chebyshev(sq, ek)); }
        }
        mg += sgn * trop / 2;
    }
    return taper(ei, mg, 0);
}

static int score_pawn_storm(const EvalInfo& ei) {
    // Advancing pawns toward the enemy king's file region.
    int mg = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        int ekf = file_of(ei.ksq[~c]);
        U64 b = ei.pawns[c];
        while (b) {
            int sq = pop_lsb(b);
            if (iabs(file_of(sq) - ekf) > 2) continue;
            int rel = (c == WHITE) ? rank_of(sq) : 7 - rank_of(sq);
            if (rel >= 3) mg += sgn * (rel - 2) * 6;
        }
    }
    return taper(ei, mg, 0);
}

static int score_castling(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int mg = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        int k = ei.ksq[c];
        int homeF = file_of(k);
        int backRank = (c == WHITE) ? 0 : 7;
        // king tucked on the g/b/c-file home rank ~ "has castled"
        if (rank_of(k) == backRank && (homeF >= 5 || homeF <= 2) && homeF != 4)
            mg += sgn * 28;
        // retained rights are worth something
        int rights = (c == WHITE) ? ((pos.castling & WHITE_OO) ? 1:0) + ((pos.castling & WHITE_OOO)?1:0)
                                   : ((pos.castling & BLACK_OO) ? 1:0) + ((pos.castling & BLACK_OOO)?1:0);
        mg += sgn * 8 * rights;
        // king stuck in the center with no rights = bad
        if (homeF == 4 && rank_of(k) == backRank && rights == 0) mg -= sgn * 20;
    }
    return taper(ei, mg, 0);
}

static int score_rim(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    const U64 RIM = FILE_BB[0] | FILE_BB[7];
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        s -= sgn * 12 * popcount(pos.pieces(c, KNIGHT) & RIM);
        s -= sgn * 6  * popcount(pos.pieces(c, BISHOP) & RIM);
    }
    return s;
}

static int score_threat_by_pawn(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        U64 victims = pos.pieces(them, KNIGHT) | pos.pieces(them, BISHOP)
                    | pos.pieces(them, ROOK)   | pos.pieces(them, QUEEN);
        s += sgn * 22 * popcount(ei.pawnAtt[c] & victims);
    }
    return s;
}

static int score_minor_behind_pawn(const EvalInfo& ei) {
    const Position& pos = *ei.pos;
    int s = 0;
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        int sgn = (c == WHITE) ? 1 : -1;
        U64 minors = pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP);
        U64 b = minors;
        while (b) {
            int sq = pop_lsb(b);
            int front = sq + (c == WHITE ? 8 : -8);
            if (front >= 0 && front < 64 && (ei.pawns[c] & sq_bb(front)))
                s += sgn * 8;
        }
    }
    return s;
}

static int score_bishop_pair(const EvalInfo& ei) {
    int mg = 0, eg = 0;
    if (ei.cnt[WHITE][BISHOP] >= 2) { mg += 25; eg += 45; }
    if (ei.cnt[BLACK][BISHOP] >= 2) { mg -= 25; eg -= 45; }
    return taper(ei, mg, eg);
}

// ============================================================
// drawish material scaling (term 29)
// ============================================================
static int draw_scale(const EvalInfo& ei) {
    // Factor out of 64 applied to the final score, damping configurations that
    // are hard or impossible to win.
    int wp = ei.cnt[WHITE][PAWN], bp = ei.cnt[BLACK][PAWN];
    int wb = ei.cnt[WHITE][BISHOP], bb = ei.cnt[BLACK][BISHOP];
    // Non-pawn material (endgame values) per side.
    int mw = 0, mb = 0;
    for (int pt = KNIGHT; pt <= QUEEN; ++pt) {
        mw += MAT_EG[pt] * ei.cnt[WHITE][pt];
        mb += MAT_EG[pt] * ei.cnt[BLACK][pt];
    }
    // Opposite-coloured bishops, nothing but pawns -> drawish.
    bool ocb = (wb == 1 && bb == 1)
        && (ei.cnt[WHITE][KNIGHT]+ei.cnt[WHITE][ROOK]+ei.cnt[WHITE][QUEEN] == 0)
        && (ei.cnt[BLACK][KNIGHT]+ei.cnt[BLACK][ROOK]+ei.cnt[BLACK][QUEEN] == 0);
    if (ocb) {
        U64 wB = ei.pos->pieces(WHITE, BISHOP), bB = ei.pos->pieces(BLACK, BISHOP);
        if (((wB & DARK_SQ) != 0) != ((bB & DARK_SQ) != 0)) return 32;
    }
    // No pawns anywhere: only winnable with a real material edge (>= a rook's
    // worth). A bare-minor edge or less is a draw (KNvK, KBvK, K+minor v K+minor).
    if (wp == 0 && bp == 0) {
        int diff = mw - mb; if (diff < 0) diff = -diff;
        return (diff < 400) ? 16 : 64;
    }
    return 64;
}

// ============================================================
// 4. Aggregation
// ============================================================
static int run_terms(const EvalInfo& ei, int out[TERM_NB]) {
    out[T_MATERIAL]          = score_material(ei);
    out[T_PST]               = score_pst(ei);
    out[T_CENTER]            = score_center(ei);
    out[T_DEFENSE]           = score_defense(ei);
    out[T_MOBILITY]          = score_mobility(ei);
    out[T_THREATS]           = score_threats(ei);
    out[T_PINS]              = score_pins(ei);
    out[T_KING_SAFETY]       = score_king_safety(ei);
    out[T_PAWN_STRUCT]       = score_pawn_structure(ei);
    out[T_ROOK_ACT]          = score_rook_activity(ei);
    out[T_SAFE_FORKS]        = score_safe_forks(ei);
    out[T_SPACE]             = score_space(ei);
    out[T_COORDINATION]      = score_coordination(ei);
    out[T_OUTPOSTS]          = score_outposts(ei);
    out[T_KING_ACTIVITY]     = score_king_activity(ei);
    out[T_TACTICS]           = score_tactics(ei);
    out[T_TEMPO]             = score_tempo(ei);
    out[T_BAD_BISHOP]        = score_bad_bishop(ei);
    out[T_TRAPPED]           = score_trapped(ei);
    out[T_ROOK_BEHIND_PASSER]= score_rook_behind_passer(ei);
    out[T_BATTERY]           = score_battery(ei);
    out[T_KING_TROPISM]      = score_king_tropism(ei);
    out[T_PAWN_STORM]        = score_pawn_storm(ei);
    out[T_CASTLING]          = score_castling(ei);
    out[T_RIM]               = score_rim(ei);
    out[T_THREAT_BY_PAWN]    = score_threat_by_pawn(ei);
    out[T_MINOR_BEHIND_PAWN] = score_minor_behind_pawn(ei);
    out[T_BISHOP_PAIR]       = score_bishop_pair(ei);
    int sum = 0;
    for (int i = 0; i < TERM_NB; ++i) sum += out[i];
    return sum;
}

int evaluate_breakdown(const Position& pos, int out[TERM_NB], int* phase, int* scale) {
    EvalInfo ei;
    fill(ei, const_cast<Position&>(pos));
    int sum = run_terms(ei, out);
    int sc = draw_scale(ei);
    if (phase) *phase = ei.phase;
    if (scale) *scale = sc;
    return sum * sc / 64;
}

int evaluate_white(const Position& pos) {
    int out[TERM_NB];
    return evaluate_breakdown(pos, out, nullptr, nullptr);
}

int evaluate(const Position& pos) {
    int w = evaluate_white(pos);
    return (pos.side == WHITE) ? w : -w;
}

} // namespace Eval
// end of hand-crafted evaluation
