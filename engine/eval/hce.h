// hce.h — hand-crafted evaluation (Phase 8). Implements the abstract
// Evaluator. The total is a phase-tapered, weighted sum of independent terms
// (Tiers 1-3 + the additional terms from the gameplan). Each term lives in its
// own score_* function in hce.cpp so it can be unit-tested and toggled while
// tuning. Score sign convention: white-minus-black internally; evaluate()
// flips to the side-to-move perspective for the search.
#pragma once
#include "evaluator.h"
#include "../board/position.h"

namespace Eval {

// Per-term breakdown indices (also the display order in the debug API).
enum Term {
    T_MATERIAL = 0,
    T_PST,
    T_CENTER,
    T_DEFENSE,
    T_MOBILITY,
    T_THREATS,
    T_PINS,
    T_KING_SAFETY,
    T_PAWN_STRUCT,
    T_ROOK_ACT,
    T_SAFE_FORKS,
    T_SPACE,
    T_COORDINATION,
    T_OUTPOSTS,
    T_KING_ACTIVITY,
    T_TACTICS,
    T_TEMPO,
    T_BAD_BISHOP,
    T_TRAPPED,
    T_ROOK_BEHIND_PASSER,
    T_BATTERY,
    T_KING_TROPISM,
    T_PAWN_STORM,
    T_CASTLING,
    T_RIM,
    T_THREAT_BY_PAWN,
    T_MINOR_BEHIND_PAWN,
    T_BISHOP_PAIR,
    TERM_NB
};

extern const char* const TERM_NAMES[TERM_NB];

// Full static eval from the side-to-move perspective (+ = stm better). Tapered
// between middlegame/endgame by phase, then scaled toward 0 in drawish material.
int evaluate(const Position& pos);

// White-minus-black tapered eval (independent of side to move) — for display.
int evaluate_white(const Position& pos);

// Fills out[TERM_NB] with each term's tapered white-minus-black contribution
// (BEFORE the drawish scale factor). Writes phase (0..24) and scale (out of 64)
// through the pointers. Returns the final scaled white-minus-black eval.
int evaluate_breakdown(const Position& pos, int out[TERM_NB], int* phase, int* scale);

} // namespace Eval

// Concrete evaluator implementing the abstract interface (search talks to this).
struct HandCraftedEval : Evaluator {
    int evaluate(const Position& pos) const override { return Eval::evaluate(pos); }
};
