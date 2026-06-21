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
import argparse, time
import numpy as np
import torch
from torch.utils.data import DataLoader, Subset
from model import NNUE
from dataset import PackedDataset, collate
import os


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
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    if args.device == 'cpu':
        print("WARNING: training on CPU (no CUDA GPU found). This is slow for big "
              "data - consider --limit and few epochs, or a GPU (e.g. Colab).")

    ds = PackedDataset(args.data)
    if args.limit and args.limit < len(ds):
        idx = np.random.default_rng(0).choice(len(ds), size=args.limit, replace=False)
        ds = Subset(ds, idx.tolist())
    print(f"{len(ds):,} positions  |  device={args.device}  workers={args.workers}  batch={args.batch}")

    dl = DataLoader(ds, batch_size=args.batch, shuffle=True, collate_fn=collate,
                    num_workers=args.workers, drop_last=True,
                    pin_memory=(args.device == 'cuda'),
                    persistent_workers=(args.workers > 0))
    nbatches = len(dl)

    model = NNUE().to(args.device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
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
            loss = loss_fn(model.win_prob(out), tgt)
            opt.zero_grad(); loss.backward(); opt.step()

            run += loss.item(); seen += tgt.numel()
            if b % args.log_every == 0 or b == nbatches:
                rate = seen / max(time.time() - t0, 1e-6)
                print(f"epoch {epoch}/{args.epochs-1}  batch {b}/{nbatches}  "
                      f"loss {run/b:.5f}  {rate:,.0f} pos/s", flush=True)
                
        checkpoint = {
            "model": model.state_dict(),
            "optimizer": opt.state_dict(),
            "epoch": epoch+1,
        }
        path = os.path.join(args.out_dir, f"epoch_{epoch}.pt")
        
        torch.save(checkpoint, path)

        print(f"  epoch {epoch} done in {time.time()-t0:.0f}s  -> saved {os.path.join(args.out_dir, f'epoch_{epoch}.pt')}", flush=True)

        epoch_loss = run / nbatches

        if epoch_loss < best_loss:
            best_loss = epoch_loss
            torch.save(checkpoint, os.path.join(args.out_dir, "best.pt"))

    print(f"done -> {args.out_dir}")


if __name__ == '__main__':
    main()
