// evaluator.h — ABSTRACT evaluation interface.
//
// Search talks to this, not a concrete evaluator. The hand-crafted eval and the
// NNUE evaluator both implement evaluate(); either drops into search unchanged.
//
// Convention: evaluate() returns centipawns from the side-to-move's perspective
// (positive = side to move better) — what negamax expects.
//
// Incremental hooks: search calls acc_make/acc_unmake around every make/unmake
// and eval_inc at the leaves. Stateless evaluators (the hand-crafted eval)
// inherit the no-op defaults and just recompute in eval_inc. NNUE overrides
// them to keep an accumulator up to date instead of recomputing from scratch.
//
// No virtual destructor on purpose (avoids __cxa_atexit / operator delete in the
// freestanding -nostdlib wasm build); evaluators are never deleted via base ptr.
#pragma once
#include "../board/position.h"

struct Evaluator {
    virtual int evaluate(const Position& pos) const = 0;

    virtual void acc_refresh(const Position& pos) { (void)pos; }   // set root accumulator
    virtual void acc_make(const Position& after)  { (void)after; } // after pos.make()
    virtual void acc_unmake() {}                                   // after pos.unmake()
    virtual int  eval_inc(const Position& pos)    { return evaluate(pos); } // leaf eval
};
