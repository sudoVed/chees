// tournament.cpp - play two "models" and report win %. A model is (evaluator, depth):
//   nnue:<depth>  (uses --net)   |   hce:<depth>  (hand-crafted eval)
// Net format auto-detected: NNU1 = float (NO quantization), NNU2 = int8,
// NNU3 = int16 feature transformer.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/movegen/movegen.h"
#include "../engine/rules/gamestate.h"
#include "../engine/search/search.h"
#include "../engine/search/tt.h"
#include "../engine/eval/hce.h"
#include "../engine/nn/nnue.h"

struct Player { Evaluator* eval; int depth; bool isNnue; char name[40]; };

static HandCraftedEval g_hce;
static NnueEvalQ       g_nnueQ;     // quantized (int8/int16)
static NnueEval        g_nnueF;     // float (no quantization)
static Evaluator*      g_nnueEval = &g_nnueQ;
static const char*     g_netKind = "?";

static bool load_net(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = (unsigned char*)malloc(n);              // leaked on purpose
    bool ok = (fread(b, 1, n, f) == (size_t)n); fclose(f);
    if (!ok) return false;
    if (b[3] == '1') {                                         // NNU1 = float, no quant
        ok = g_nnueF.net.load(b, n); g_nnueEval = &g_nnueF; g_netKind = "NNU1 float (no quant)";
    } else {                                                   // NNU2 int8 / NNU3 int16-FT
        ok = g_nnueQ.net.load_q(b, n); g_nnueEval = &g_nnueQ;
        g_netKind = (b[3] == '3') ? "NNU3 int16-FT" : "NNU2 int8";
    }
    return ok;
}

static bool parse_player(const char* spec, Player& p) {
    char kind[16]; const char* colon = strchr(spec, ':');
    if (!colon) return false;
    size_t kl = colon - spec; if (kl >= sizeof(kind)) return false;
    memcpy(kind, spec, kl); kind[kl] = 0;
    int depth = atoi(colon + 1); if (depth < 1) return false;
    if (!strcmp(kind, "nnue")) { p.isNnue = true;  p.eval = nullptr; }
    else if (!strcmp(kind, "hce")) { p.isNnue = false; p.eval = &g_hce; }
    else return false;
    p.depth = depth;
    snprintf(p.name, sizeof(p.name), "%s:%d", kind, depth);
    return true;
}

static unsigned lcg(unsigned& s) { s = s * 1103515245u + 12345u; return (s >> 16) & 0x7fff; }

static int play_game(Player white, Player black, unsigned seed, int openPlies, int maxMoves, bool mixed) {
    Position pos; pos.set_startpos();
    unsigned s = seed ? seed : 1;
    for (int k = 0; k < openPlies; ++k) {
        MoveList ml; MoveGen::generate_legal(pos, ml);
        if (ml.count == 0) break;
        pos.make(ml.moves[lcg(s) % ml.count]);
    }
    for (int mv = 0; mv < maxMoves; ++mv) {
        Status st = Rules::status(pos);
        if (st == STATUS_CHECKMATE) return (pos.side == WHITE) ? -1 : +1;
        if (st == STATUS_STALEMATE || st == STATUS_DRAW_50 ||
            st == STATUS_DRAW_REPETITION || st == STATUS_DRAW_MATERIAL) return 0;
        Player& p = (pos.side == WHITE) ? white : black;
        Search::set_evaluator(p.eval);
        if (mixed) TT::clear();
        Search::Result r = Search::think(pos, p.depth, 0);
        if (r.best == 0) return 0;
        pos.make(r.best);
    }
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* aSpec = "nnue:6"; const char* bSpec = "hce:6";
    const char* netPath = "../dist/net.nnue";
    int games = 20, openPlies = 4, maxMoves = 200; unsigned seed = 1;
    for (int i = 1; i < argc - 1; ++i) {
        if (!strcmp(argv[i], "--a")) aSpec = argv[++i];
        else if (!strcmp(argv[i], "--b")) bSpec = argv[++i];
        else if (!strcmp(argv[i], "--games")) games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net")) netPath = argv[++i];
        else if (!strcmp(argv[i], "--openplies")) openPlies = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--maxmoves")) maxMoves = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atoi(argv[++i]);
    }
    Zobrist::init(); Attacks::init(); TT::init();

    Player A, B;
    if (!parse_player(aSpec, A) || !parse_player(bSpec, B)) {
        printf("bad player spec; use nnue:<depth> or hce:<depth>\n"); return 1;
    }
    if (A.isNnue || B.isNnue) {
        if (!load_net(netPath)) { printf("failed to load net: %s\n", netPath); return 1; }
        printf("loaded net %s  [%s]\n", netPath, g_netKind);
        if (A.isNnue) A.eval = g_nnueEval;
        if (B.isNnue) B.eval = g_nnueEval;
    }
    bool mixed = (A.eval != B.eval);
    printf("Tournament: A=%s  vs  B=%s   games=%d\n", A.name, B.name, games);

    int aWin = 0, bWin = 0, draw = 0;
    for (int g = 0; g < games; ++g) {
        bool aIsWhite = (g % 2 == 0);
        Player white = aIsWhite ? A : B, black = aIsWhite ? B : A;
        int res = play_game(white, black, seed + g * 2654435761u, openPlies, maxMoves, mixed);
        int aRes = aIsWhite ? res : -res;
        if (aRes > 0) aWin++; else if (aRes < 0) bWin++; else draw++;
        const char* winner = aRes > 0 ? A.name : (aRes < 0 ? B.name : "draw");
        printf("game %2d/%d:  %s(W) vs %s(B)  ->  %s%s    |  running:  A(%s) %d  -  %d B(%s)  -  %d draw\n",
               g + 1, games, white.name, black.name, (aRes == 0 ? "" : "won by "), winner,
               A.name, aWin, bWin, B.name, draw);
    }
    double tot = games, aScore = aWin + 0.5 * draw;
    printf("\nResults over %d games:\n", games);
    printf("  A (%s):  %d wins  (%.1f%%)\n", A.name, aWin, 100.0 * aWin / tot);
    printf("  B (%s):  %d wins  (%.1f%%)\n", B.name, bWin, 100.0 * bWin / tot);
    printf("  draws:        %d       (%.1f%%)\n", draw, 100.0 * draw / tot);
    printf("  score:  A %.1f - %.1f B   (A = %.1f%%)\n", aScore, games - aScore, 100.0 * aScore / tot);
    return 0;
}
