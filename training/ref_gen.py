#!/usr/bin/env python3
"""ref_gen.py — generate random NNUE weights in the locked binary format AND
compute the reference forward pass in numpy. The native C++ inference is checked
against this output (scripts/nnue_test.cpp) so feature indexing + layer math
can't drift between the trainer and the engine.

Usage:  python3 ref_gen.py /tmp/net.bin            # writes weights + prints evals
The same FEN list is hard-coded in scripts/nnue_test.cpp.
"""
import sys, struct
import numpy as np

NUM_FEATURES = 64 * 10 * 64   # 40960
ACC, L1, L2 = 256, 32, 32

FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",            # startpos
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 b kq - 0 1", # italian, black to move
    "r2q1rk1/pp1nbppp/2p1pn2/3p4/2PP1B2/2N1PN2/PP3PPP/R2Q1RK1 w - - 0 1",  # QGD middlegame
    "8/8/4k3/8/8/3K4/4P3/8 w - - 0 1",                                      # K+P endgame
    "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",            # white up a queen
]

PIECE = {'P':(0,0),'N':(0,1),'B':(0,2),'R':(0,3),'Q':(0,4),'K':(0,5),
         'p':(1,0),'n':(1,1),'b':(1,2),'r':(1,3),'q':(1,4),'k':(1,5)}

def parse_fen(fen):
    """Return (pieces, stm). pieces: list of (color, ptype, square). a1=0..h8=63."""
    board, stm = fen.split()[0], fen.split()[1]
    pieces, sq = [], 0
    ranks = board.split('/')           # rank 8 first
    for r, row in enumerate(ranks):
        rank = 7 - r                   # rank 8 -> index 7
        f = 0
        for ch in row:
            if ch.isdigit():
                f += int(ch)
            else:
                color, pt = PIECE[ch]
                pieces.append((color, pt, rank * 8 + f))
                f += 1
    return pieces, (0 if stm == 'w' else 1)

def orient(persp, s):
    return s if persp == 0 else (s ^ 56)

def features(pieces, persp):
    king = next(sq for (c, pt, sq) in pieces if pt == 5 and c == persp)
    ok = orient(persp, king)
    feats = []
    for (c, pt, sq) in pieces:
        if pt == 5:                    # king excluded
            continue
        rel = 0 if c == persp else 1
        kind = rel * 5 + pt
        feats.append(ok * (10 * 64) + kind * 64 + orient(persp, sq))
    return feats

def crelu(v):
    return np.clip(v, 0.0, 1.0)

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/net.bin"
    rng = np.random.default_rng(1234)
    W1 = (rng.standard_normal((NUM_FEATURES, ACC)) * 0.01).astype(np.float32)
    b1 = (rng.standard_normal(ACC) * 0.01).astype(np.float32)
    W2 = (rng.standard_normal((L1, 2 * ACC)) * 0.05).astype(np.float32)
    b2 = (rng.standard_normal(L1) * 0.05).astype(np.float32)
    W3 = (rng.standard_normal((L2, L1)) * 0.05).astype(np.float32)
    b3 = (rng.standard_normal(L2) * 0.05).astype(np.float32)
    W4 = (rng.standard_normal(L2) * 0.5).astype(np.float32)
    b4 = (rng.standard_normal(1) * 0.5).astype(np.float32)

    with open(out_path, "wb") as fh:
        fh.write(b"NNU1")
        fh.write(struct.pack("<4i", NUM_FEATURES, ACC, L1, L2))
        for arr in (W1, b1, W2, b2, W3, b3, W4, b4):
            fh.write(np.ascontiguousarray(arr, dtype="<f4").tobytes())

    for fen in FENS:
        pieces, stm = parse_fen(fen)
        opp = stm ^ 1
        acc_s = b1 + W1[features(pieces, stm)].sum(axis=0)
        acc_o = b1 + W1[features(pieces, opp)].sum(axis=0)
        x = np.concatenate([crelu(acc_s), crelu(acc_o)])
        h1 = crelu(W2 @ x + b2)
        h2 = crelu(W3 @ h1 + b3)
        out = float(W4 @ h2 + b4[0])
        print(f"{out:+.5f}\t{round(out)}\t{fen}")

if __name__ == "__main__":
    main()
