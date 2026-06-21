// nnue.cpp — HalfKP NNUE inference (float reference + quantized/incremental).
#include "nnue.h"
#if defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define NN_SIMD 1
#endif

namespace NN {

// add/subtract a 256-wide int16 feature column into an accumulator. SIMD on wasm
// (8 lanes/op), scalar elsewhere (native tournament build).
static inline void acc_add(int16_t* a, const int16_t* col) {
#ifdef NN_SIMD
    for (int i = 0; i < ACC; i += 8)
        wasm_v128_store(a + i, wasm_i16x8_add(wasm_v128_load(a + i), wasm_v128_load(col + i)));
#else
    for (int i = 0; i < ACC; ++i) a[i] += col[i];
#endif
}
static inline void acc_sub(int16_t* a, const int16_t* col) {
#ifdef NN_SIMD
    for (int i = 0; i < ACC; i += 8)
        wasm_v128_store(a + i, wasm_i16x8_sub(wasm_v128_load(a + i), wasm_v128_load(col + i)));
#else
    for (int i = 0; i < ACC; ++i) a[i] -= col[i];
#endif
}

// int16·int16 -> int32 dot product. SIMD path uses i32x4_dot_i16x8 (8 MACs/op),
// which covers the dense L1/L2/output layers — the real per-eval hotspot. n must
// be a multiple of 8 (512, 32 here). Exact integer result on both paths.
static inline int dot_i16(const int16_t* a, const int16_t* b, int n) {
#ifdef NN_SIMD
    v128_t s0 = wasm_i32x4_splat(0), s1 = wasm_i32x4_splat(0);
    int i = 0;
    for (; i + 16 <= n; i += 16) {
        s0 = wasm_i32x4_add(s0, wasm_i32x4_dot_i16x8(wasm_v128_load(a + i),     wasm_v128_load(b + i)));
        s1 = wasm_i32x4_add(s1, wasm_i32x4_dot_i16x8(wasm_v128_load(a + i + 8), wasm_v128_load(b + i + 8)));
    }
    for (; i + 8 <= n; i += 8)
        s0 = wasm_i32x4_add(s0, wasm_i32x4_dot_i16x8(wasm_v128_load(a + i), wasm_v128_load(b + i)));
    v128_t s = wasm_i32x4_add(s0, s1);
    return wasm_i32x4_extract_lane(s, 0) + wasm_i32x4_extract_lane(s, 1)
         + wasm_i32x4_extract_lane(s, 2) + wasm_i32x4_extract_lane(s, 3);
#else
    int s = 0;
    for (int i = 0; i < n; ++i) s += (int)a[i] * (int)b[i];
    return s;
#endif
}

// Insertion-sort a small feature list ascending (n <= MAX_ACTIVE). Keeping the
// list sorted lets update() diff old vs new in O(n) via a merge.
static inline void sort_feats(int* f, int n) {
    for (int i = 1; i < n; ++i) {
        int v = f[i], j = i - 1;
        while (j >= 0 && f[j] > v) { f[j + 1] = f[j]; --j; }
        f[j + 1] = v;
    }
}

static inline int orient(Color persp, int sq) {
    return (persp == WHITE) ? sq : (sq ^ 56);
}

int features(const Position& pos, Color persp, int* out) {
    int n = 0;
    int kingSq = pos.king_square(persp);
    int ok = orient(persp, kingSq);
    for (int c = 0; c < COLOR_NB; ++c) {
        int rel = (c == persp) ? 0 : 1;
        for (int pt = PAWN; pt <= QUEEN; ++pt) {
            U64 b = pos.pieces((Color)c, (PieceType)pt);
            while (b) {
                int s = pop_lsb(b);
                int kindIdx = rel * 5 + pt;
                int feat = ok * (10 * 64) + kindIdx * 64 + orient(persp, s);
                out[n++] = feat;
            }
        }
    }
    return n;
}

bool Network::load(const uint8_t* blob, size_t len) {
    loaded = false;
    if (len < 4 + 4 * 4) return false;
    if (blob[0] != 'N' || blob[1] != 'N' || blob[2] != 'U' || blob[3] != '1')
        return false;
    const int32_t* hdr = reinterpret_cast<const int32_t*>(blob + 4);
    if (hdr[0] != NUM_FEATURES || hdr[1] != ACC || hdr[2] != L1 || hdr[3] != L2)
        return false;
    size_t need = 4 + 4 * 4 + sizeof(float) * (
        (size_t)NUM_FEATURES * ACC + ACC +
        (size_t)L1 * (2 * ACC) + L1 +
        (size_t)L2 * L1 + L2 +
        (size_t)L2 + 1);
    if (len < need) return false;
    const float* p = reinterpret_cast<const float*>(blob + 4 + 4 * 4);
    W1 = p; p += (size_t)NUM_FEATURES * ACC;
    b1 = p; p += ACC;
    W2 = p; p += (size_t)L1 * (2 * ACC);
    b2 = p; p += L1;
    W3 = p; p += (size_t)L2 * L1;
    b3 = p; p += L2;
    W4 = p; p += L2;
    b4 = p; p += 1;
    loaded = true;
    return true;
}

static inline float crelu(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

float Network::evaluate_raw(const Position& pos) const {
    if (!loaded) return 0.f;
    int feat[MAX_ACTIVE * 2];
    Color stm = pos.side, opp = ~stm;
    float accStm[ACC], accOpp[ACC];
    for (int i = 0; i < ACC; ++i) { accStm[i] = b1[i]; accOpp[i] = b1[i]; }
    int nStm = features(pos, stm, feat);
    for (int k = 0; k < nStm; ++k) {
        const float* col = W1 + (size_t)feat[k] * ACC;
        for (int i = 0; i < ACC; ++i) accStm[i] += col[i];
    }
    int nOpp = features(pos, opp, feat);
    for (int k = 0; k < nOpp; ++k) {
        const float* col = W1 + (size_t)feat[k] * ACC;
        for (int i = 0; i < ACC; ++i) accOpp[i] += col[i];
    }
    float x[2 * ACC];
    for (int i = 0; i < ACC; ++i) { x[i] = crelu(accStm[i]); x[ACC + i] = crelu(accOpp[i]); }
    float h1[L1];
    for (int o = 0; o < L1; ++o) {
        const float* row = W2 + (size_t)o * (2 * ACC);
        float s = b2[o];
        for (int i = 0; i < 2 * ACC; ++i) s += row[i] * x[i];
        h1[o] = crelu(s);
    }
    float h2[L2];
    for (int o = 0; o < L2; ++o) {
        const float* row = W3 + (size_t)o * L1;
        float s = b3[o];
        for (int i = 0; i < L1; ++i) s += row[i] * h1[i];
        h2[o] = crelu(s);
    }
    float out = b4[0];
    for (int i = 0; i < L2; ++i) out += W4[i] * h2[i];
    return out;
}

int Network::evaluate(const Position& pos) const {
    if (!loaded) return 0;
    float out = evaluate_raw(pos);
    return (int)(out < 0 ? out - 0.5f : out + 0.5f);
}

static int16_t q_ftW[(size_t)NUM_FEATURES * ACC];
static int16_t q_ftB[ACC];
static int16_t q_l1W[L1 * (2 * ACC)];
static int32_t q_l1B[L1];
static int16_t q_l2W[L2 * L1];
static int32_t q_l2B[L2];
static int16_t q_outW[L2];
static int32_t q_outB;

static inline int iround(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int8_t to_i8(float v) { return (int8_t)clampi(iround(v), -127, 127); }

void Network::quantize() {
    if (!loaded) return;
    for (size_t i = 0; i < (size_t)NUM_FEATURES * ACC; ++i)
        q_ftW[i] = (int16_t)iround(W1[i] * QA);
    for (int i = 0; i < ACC; ++i) q_ftB[i] = (int16_t)iround(b1[i] * QA);
    for (int i = 0; i < L1 * (2 * ACC); ++i) q_l1W[i] = (int16_t)iround(W2[i] * QW);
    for (int o = 0; o < L1; ++o) q_l1B[o] = iround(b2[o] * QA * QW);
    for (int i = 0; i < L2 * L1; ++i) q_l2W[i] = (int16_t)iround(W3[i] * QW);
    for (int o = 0; o < L2; ++o) q_l2B[o] = iround(b3[o] * QA * QW);
    for (int i = 0; i < L2; ++i) q_outW[i] = (int16_t)iround(W4[i] * QW);
    q_outB = iround(b4[0] * QA * QW);
    quantized = true;
}

void Network::refresh(Accumulator& a, const Position& pos) const {
    for (int c = 0; c < 2; ++c) {
        a.nf[c] = features(pos, (Color)c, a.feats[c]);
        sort_feats(a.feats[c], a.nf[c]);
        a.king[c] = pos.king_square((Color)c);
        int16_t* acc = a.acc[c];
        for (int i = 0; i < ACC; ++i) acc[i] = q_ftB[i];
        for (int k = 0; k < a.nf[c]; ++k)
            acc_add(acc, q_ftW + (size_t)a.feats[c][k] * ACC);
    }
    a.valid = true;
}

// Truly incremental: for each perspective, if its OWN king moved every HalfKP
// feature index shifts, so we rebuild that side from scratch (same cost as a
// refresh). Otherwise only a handful of features change (moved/captured/promoted
// piece, castled rook) — we apply just the symmetric difference of the sorted
// old/new feature lists, an O(n) merge that touches only the changed columns.
void Network::update(Accumulator& dst, const Accumulator& src, const Position& after) const {
    for (int c = 0; c < 2; ++c) {
        int newFeats[MAX_ACTIVE];
        int nNew = features(after, (Color)c, newFeats);
        sort_feats(newFeats, nNew);
        int newKing = after.king_square((Color)c);

        if (newKing != src.king[c]) {                       // king moved -> full rebuild
            int16_t* acc = dst.acc[c];
            for (int i = 0; i < ACC; ++i) acc[i] = q_ftB[i];
            for (int k = 0; k < nNew; ++k)
                acc_add(acc, q_ftW + (size_t)newFeats[k] * ACC);
        } else {                                            // delta = sorted set-difference
            for (int i = 0; i < ACC; ++i) dst.acc[c][i] = src.acc[c][i];
            const int* O = src.feats[c]; int nO = src.nf[c];
            int ia = 0, ib = 0;
            while (ia < nO && ib < nNew) {
                if (O[ia] == newFeats[ib]) { ++ia; ++ib; }
                else if (O[ia] < newFeats[ib]) { acc_sub(dst.acc[c], q_ftW + (size_t)O[ia] * ACC); ++ia; }
                else { acc_add(dst.acc[c], q_ftW + (size_t)newFeats[ib] * ACC); ++ib; }
            }
            for (; ia < nO;  ++ia) acc_sub(dst.acc[c], q_ftW + (size_t)O[ia] * ACC);
            for (; ib < nNew; ++ib) acc_add(dst.acc[c], q_ftW + (size_t)newFeats[ib] * ACC);
        }
        dst.nf[c] = nNew;
        dst.king[c] = newKing;
        for (int j = 0; j < nNew; ++j) dst.feats[c][j] = newFeats[j];
    }
    dst.valid = true;
}

int Network::eval_acc(const Accumulator& a, Color stm) const {
    Color opp = ~stm;
    // clamped-relu activations as int16 (range 0..QA=127) so the dense layers can
    // run as SIMD int16 dot products.
    int16_t x[2 * ACC];
    for (int i = 0; i < ACC; ++i) {
        x[i]       = (int16_t)clampi(a.acc[stm][i], 0, QA);
        x[ACC + i] = (int16_t)clampi(a.acc[opp][i], 0, QA);
    }
    int16_t h1[L1];
    for (int o = 0; o < L1; ++o) {
        int s = q_l1B[o] + dot_i16(q_l1W + o * (2 * ACC), x, 2 * ACC);
        h1[o] = (int16_t)clampi(s >> QW_SHIFT, 0, QA);
    }
    int16_t h2[L2];
    for (int o = 0; o < L2; ++o) {
        int s = q_l2B[o] + dot_i16(q_l2W + o * L1, h1, L1);
        h2[o] = (int16_t)clampi(s >> QW_SHIFT, 0, QA);
    }
    long s = (long)q_outB + dot_i16(q_outW, h2, L2);
    long denom = (long)QA * QW;
    return (int)(s < 0 ? (s - denom / 2) / denom : (s + denom / 2) / denom);
}

int Network::evaluate_q(const Position& pos) const {
    Accumulator a;
    refresh(a, pos);
    return eval_acc(a, pos.side);
}

// Load a PRE-QUANTIZED net (NNU2): int8 weights / int16 ft-bias / int32 dense
// biases, copied straight into the quantized tables (no float, no quantize step).
bool Network::load_q(const uint8_t* blob, size_t len) {
    loaded = false; quantized = false;
    if (len < 4 + 16) return false;
    if (blob[0]!='N'||blob[1]!='N'||blob[2]!='U'||(blob[3]!='2'&&blob[3]!='3')) return false;
    int ftw = (blob[3]=='3') ? 2 : 1;     // feature-transformer weight bytes
    const int32_t* hdr = reinterpret_cast<const int32_t*>(blob + 4);
    if (hdr[0]!=NUM_FEATURES||hdr[1]!=ACC||hdr[2]!=L1||hdr[3]!=L2) return false;

    // Dense weights (W2/W3/W4) are int16 in the current format, but legacy NNU2
    // files stored them as int8. The header doesn't distinguish the two, so we
    // disambiguate by total length and read the dense layers at the right width.
    const size_t ftBytes = (size_t)NUM_FEATURES*ACC*ftw + (size_t)ACC*2;  // FT W + b1(i16)
    const size_t denseElems = (size_t)L1*(2*ACC) + (size_t)L2*L1 + (size_t)L2; // W2+W3+W4
    const size_t biasBytes  = (size_t)L1*4 + (size_t)L2*4 + 4;            // b2,b3,b4 (i32)
    const size_t need16 = 4 + 16 + ftBytes + denseElems*2 + biasBytes;
    const size_t need8  = 4 + 16 + ftBytes + denseElems*1 + biasBytes;
    int densew;
    if      ((size_t)len == need16) densew = 2;
    else if ((size_t)len == need8)  densew = 1;   // legacy int8 dense
    else if ((size_t)len >= need16) densew = 2;   // tolerate trailing bytes
    else return false;

    const uint8_t* p = blob + 4 + 16;
    if (ftw == 1) {
        const int8_t* W1q = (const int8_t*)p;
        for (size_t i = 0; i < (size_t)NUM_FEATURES*ACC; ++i) q_ftW[i] = (int16_t)W1q[i];
        p += (size_t)NUM_FEATURES*ACC;
    } else {
        const int16_t* W1q = (const int16_t*)p;
        for (size_t i = 0; i < (size_t)NUM_FEATURES*ACC; ++i) q_ftW[i] = W1q[i];
        p += (size_t)NUM_FEATURES*ACC*2;
    }
    const int16_t* b1q=(const int16_t*)p; p += (size_t)ACC*2;
    for (int i=0;i<ACC;++i) q_ftB[i]=b1q[i];

    // read a dense weight block of `count` elements at int8 or int16 width
    auto readW = [&](int16_t* dst, size_t count) {
        if (densew == 2) { const int16_t* s=(const int16_t*)p; for (size_t i=0;i<count;++i) dst[i]=s[i]; p+=count*2; }
        else             { const int8_t*  s=(const int8_t*) p; for (size_t i=0;i<count;++i) dst[i]=(int16_t)s[i]; p+=count; }
    };
    readW(q_l1W, (size_t)L1*(2*ACC));
    { const int32_t* b2q=(const int32_t*)p; for (int o=0;o<L1;++o) q_l1B[o]=b2q[o]; p+=(size_t)L1*4; }
    readW(q_l2W, (size_t)L2*L1);
    { const int32_t* b3q=(const int32_t*)p; for (int o=0;o<L2;++o) q_l2B[o]=b3q[o]; p+=(size_t)L2*4; }
    readW(q_outW, (size_t)L2);
    { const int32_t* b4q=(const int32_t*)p; q_outB=b4q[0]; }
    loaded=true; quantized=true;
    return true;
}

} // namespace NN
