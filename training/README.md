# NNUE training (HalfKP)

Offline trainer for the engine's neural evaluation. Architecture + feature
indexing here are byte-for-byte identical to the C++ inference in
`engine/nn/nnue.cpp` (verified by `scripts/nnue_test.cpp` vs `ref_gen.py`, and
the data pipeline verified by the start position mapping to target 0.5).

## Architecture (locked)

HalfKP features (40960 / perspective) -> accumulator 256 / side ->
concat(stm, opp) = 512 -> 32 -> 32 -> 1, clipped-ReLU. Output is side-to-move
centipawns; trained as `sigmoid(out/400)` against the teacher eval.

## Data: HF lichess-stockfish-normalized (10% slice ~= 32M)

One of the dataset's 10 parts is ~31.6M deduplicated positions (max-depth
Stockfish evals) = the recommended slice. Eval-only training (no game result).

```bash
pip install datasets pyarrow torch numpy

# 1. download ONE part (~650MB parquet, ~32M positions)
huggingface-cli download mateuszgrzyb/lichess-stockfish-normalized \
    --repo-type dataset --include "*part*00*.parquet" --local-dir data/

# 2. one-time preprocess -> packed binary (memmapped at train time; multi-core)
python3 preprocess.py --src data/<part>.parquet --out train.bin --workers 8
#    sanity: the start position must map to target ~0.5 (orientation check)

# 3. train (GPU strongly recommended)
python3 train.py --data train.bin --epochs 8 --batch 16384 --out net.pt

# 4. export to the engine's NNU1 binary
python3 export_weights.py --model net.pt --out ../dist/net.nnue

# 5. re-run parity vs the C++ inference before shipping
```

### Eval convention
Source `cp` is treated as WHITE point-of-view (Lichess). `mate` -> +/-2000cp,
`cp` clamped to +/-2000. If a source is side-to-move relative, pass
`--cp-pov stm` to preprocess. The start-position target (~0.5) is the check.

## Files
- `halfkp.py` - shared feature indexing (single source of truth).
- `model.py` - PyTorch network (EmbeddingBag = the accumulator).
- `preprocess.py` - source (csv/parquet) -> packed binary (+ `.meta`).
- `dataset.py` - memmapped `PackedDataset` + EmbeddingBag collate.
- `train.py` - training loop (win-prob MSE).
- `export_weights.py` - writes the engine `NNU1` weights.
- `ref_gen.py` + `../scripts/nnue_test.cpp` - numerical parity harness.

## Engine-side TODO (next)
- int16 **incremental** accumulator in `nnue.cpp` (subtract/add only moved
  pieces; full refresh on king move) + int8 quantization + WASM SIMD.
- load `net.nnue` into wasm memory; wire the evaluator into Phase 9 search.
