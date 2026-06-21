// nnue_search_test.cpp — verify the incremental accumulator threaded through
// search gives IDENTICAL results to a refresh-every-node reference.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/search/search.h"
#include "../engine/search/tt.h"
#include "../engine/nn/nnue.h"

// reference evaluator: refresh-every-node (uses default no-op acc hooks)
struct RefEval : Evaluator {
    NN::Network n;
    int evaluate(const Position& p) const override { return const_cast<NN::Network&>(n).evaluate_q(p); }
};

static void sq(char*b,int s){b[0]='a'+(s&7);b[1]='1'+(s>>3);b[2]=0;}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    Zobrist::init(); Attacks::init(); TT::init();
    // load the (random) net + quantize -> fills the global int8/int16 tables
    FILE* f=fopen("/tmp/net.bin","rb"); fseek(f,0,SEEK_END); long len=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char* blob=(unsigned char*)malloc(len);
    if(fread(blob,1,len,f)!=(size_t)len){printf("read fail\n");return 1;} fclose(f);

    static NnueEvalQ inc;          // incremental
    static RefEval   ref;          // refresh-every-node
    inc.net.load(blob,len); inc.net.quantize();   // fills global quantized tables

    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 b kq - 0 1",
        "r2q1rk1/pp1nbppp/2p1pn2/3p4/2PP1B2/2N1PN2/PP3PPP/R2Q1RK1 w - - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", // Kiwipete
    };
    int D=4, fails=0;
    char a[3],b[3];
    for(const char* fen: fens){
        Position p1,p2; p1.set_fen(fen,strlen(fen)); p2.set_fen(fen,strlen(fen));
        Search::set_evaluator(&ref); Search::new_game(); Search::Result r=Search::think(p1,D);
        Search::set_evaluator(&inc); Search::new_game(); Search::Result i=Search::think(p2,D);
        bool ok = (r.best==i.best && r.score==i.score && r.nodes==i.nodes && inc.sp==0);
        sq(a,move_from(r.best));sq(b,move_to(r.best));
        printf("  ref: %s%s sc=%d nodes=%ld | inc nodes=%ld sp=%d -> %s\n",
               a,b,r.score,r.nodes,i.nodes,inc.sp, ok?"MATCH":"DIFF");
        if(!ok) fails++;
    }
    printf("== %s ==\n", fails==0?"PASS (incremental == reference)":"FAIL");
    free(blob);
    return fails==0?0:1;
}
