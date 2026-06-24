// nnue.h — HalfKP NNUE evaluator. See nnue.cpp / training/ for the architecture.
#pragma once
#include "../eval/evaluator.h"
#include "../board/position.h"
#include <stdint.h>
#include <stddef.h>

namespace NN {

constexpr int NUM_FEATURES = 64 * 10 * 64;  // 40960
constexpr int ACC          = 256;
constexpr int L1           = 256;
constexpr int L2           = 64;
constexpr int EVAL_SCALE   = 400;
constexpr int MAX_ACTIVE   = 30;

constexpr int QA       = 127;   // activation fixed-point
constexpr int QW       = 64;    // dense-weight fixed-point
constexpr int QW_SHIFT = 6;

struct Accumulator {
    int16_t acc[2][ACC];
    int     feats[2][MAX_ACTIVE];   // kept SORTED ascending per perspective
    int     nf[2];
    int     king[2] = {-1, -1};     // king square per perspective; king move => refresh
    bool    valid = false;
};

int features(const Position& pos, Color persp, int* out);

struct Network {
    const float* W1 = nullptr;
    const float* b1 = nullptr;
    const float* W2 = nullptr;
    const float* b2 = nullptr;
    const float* W3 = nullptr;
    const float* b3 = nullptr;
    const float* W4 = nullptr;
    const float* b4 = nullptr;
    bool loaded = false;

    bool load(const uint8_t* blob, size_t len);
    bool load_q(const uint8_t* blob, size_t len);   // pre-quantized int8 (NNU2)
    int evaluate(const Position& pos) const;
    float evaluate_raw(const Position& pos) const;

    bool quantized = false;
    void quantize();
    void refresh(Accumulator& a, const Position& pos) const;
    void update(Accumulator& dst, const Accumulator& src, const Position& after) const;
    int eval_acc(const Accumulator& a, Color stm) const;
    int evaluate_q(const Position& pos) const;
};

} // namespace NN

// Float evaluator (standalone, non-incremental).
struct NnueEval : Evaluator {
    NN::Network net;
    int evaluate(const Position& pos) const override { return net.evaluate(pos); }
};

// Quantized + INCREMENTAL evaluator — what search uses. Keeps a per-ply
// accumulator stack so each node updates only the changed feature columns
// instead of rebuilding the accumulator from scratch.
struct NnueEvalQ : Evaluator {
    NN::Network net;
    NN::Accumulator stack[MAX_PLY];
    int sp = 0;

    int evaluate(const Position& pos) const override { return net.evaluate_q(pos); }

    void acc_refresh(const Position& pos) override { sp = 0; net.refresh(stack[0], pos); }
    void acc_make(const Position& after) override { net.update(stack[sp + 1], stack[sp], after); ++sp; }
    void acc_unmake() override { --sp; }
    int  eval_inc(const Position& pos) override { return net.eval_acc(stack[sp], pos.side); }
};
