"""halfkp.py — shared HalfKP feature indexing + format constants.

These functions are byte-for-byte consistent with the C++ engine
(engine/nn/nnue.cpp) and the validated reference (ref_gen.py). The trainer and
the exporter both import from here so indexing can never drift from the engine.
"""
NUM_FEATURES = 64 * 10 * 64   # 40960
ACC, L1, L2 = 256, 256, 64
EVAL_SCALE = 400.0            # UI win-probability scale; training targets are cp

PIECE = {'P': (0, 0), 'N': (0, 1), 'B': (0, 2), 'R': (0, 3), 'Q': (0, 4), 'K': (0, 5),
         'p': (1, 0), 'n': (1, 1), 'b': (1, 2), 'r': (1, 3), 'q': (1, 4), 'k': (1, 5)}


def parse_fen(fen):
    """Return (pieces, stm). pieces: list of (color, ptype, square); a1=0..h8=63."""
    parts = fen.split()
    board, stm = parts[0], parts[1]
    pieces = []
    for r, row in enumerate(board.split('/')):   # rank 8 first
        rank = 7 - r
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
    """Active HalfKP feature indices for one perspective."""
    king = next(sq for (c, pt, sq) in pieces if pt == 5 and c == persp)
    ok = orient(persp, king)
    out = []
    for (c, pt, sq) in pieces:
        if pt == 5:
            continue
        rel = 0 if c == persp else 1
        kind = rel * 5 + pt
        out.append(ok * (10 * 64) + kind * 64 + orient(persp, sq))
    return out
