#!/usr/bin/env python3
"""preprocess.py — one-time conversion of a labeled position source into a
packed binary the trainer can memory-map (so training is GPU-bound, not stuck
parsing FENs in Python).

Sources:
  * CSV  with header `FEN,Evaluation`            (your train.csv; depth-0)
  * Parquet from mateuszgrzyb/lichess-stockfish-normalized
        columns: fen, depth, cp, mate

For each position we compute the HalfKP features (shared halfkp.py, identical to
the engine) for both perspectives and a side-to-move win-probability target.

Packed record (126 bytes, all fields naturally aligned):
    float32 target            # side-to-move win prob in [0,1]
    uint8   stm_count, opp_count
    uint16  stm_feats[30]      # zero-padded
    uint16  opp_feats[30]
A sidecar `<out>.meta` stores N, MAXF, REC_SIZE for the loader.

Eval convention: the source `cp` is assumed to be from WHITE's point of view
(Lichess convention). Use --cp-pov stm if your source is side-to-move relative.
Sanity check after preprocessing: the start position should map near target 0.5.
"""
import argparse, struct, math, sys
import numpy as np
from halfkp import parse_fen, features

MAXF = 30
REC = 4 + 2 + MAXF * 2 * 2          # 126 bytes
CLAMP = 2000                         # clamp |cp|; mate -> +-CLAMP


def cp_to_target(cp_white, stm):
    cp_white = max(-CLAMP, min(CLAMP, cp_white))
    wp = 1.0 / (1.0 + math.exp(-cp_white / 400.0))
    return wp if stm == 0 else (1.0 - wp)


def make_record(args):
    fen, cp_white = args
    try:
        pieces, stm = parse_fen(fen)
    except Exception:
        return None
    # light legality filter: exactly one king per side, <=32 men
    wk = sum(1 for c, pt, _ in pieces if pt == 5 and c == 0)
    bk = sum(1 for c, pt, _ in pieces if pt == 5 and c == 1)
    if wk != 1 or bk != 1 or len(pieces) > 32:
        return None
    fs = features(pieces, stm)
    fo = features(pieces, stm ^ 1)
    if len(fs) > MAXF or len(fo) > MAXF:
        return None
    target = cp_to_target(cp_white, stm)
    s = np.zeros(MAXF, np.uint16); s[:len(fs)] = fs
    o = np.zeros(MAXF, np.uint16); o[:len(fo)] = fo
    return (struct.pack('<f', target) + struct.pack('<BB', len(fs), len(fo))
            + s.tobytes() + o.tobytes())


def iter_csv(path, pov):
    import csv
    with open(path, newline='') as fh:
        r = csv.reader(fh)
        header = next(r)
        for row in r:
            if len(row) < 2:
                continue
            fen = row[0]
            try:
                cp = float(row[1])
            except ValueError:
                continue
            stm = 0 if fen.split()[1] == 'w' else 1
            cp_white = cp if pov == 'white' else (cp if stm == 0 else -cp)
            yield (fen, cp_white)


def iter_parquet(path, pov):
    import pyarrow.parquet as pq
    pf = pq.ParquetFile(path)
    for batch in pf.iter_batches(batch_size=65536, columns=['fen', 'cp', 'mate']):
        d = batch.to_pydict()
        fens, cps, mates = d['fen'], d['cp'], d['mate']
        for i in range(len(fens)):
            fen = fens[i]
            cp, mate = cps[i], mates[i]
            if cp is not None:
                cp_white = float(cp)
            elif mate is not None:
                cp_white = CLAMP if mate > 0 else -CLAMP
            else:
                continue
            if pov != 'white':
                stm = 0 if fen.split()[1] == 'w' else 1
                if stm == 1:
                    cp_white = -cp_white
            yield (fen, cp_white)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', nargs='+', required=True,
                help='input files (.csv or .parquet)')
    ap.add_argument('--fmt', choices=['csv', 'parquet'], default=None)
    ap.add_argument('--out', required=True, help='output .bin path')
    ap.add_argument('--cp-pov', choices=['white', 'stm'], default='white')
    ap.add_argument('--limit', type=int, default=0, help='max positions (0 = all)')
    ap.add_argument('--workers', type=int, default=4)
    args = ap.parse_args()

    def all_rows(paths, pov):
        for path in paths:
            fmt = args.fmt or ('parquet' if path.endswith('.parquet') else 'csv')
            if fmt == 'parquet':
                yield from iter_parquet(path, pov)
            else:
                yield from iter_csv(path, pov)

    rows = all_rows(args.src, args.cp_pov)

    if args.limit:
        import itertools
        rows = itertools.islice(rows, args.limit)

    from multiprocessing import Pool
    n = 0
    with open(args.out, 'wb') as out, Pool(args.workers) as pool:
        for rec in pool.imap(make_record, rows, chunksize=4096):
            if rec is not None:
                out.write(rec); n += 1
                if n % 1_000_000 == 0:
                    print(f"  {n:,} positions", file=sys.stderr)
    with open(args.out + '.meta', 'w') as m:
        m.write(f"{n} {MAXF} {REC}\n")
    print(f"wrote {n:,} records to {args.out}  (record={REC}B, ~{n*REC/1e9:.2f} GB)")


if __name__ == '__main__':
    main()
