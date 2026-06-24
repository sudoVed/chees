#!/usr/bin/env python3
"""load.py - download ONE shard of the HF dataset directly to a local folder.

IMPORTANT: do NOT use datasets.load_dataset for this repo. `split="train[:10%]"`
still downloads ALL 10 shards (~6.5GB) and then re-materializes them into a
second Arrow copy before slicing - which can fill your system drive. Each shard
here is already ~31.6M positions = the 10% slice, so we just grab one file.

    python load.py                              # -> data/train-00000.parquet
    python load.py --shard train-00003.parquet  # a different shard
    python load.py --out-dir D:/chessdata       # choose where it lands

    python load.py --shard train-00000.parquet `
    --shard train-00001.parquet  `
    --shard train-00002.parquet  `
    --shard train-00003.parquet  `
    --shard train-00004.parquet  `
    --shard train-00005.parquet  `
    --shard train-00006.parquet  `
    --shard train-00007.parquet  `
    --shard train-00008.parquet  `
    --shard train-00009.parquet  `
    --out-dir E:/chessdata

Everything (download + any HF cache) stays under --out-dir, so nothing touches
C:. Next step is printed at the end.
"""
import argparse, os

REPO = "mateuszgrzyb/lichess-stockfish-normalized"

shards = [f"train-{i:05d}.parquet" for i in range(10)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--shard', default='train-00000.parquet',
                    help='which shard to download (each ~31.6M positions)')
    ap.add_argument('--out-dir', default='data',
                    help='download folder - keep this OFF your system (C:) drive')
    args = ap.parse_args()

    out = os.path.abspath(args.out_dir)
    os.makedirs(out, exist_ok=True)
    # pin ALL huggingface caching under out-dir BEFORE importing the library,
    # so nothing is written to the default C:\Users\<you>\.cache location.
    os.environ['HF_HOME'] = os.path.join(out, '.hfcache')

    from huggingface_hub import hf_hub_download
    for shard in shards:
        print(f"downloading {shard} from {REPO}\n  -> {out}")
        path = hf_hub_download(repo_id=REPO, filename=shard,
                               repo_type='dataset', local_dir=out)
        print(f"saved {path}  ({os.path.getsize(path)/1e6:.0f} MB)")
    print("\nnext:\n  python preprocess.py --src", path, "--out train.bin --workers 8")


if __name__ == '__main__':
    main()
