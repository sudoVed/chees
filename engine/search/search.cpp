// search.cpp — alpha-beta + iterative deepening + TT + move ordering + quiescence,
// with null-move pruning, late-move reductions and aspiration windows. The NNUE
// accumulator is threaded through make/unmake; an optional node budget bounds it.
#include "search.h"
#include "tt.h"
#include "../movegen/movegen.h"
#include "../eval/hce.h"
#include "../board/zobrist.h"

namespace Search {

static const int INF        = 32000;
static const int MATE       = 30000;
static const int MATE_IN_MAX = MATE - (int)MAX_PLY;
// Contempt: treat a draw as slightly bad for the side to move, so the engine
// fights on (avoids repetition / 50-move draws) when the position is roughly
// equal. It will still take a draw when the alternative is clearly worse.
static const int CONTEMPT   = 35;

static const int VAL[6] = {100, 320, 330, 500, 900, 20000};

static HandCraftedEval g_hce;
static Evaluator* g_eval = &g_hce;

static long g_nodes;
static long g_maxNodes;
static bool g_abort;
static Move g_rootBest;
static Move g_killers[MAX_PLY][2];
static int  g_history[2][64][64];

void set_evaluator(Evaluator* e) { g_eval = e ? e : &g_hce; }

void new_game() {
    TT::clear();
    for (int p = 0; p < MAX_PLY; ++p) g_killers[p][0] = g_killers[p][1] = 0;
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 64; ++f)
            for (int t = 0; t < 64; ++t) g_history[c][f][t] = 0;
}

static inline void do_make(Position& pos, Move m)   { pos.make(m);  g_eval->acc_make(pos); }
static inline void do_unmake(Position& pos, Move m) { g_eval->acc_unmake(); pos.unmake(m); }

static inline bool has_non_pawn(const Position& pos, Color c) {
    return (pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP) |
            pos.pieces(c, ROOK)   | pos.pieces(c, QUEEN)) != 0;
}

static inline int to_tt(int s, int ply) {
    if (s >=  MATE_IN_MAX) return s + ply;
    if (s <= -MATE_IN_MAX) return s - ply;
    return s;
}
static inline int from_tt(int s, int ply) {
    if (s >=  MATE_IN_MAX) return s - ply;
    if (s <= -MATE_IN_MAX) return s + ply;
    return s;
}

static inline int move_score(const Position& pos, Move m, Move ttMove, int ply) {
    if (m == ttMove) return 2000000;
    if (is_capture(m)) {
        int victim   = is_ep(m) ? PAWN : pos.board[move_to(m)];
        int attacker = pos.board[move_from(m)];
        if (victim < 0 || victim > 5) victim = PAWN;
        return 1000000 + VAL[victim] * 16 - VAL[attacker];
    }
    if (is_promotion(m)) return 950000 + (int)promo_type(m);
    if (m == g_killers[ply][0]) return 800000;
    if (m == g_killers[ply][1]) return 700000;
    return g_history[pos.side][move_from(m)][move_to(m)];
}

static inline void pick(MoveList& ml, int* sc, int i) {
    int best = i;
    for (int j = i + 1; j < ml.count; ++j) if (sc[j] > sc[best]) best = j;
    if (best != i) {
        Move tm = ml.moves[i]; ml.moves[i] = ml.moves[best]; ml.moves[best] = tm;
        int ts = sc[i]; sc[i] = sc[best]; sc[best] = ts;
    }
}

static inline bool budget_hit() {
    if (g_abort) return true;
    if (g_maxNodes && g_nodes >= g_maxNodes) { g_abort = true; return true; }
    return false;
}

static int qsearch(Position& pos, int alpha, int beta, int ply) {
    if (budget_hit()) return alpha;
    g_nodes++;
    int stand = g_eval->eval_inc(pos);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    MoveList ml; MoveGen::generate_legal(pos, ml);
    int sc[MAX_MOVES];
    int nCap = 0;
    for (int i = 0; i < ml.count; ++i) {
        Move m = ml.moves[i];
        if (is_capture(m) || is_promotion(m)) {
            ml.moves[nCap] = m; sc[nCap] = move_score(pos, m, 0, ply); nCap++;
        }
    }
    ml.count = nCap;

    for (int i = 0; i < ml.count; ++i) {
        pick(ml, sc, i);
        Move m = ml.moves[i];
        do_make(pos, m);
        int score = -qsearch(pos, -beta, -alpha, ply + 1);
        do_unmake(pos, m);
        if (g_abort) return alpha;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

static int negamax(Position& pos, int depth, int alpha, int beta, int ply) {
    if (budget_hit()) return 0;
    g_nodes++;

    if (ply > 0 && (pos.is_repetition() || pos.halfmoveClock >= 100)) return -CONTEMPT;
    if (depth <= 0) return qsearch(pos, alpha, beta, ply);

    Move ttMove = 0;
    TT::Entry e;
    if (TT::probe(pos.key, e)) {
        ttMove = e.move;
        if (ply > 0 && e.depth >= depth) {
            int s = from_tt(e.score, ply);
            if (e.bound == TT::B_EXACT) return s;
            if (e.bound == TT::B_LOWER && s > alpha) alpha = s;
            else if (e.bound == TT::B_UPPER && s < beta) beta = s;
            if (alpha >= beta) return s;
        }
    }

    bool inCheck = pos.in_check(pos.side);

    // ---- null-move pruning: give the opponent a free move; if we're still
    // winning (fail-high), this line is too good and we can prune. Skipped in
    // check, in shallow nodes, and in pawn-only endings (zugzwang risk). A null
    // move changes no pieces, so the NNUE accumulator stays valid. ----
    if (!inCheck && depth >= 3 && ply > 0 && beta < MATE_IN_MAX &&
        has_non_pawn(pos, pos.side)) {
        int savedEp = pos.epSquare; U64 savedKey = pos.key;
        pos.key ^= Zobrist::side;
        if (savedEp != -1) pos.key ^= Zobrist::epFile[file_of(savedEp)];
        pos.epSquare = -1; pos.side = ~pos.side;
        int R = 2 + (depth >= 6 ? 1 : 0);
        int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1);
        pos.side = ~pos.side; pos.epSquare = savedEp; pos.key = savedKey;
        if (g_abort) return 0;
        if (score >= beta) return beta;
    }

    MoveList ml; MoveGen::generate_legal(pos, ml);
    if (ml.count == 0) return inCheck ? (-MATE + ply) : 0;

    int sc[MAX_MOVES];
    for (int i = 0; i < ml.count; ++i) sc[i] = move_score(pos, ml.moves[i], ttMove, ply);

    int origAlpha = alpha, best = -INF;
    Move bestMove = 0;
    for (int i = 0; i < ml.count; ++i) {
        pick(ml, sc, i);
        Move m = ml.moves[i];
        bool quiet = !is_capture(m) && !is_promotion(m);
        do_make(pos, m);

        int newDepth = depth - 1, score;
        // late-move reductions: search likely-bad late quiet moves shallower,
        // re-search at full depth only if they surprise us by beating alpha.
        if (depth >= 3 && i >= 4 && quiet && !inCheck) {
            int r = 1 + (depth >= 6 ? 1 : 0) + (i >= 8 ? 1 : 0);
            int rd = newDepth - r; if (rd < 1) rd = 1;
            score = -negamax(pos, rd, -alpha - 1, -alpha, ply + 1);
            if (!g_abort && score > alpha)
                score = -negamax(pos, newDepth, -beta, -alpha, ply + 1);
        } else {
            score = -negamax(pos, newDepth, -beta, -alpha, ply + 1);
        }

        do_unmake(pos, m);
        if (g_abort) return 0;

        if (score > best) { best = score; bestMove = m; if (ply == 0) g_rootBest = m; }
        if (score > alpha) alpha = score;
        if (alpha >= beta) {
            if (quiet) {
                if (g_killers[ply][0] != m) {
                    g_killers[ply][1] = g_killers[ply][0];
                    g_killers[ply][0] = m;
                }
                g_history[pos.side][move_from(m)][move_to(m)] += depth * depth;
            }
            break;
        }
    }

    TT::Bound b = best <= origAlpha ? TT::B_UPPER : (best >= beta ? TT::B_LOWER : TT::B_EXACT);
    TT::store(pos.key, depth, to_tt(best, ply), b, bestMove);
    return best;
}

Result think(Position& pos, int maxDepth, long maxNodes) {
    g_nodes = 0; g_rootBest = 0; g_abort = false; g_maxNodes = maxNodes;
    for (int p = 0; p < MAX_PLY; ++p) g_killers[p][0] = g_killers[p][1] = 0;
    g_eval->acc_refresh(pos);

    Result res{0, 0, 0, 0};
    int prevScore = 0;
    for (int depth = 1; depth <= maxDepth; ++depth) {
        int delta = 30, alpha = -INF, beta = INF;
        if (depth >= 4) { alpha = prevScore - delta; beta = prevScore + delta; }
        int score;
        for (;;) {                                   // aspiration window + widen on fail
            score = negamax(pos, depth, alpha, beta, 0);
            if (g_abort) break;
            if (score <= alpha) { alpha -= delta; delta += delta; if (alpha < -INF) alpha = -INF; }
            else if (score >= beta) { beta += delta; delta += delta; if (beta > INF) beta = INF; }
            else break;
        }
        if (g_abort) break;
        res.best = g_rootBest; res.score = score; res.depth = depth; prevScore = score;
    }
    res.nodes = g_nodes;

    if (res.best == 0) {
        MoveList ml; MoveGen::generate_legal(pos, ml);
        if (ml.count) res.best = ml.moves[0];
    }
    return res;
}

} // namespace Search
