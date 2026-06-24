#!/usr/bin/env python3
"""train.py - train the HalfKP NNUE.

    python train.py --data train.bin --epochs 8 --batch 16384 --workers 2
    python train.py --data train.bin --limit 2000000 --workers 0   # quick CPU run

RAM notes:
  * The dataset memmaps train.bin and opens it lazily per worker (no copying).
  * --workers controls DataLoader processes; on Windows each worker is a full
    process that imports torch (~0.5GB). Use 0-2 on a low-RAM machine; 0 loads
    in the main process (lowest RAM, no parallel prefetch).
  * --limit N trains on a random N-position subset (good for CPU / a smoke run).

Progress prints every --log-every batches so you can see it's alive.
"""
import argparse, math, time
import numpy as np
import torch
from torch.utils.data import DataLoader, Dataset, Subset
from model import NNUE
from dataset import PackedDataset, collate
import os


class SparseValidationSplit(Dataset):
    """Deterministic holdout without storing one index per training position."""
    def __init__(self, dataset, val_size, train):
        self.dataset = dataset
        self.n = len(dataset)
        self.val_size = val_size
        self.train = train
        self.stride = max(1, self.n // max(1, val_size))

    def __len__(self):
        return self.n - self.val_size if self.train else self.val_size

    def _val_index(self, i):
        return min(i * self.stride, self.n - 1)

    def _reserved_through(self, original_index):
        if original_index < 0:
            return 0
        return min(self.val_size, original_index // self.stride + 1)

    def _train_index(self, i):
        lo, hi = 0, self.n - 1
        while lo < hi:
            mid = (lo + hi) // 2
            non_val = mid + 1 - self._reserved_through(mid)
            if non_val > i:
                hi = mid
            else:
                lo = mid + 1
        return lo

    def _map_index(self, i):
        return self._train_index(i) if self.train else self._val_index(i)

    def __getitem__(self, i):
        return self.dataset[self._map_index(i)]

    def target(self, i):
        return target_at(self.dataset, self._map_index(i))


def target_at(dataset, i):
    if hasattr(dataset, "target"):
        return dataset.target(i)
    if isinstance(dataset, Subset):
        return target_at(dataset.dataset, dataset.indices[i])
    return dataset[i][2]


def target_stats(dataset, samples):
    n = len(dataset)
    count = n if samples == 0 else min(n, samples)
    if count == n:
        indices = range(n)
    else:
        indices = np.linspace(0, n - 1, count, dtype=np.int64)

    mean = 0.0
    m2 = 0.0
    mn = float("inf")
    mx = float("-inf")
    for k, idx in enumerate(indices, 1):
        v = float(target_at(dataset, int(idx)))
        delta = v - mean
        mean += delta / k
        m2 += delta * (v - mean)
        mn = min(mn, v)
        mx = max(mx, v)
    std = math.sqrt(m2 / max(count, 1))
    return mean, std, mn, mx, count


def evaluate(model, loader, loss_fn, device):
    model.eval()
    total_loss, total_seen = 0.0, 0
    with torch.no_grad():
        for si, so, oi, oo, tgt in loader:
            si = si.to(device); so = so.to(device)
            oi = oi.to(device); oo = oo.to(device); tgt = tgt.to(device)
            out = model(si, so, oi, oo)
            loss = loss_fn(out, tgt)
            seen = tgt.numel()
            total_loss += loss.item() * seen
            total_seen += seen
    avg = total_loss / max(total_seen, 1)
    return avg, math.sqrt(avg)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', required=True, help='packed .bin from preprocess.py')
    ap.add_argument('--load', default=None, help='load existing model weights')
    ap.add_argument('--epochs', type=int, default=8)
    ap.add_argument('--batch', type=int, default=16384)
    ap.add_argument('--lr', type=float, default=1e-3)
    ap.add_argument('--workers', type=int, default=2)
    ap.add_argument('--limit', type=int, default=0, help='train on a random N-position subset (0=all)')
    ap.add_argument('--log-every', type=int, default=50, help='print every N batches')
    ap.add_argument('--out-dir', default='checkpoints', help='directory to save checkpoints')
    ap.add_argument('--device', default='cuda' if torch.cuda.is_available() else 'cpu')
    ap.add_argument('--val-size', type=int, default=0,
                    help='validation positions (0=1%% capped at about 1M)')
    ap.add_argument('--stats-samples', type=int, default=1000000,
                    help='target-stat sample count (0=full dataset)')
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    if args.device == 'cpu':
        print("WARNING: training on CPU (no CUDA GPU found). This is slow for big "
              "data - consider --limit and few epochs, or a GPU (e.g. Colab).")

    ds = PackedDataset(args.data)
    if args.limit and args.limit < len(ds):
        idx = np.random.default_rng(0).choice(len(ds), size=args.limit, replace=False)
        ds = Subset(ds, idx.tolist())

    mean, std, mn, mx, stat_count = target_stats(ds, args.stats_samples)
    stat_note = "full" if stat_count == len(ds) else f"{stat_count:,} sample"
    print(f"target cp stats ({stat_note}): mean={mean:.2f}  std={std:.2f}  "
          f"min={mn:.2f}  max={mx:.2f}")

    val_size = args.val_size if args.val_size > 0 else min(max(1, len(ds) // 100), 1_000_000)
    val_size = min(val_size, max(1, len(ds) - 1))
    train_ds = SparseValidationSplit(ds, val_size, train=True)
    val_ds = SparseValidationSplit(ds, val_size, train=False)
    print(f"{len(ds):,} positions  |  train={len(train_ds):,}  val={len(val_ds):,}  "
          f"device={args.device}  workers={args.workers}  batch={args.batch}")

    dl = DataLoader(train_ds, batch_size=args.batch, shuffle=True, collate_fn=collate,
                    num_workers=args.workers, drop_last=True,
                    pin_memory=(args.device == 'cuda'),
                    persistent_workers=(args.workers > 0))
    val_dl = DataLoader(val_ds, batch_size=args.batch, shuffle=False, collate_fn=collate,
                        num_workers=args.workers, drop_last=False,
                        pin_memory=(args.device == 'cuda'),
                        persistent_workers=(args.workers > 0))
    nbatches = len(dl)

    model = NNUE().to(args.device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-5)
    start_epoch = 0

    if args.load:
        ckpt = torch.load(args.load, map_location=args.device)
        model.load_state_dict(ckpt["model"])
        opt.load_state_dict(ckpt["optimizer"])
        start_epoch = ckpt["epoch"]
        print(f"resumed from epoch {ckpt['epoch']}")

    loss_fn = torch.nn.MSELoss()
    best_loss = float("inf")

    for epoch in range(start_epoch, args.epochs):
        model.train()
        run, seen, t0 = 0.0, 0, time.time()
        for b, (si, so, oi, oo, tgt) in enumerate(dl, 1):
            si = si.to(args.device); so = so.to(args.device)
            oi = oi.to(args.device); oo = oo.to(args.device); tgt = tgt.to(args.device)

            out = model(si, so, oi, oo)
            loss = loss_fn(out, tgt)
            opt.zero_grad(); loss.backward(); opt.step()

            batch_seen = tgt.numel()
            run += loss.item() * batch_seen
            seen += batch_seen
            if b % args.log_every == 0 or b == nbatches:
                rate = seen / max(time.time() - t0, 1e-6)
                print(f"epoch {epoch}/{args.epochs-1}  batch {b}/{nbatches}  "
                      f"train loss {run/max(seen, 1):.2f}  {rate:,.0f} pos/s", flush=True)

        epoch_loss = run / max(seen, 1)
        val_loss, val_rmse = evaluate(model, val_dl, loss_fn, args.device)
        print(f"Epoch {epoch}:")
        print(f"  train loss: {epoch_loss:.2f}")
        print(f"  val loss:   {val_loss:.2f}")
        print(f"  val RMSE:   {val_rmse:.1f} cp")
                
        checkpoint = {
            "model": model.state_dict(),
            "optimizer": opt.state_dict(),
            "epoch": epoch+1,
            "train_loss": epoch_loss,
            "val_loss": val_loss,
            "val_rmse": val_rmse,
        }
        path = os.path.join(args.out_dir, f"epoch_{epoch}.pt")
        
        torch.save(checkpoint, path)

        print(f"  epoch {epoch} done in {time.time()-t0:.0f}s  -> saved {os.path.join(args.out_dir, f'epoch_{epoch}.pt')}", flush=True)

        if val_loss < best_loss:
            best_loss = val_loss
            torch.save(checkpoint, os.path.join(args.out_dir, "best.pt"))

    print(f"done -> {args.out_dir}")


if __name__ == '__main__':
    main()
