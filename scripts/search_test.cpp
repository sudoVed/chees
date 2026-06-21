#include <cstdio>
#include <cstring>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/search/search.h"
#include "../engine/search/tt.h"

static void sq(char* b, int s){ b[0]='a'+ (s&7); b[1]='1'+(s>>3); b[2]=0; }
static void mv(char* b, Move m){ char f[3],t[3]; sq(f,move_from(m)); sq(t,move_to(m)); sprintf(b,"%s%s",f,t); }

static Search::Result go(const char* fen, int depth){
    Position p; p.set_fen(fen, strlen(fen));
    unsigned long long k0 = p.key;
    Search::Result r = Search::think(p, depth);
    if (p.key != k0) printf("  !! position not restored\n");
    return r;
}

int main(){
    Zobrist::init(); Attacks::init(); TT::init(); Search::new_game();
    char b[8];

    printf("== mate in 1 (expect Rh8, score ~ +mate) ==\n");
    { Search::Result r = go("k7/7R/1K6/8/8/8/8/8 w - - 0 1", 4);
      mv(b,r.best); printf("  best=%s score=%d depth=%d nodes=%ld\n", b, r.score, r.depth, r.nodes); }

    printf("== win hanging queen (expect Rxd4 d1d4, score big +) ==\n");
    { Search::Result r = go("4k3/8/8/8/3q4/8/8/3RK3 w - - 0 1", 6);
      mv(b,r.best); printf("  best=%s score=%d depth=%d nodes=%ld\n", b, r.score, r.depth, r.nodes); }

    printf("== startpos: nodes by depth (pruning sanity) ==\n");
    for (int d=1; d<=6; ++d){
        Search::new_game();
        Search::Result r = go("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", d);
        mv(b,r.best); printf("  depth %d  best=%s  score=%d  nodes=%ld\n", d, b, r.score, r.nodes);
    }
    return 0;
}
