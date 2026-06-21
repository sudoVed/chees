// checknet.cpp - sanity-check a .nnue before shipping. Prints the eval (in
// pawns, White-relative) for obvious positions. A usable net should value a
// queen near +9 and a rook near +5; if a queen reads ~+0.5, the net is blind
// (undertrained) and will hang pieces.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../engine/board/position.h"
#include "../engine/board/zobrist.h"
#include "../engine/movegen/attacks.h"
#include "../engine/nn/nnue.h"

static NnueEvalQ ev;
static NnueEval evF;
static bool g_float=false;
static bool loadnet(const char* p){
  FILE*f=fopen(p,"rb"); if(!f){printf("cannot open %s\n",p); return false;}
  fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  unsigned char*b=(unsigned char*)malloc(n);
  if(fread(b,1,n,f)!=(size_t)n){printf("read fail\n");return false;} fclose(f);
  bool ok;
  if (b[3]=='1') { ok = evF.net.load(b,n); g_float = true; }       // NNU1 = float, no quant
  else { ok = (b[3]=='2'||b[3]=='3') && ev.net.load_q(b,n); }
  printf("net: %s  (%ld bytes, %s)\n\n", p, n,
         b[3]=='3'?"NNU3 int16-FT":(b[3]=='2'?"NNU2 int8":"NNU1 float (no quant)"));
  return ok;
}
static double pawns(const char* fen){ Position pos; pos.set_fen(fen,strlen(fen));
  int stm = g_float ? evF.net.evaluate(pos) : ev.net.evaluate_q(pos); int wr=(pos.side==WHITE)?stm:-stm; return wr/100.0; }

int main(int argc,char**argv){
  Zobrist::init(); Attacks::init();
  const char* path = argc>1?argv[1]:"frontend/models/net.nnue";
  if(!loadnet(path)) return 1;
  double sp = pawns("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  double uq = pawns("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  double dq = pawns("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1");
  double ur = pawns("1nbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQk - 0 1");
  double up = pawns("rnbqkbnr/ppp1pppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); // black missing a pawn -> white +1
  printf("  start position        %+6.2f   (want  ~0)\n", sp);
  printf("  WHITE up a queen      %+6.2f   (want ~+9)\n", uq);
  printf("  WHITE down a queen    %+6.2f   (want ~-9)\n", dq);
  printf("  WHITE up a rook       %+6.2f   (want ~+5)\n", ur);
  printf("  WHITE up a pawn       %+6.2f   (want ~+1)\n", up);
  double q = uq - sp;   // signal for one queen
  printf("\nverdict: a queen is worth ~%.2f pawns to this net -> %s\n",
         q, q > 4.0 ? "OK (has real material sense)"
                    : "BLIND / undertrained (will hang pieces)");
  return 0;
}
