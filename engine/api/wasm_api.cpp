// wasm_api.cpp — C ABI exposed to JS. Raw exports + shared-memory buffers
// (no embind, since we compile freestanding wasm32 with Zig/clang).
//
// Contract mirrors the gameplan's API surface:
//   engine_init, new_game, load_fen, get_board, get_legal_moves(square),
//   is_capture, make_move, get_status, undo, side_to_move.
#include "../board/position.h"
#include "../board/zobrist.h"
#include "../movegen/attacks.h"
#include "../movegen/movegen.h"
#include "../rules/gamestate.h"
#include "../eval/hce.h"
#include "../search/search.h"
#include "../search/tt.h"
#include "../nn/nnue.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

// ----------------- global game state -----------------
static Position g_pos;

// Moves actually played, so undo() can unmake them.
static Move g_gameMoves[MAX_PLY];
static int  g_gamePly = 0;

// Shared buffers the JS side reads via the exported wasm memory.
static unsigned char g_board[64];      // piece codes (see encode_board)
static int g_targets[MAX_MOVES];       // legal destination squares from a square
static int g_flags[MAX_MOVES];         // matching move flags
static int g_legalCount = 0;
static char g_fenBuf[256];             // JS writes a FEN here, then calls load_fen

static int g_lastFrom = -1, g_lastTo = -1;

// piece code: 0 empty; white P..K = 1..6; black p..k = 7..12
static void encode_board() {
    for (int sq = 0; sq < 64; ++sq) {
        int pt = g_pos.board[sq];
        if (pt == NO_PIECE_TYPE) { g_board[sq] = 0; continue; }
        Color c = g_pos.color_on(sq);
        g_board[sq] = (unsigned char)(pt + 1 + (c == BLACK ? 6 : 0));
    }
}

// ----------------- lifecycle -----------------
EXPORT void engine_init() {
    Zobrist::init();
    Attacks::init();
    TT::init();
    Search::new_game();
    g_pos.set_startpos();
    g_gamePly = 0;
    g_lastFrom = g_lastTo = -1;
    encode_board();
}

EXPORT void new_game() {
    Search::new_game();
    g_pos.set_startpos();
    g_gamePly = 0;
    g_lastFrom = g_lastTo = -1;
    encode_board();
}

EXPORT char* get_fen_buf() { return g_fenBuf; }

EXPORT int load_fen(int len) {
    if (len < 0 || len > 255) return 0;
    bool ok = g_pos.set_fen(g_fenBuf, len);
    g_gamePly = 0;
    g_lastFrom = g_lastTo = -1;
    encode_board();
    return ok ? 1 : 0;
}

// ----------------- queries -----------------
EXPORT unsigned char* get_board() { encode_board(); return g_board; }
EXPORT int side_to_move()  { return (int)g_pos.side; }
EXPORT int get_castling()  { return g_pos.castling; }
EXPORT int get_ep()        { return g_pos.epSquare; }
EXPORT int halfmove_clock(){ return g_pos.halfmoveClock; }
EXPORT int fullmove_number(){ return g_pos.fullmoveNumber; }
EXPORT int last_move_from(){ return g_lastFrom; }
EXPORT int last_move_to()  { return g_lastTo; }

EXPORT int* get_targets_buf() { return g_targets; }
EXPORT int* get_flags_buf()   { return g_flags; }

// Fill g_targets/g_flags with legal moves from `square`; return the count.
// This is THE limiter the UI consumes: a piece may move only to these squares.
EXPORT int get_legal_moves(int square) {
    MoveList legal;
    MoveGen::generate_legal(g_pos, legal);
    g_legalCount = 0;
    for (int i = 0; i < legal.count; ++i) {
        Move m = legal.moves[i];
        if (move_from(m) != square) continue;
        // collapse the 4 promotion entries into a single destination
        bool dup = false;
        for (int j = 0; j < g_legalCount; ++j)
            if (g_targets[j] == move_to(m)) { dup = true; break; }
        if (dup) continue;
        g_targets[g_legalCount] = move_to(m);
        g_flags[g_legalCount] = move_flag(m);
        g_legalCount++;
    }
    return g_legalCount;
}

// Does a legal move from->to exist, and is it a capture? (for red highlight)
EXPORT int api_is_capture(int from, int to) {
    MoveList legal;
    MoveGen::generate_legal(g_pos, legal);
    for (int i = 0; i < legal.count; ++i) {
        Move m = legal.moves[i];
        if (move_from(m) == from && move_to(m) == to)
            return ::is_capture(m) ? 1 : 0;
    }
    return 0;
}

// Is the move from->to a promotion? (UI should pop a piece picker)
EXPORT int api_is_promotion(int from, int to) {
    MoveList legal;
    MoveGen::generate_legal(g_pos, legal);
    for (int i = 0; i < legal.count; ++i) {
        Move m = legal.moves[i];
        if (move_from(m) == from && move_to(m) == to && ::is_promotion(m))
            return 1;
    }
    return 0;
}

// Apply a legal move. promo: 0=auto(queen) else PieceType 1..4 (N,B,R,Q).
// Returns the resulting Status, or -1 if the move is not legal.
EXPORT int api_make_move(int from, int to, int promo) {
    MoveList legal;
    MoveGen::generate_legal(g_pos, legal);
    Move chosen = 0;
    bool found = false;
    for (int i = 0; i < legal.count; ++i) {
        Move m = legal.moves[i];
        if (move_from(m) != from || move_to(m) != to) continue;
        if (::is_promotion(m)) {
            int want = (promo == 0) ? QUEEN : promo;
            if ((int)promo_type(m) != want) continue;
        }
        chosen = m; found = true; break;
    }
    if (!found) return -1;
    g_pos.make(chosen);
    g_gameMoves[g_gamePly++] = chosen;
    g_lastFrom = from; g_lastTo = to;
    encode_board();
    return (int)Rules::status(g_pos);
}

// Revert the last played move. Returns 1 if something was undone.
EXPORT int undo() {
    if (g_gamePly == 0) return 0;
    Move m = g_gameMoves[--g_gamePly];
    g_pos.unmake(m);
    if (g_gamePly > 0) {
        Move prev = g_gameMoves[g_gamePly - 1];
        g_lastFrom = move_from(prev); g_lastTo = move_to(prev);
    } else { g_lastFrom = g_lastTo = -1; }
    encode_board();
    return 1;
}

EXPORT int get_status() { return (int)Rules::status(g_pos); }

EXPORT int in_check() { return g_pos.in_check(g_pos.side) ? 1 : 0; }

// ----------------- perft (correctness harness) -----------------
static long perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList legal;
    MoveGen::generate_legal(pos, legal);
    if (depth == 1) return legal.count;
    long nodes = 0;
    for (int i = 0; i < legal.count; ++i) {
        pos.make(legal.moves[i]);
        nodes += perft(pos, depth - 1);
        pos.unmake(legal.moves[i]);
    }
    return nodes;
}

EXPORT long perft_current(int depth) { return perft(g_pos, depth); }

// ----------------- search (Phase 9) -----------------
// Returns the best move PACKED as a 16-bit Move (from | to<<6 | flag<<12).
// JS decodes from/to and (for promotions) the piece, then plays it through the
// normal api_make_move path so all UI/eval-bar updates happen identically.
static int g_searchScore = 0, g_searchNodes = 0, g_searchDepth = 0;

EXPORT int search_best_move(int depth, int maxNodes) {
    Search::Result r = Search::think(g_pos, depth, (long)maxNodes);
    g_searchScore = r.score;
    g_searchNodes = (int)r.nodes;
    g_searchDepth = r.depth;
    return (int)r.best;
}
EXPORT int search_score() { return g_searchScore; }   // centipawns, side-to-move
EXPORT int search_nodes() { return g_searchNodes; }
EXPORT int search_depth() { return g_searchDepth; }

// ----------------- NNUE loading (Phase 11) -----------------
// JS writes the net.nnue bytes into g_nnueBlob (via get_nnue_buf), then calls
// load_nnue(len). We parse + quantize, then point the search at the NNUE eval.
// The blob is only read during quantize; the quantized int8/int16 tables (in
// nnue.cpp) are what search uses afterward.
static const int NNUE_BLOB_MAX = 42 * 1024 * 1024 + 4096;  // > one NNU1 file
static unsigned char g_nnueBlob[NNUE_BLOB_MAX];
static NnueEvalQ g_nnue;
static int g_nnueLoaded = 0;

EXPORT unsigned char* get_nnue_buf() { return g_nnueBlob; }
EXPORT int nnue_blob_capacity() { return NNUE_BLOB_MAX; }

// Hand-crafted evaluator instance so search can be forced back to HCE even when
// a net is loaded (the four AI presets pick one eval or the other, never a mix).
static HandCraftedEval g_hceEval;

// Select which evaluator the SEARCH uses: 1 = NNUE, 0 = HCE. The two never mix:
// an NNUE game runs only the net, an HCE game only the hand-crafted eval.
// Switching flushes the TT (scores from a different eval aren't comparable).
// Guard: if NNUE is requested but no net is loaded we can't run NNUE, so we stay
// on HCE rather than dereference a null net — but the UI must not get here (it
// refuses to start an NNUE game unless the net loaded), so it's defensive only.
EXPORT void set_search_eval(int useNnue) {
    static int cur = -2;
    int want = (useNnue && g_nnueLoaded) ? 1 : 0;
    if (want == cur) return;
    Search::set_evaluator(want ? (Evaluator*)&g_nnue : (Evaluator*)&g_hceEval);
    Search::new_game();
    cur = want;
}

EXPORT int load_nnue(int len) {
    if (len <= 0 || len > NNUE_BLOB_MAX) return 0;
    bool ok;
    if (g_nnueBlob[3] == '2' || g_nnueBlob[3] == '3') {   // NNU2/NNU3 = pre-quantized
        ok = g_nnue.net.load_q(g_nnueBlob, (size_t)len);
    } else {                                     // NNU1 = float, quantize on load
        ok = g_nnue.net.load(g_nnueBlob, (size_t)len);
        if (ok) g_nnue.net.quantize();
    }
    if (!ok) return 0;
    Search::set_evaluator(&g_nnue);
    Search::new_game();        // flush the TT: existing entries were scored by a
                               // different evaluator and must not be reused.
    g_nnueLoaded = 1;
    return 1;
}
EXPORT int nnue_active() { return g_nnueLoaded; }

// NNUE evaluation of the current position, White-relative centipawns (for the
// eval bar). Falls back to the hand-crafted eval if no net is loaded.
EXPORT int eval_nnue_white() {
    if (g_nnueLoaded) {
        int v = g_nnue.net.evaluate_q(g_pos);   // side-to-move centipawns
        return (g_pos.side == WHITE) ? v : -v;
    }
    return Eval::evaluate_white(g_pos);
}
