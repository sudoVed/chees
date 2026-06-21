#!/usr/bin/env python3
"""export_weights.py — convert a trained model to the engine's weight file.

    python export_weights.py --model checkpoints/best.pt --out ../dist/net.nnue
    python export_weights.py --model best.pt --out net.nnue --float   # legacy NNU1

Default output is NNU2: pre-quantized int8 weights (~10.5 MB) the engine loads
directly (no float, no on-device quantize) — ideal for static hosting. The
quantization matches engine/nn/nnue.cpp exactly (QA=127, QW=64).
"""
import argparse, struct
import numpy as np
import torch
from model import NNUE
from halfkp import NUM_FEATURES, ACC, L1, L2

QA, QW = 127, 64

def i8(a):  return np.clip(np.round(a), -127, 127).astype(np.int8)
def i16(a): return np.round(a).astype('<i2')
def i32(a): return np.round(a).astype('<i4')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', required=True)
    ap.add_argument('--out', default='net.nnue')
    ap.add_argument('--float', action='store_true', help='write legacy NNU1 float format')
    ap.add_argument('--ft16', action='store_true', help='store feature transformer as int16 (NNU3, ~21MB, more eval range)')
    args = ap.parse_args()

    model = NNUE()
    ckpt = torch.load(args.model, map_location='cpu')
    state = ckpt['model'] if isinstance(ckpt, dict) and 'model' in ckpt else ckpt
    model.load_state_dict(state); model.eval()

    def f(t): return t.detach().cpu().numpy()
    W1 = f(model.transformer.weight)   # [NF, ACC]
    b1 = f(model.b1)                    # [ACC]
    W2 = f(model.l1.weight)            # [L1, 2*ACC]
    b2 = f(model.l1.bias)
    W3 = f(model.l2.weight)           # [L2, L1]
    b3 = f(model.l2.bias)
    W4 = f(model.l3.weight).reshape(-1)  # [L2]
    b4 = f(model.l3.bias)
    assert W1.shape == (NUM_FEATURES, ACC) and W2.shape == (L1, 2*ACC) and W3.shape == (L2, L1)

    with open(args.out, 'wb') as fh:
        if args.float:
            fh.write(b'NNU1'); fh.write(struct.pack('<4i', NUM_FEATURES, ACC, L1, L2))
            for a in (W1, b1, W2, b2, W3, b3, W4, b4):
                fh.write(np.ascontiguousarray(a, dtype='<f4').tobytes())
            kind = 'NNU1 float'
        else:
            fh.write(b'NNU3' if args.ft16 else b'NNU2')
            fh.write(struct.pack('<4i', NUM_FEATURES, ACC, L1, L2))
            if args.ft16:
                fh.write(np.ascontiguousarray(i16(W1 * QA)).tobytes())  # int16 [NF*ACC]
            else:
                fh.write(np.ascontiguousarray(i8(W1 * QA)).tobytes())   # int8  [NF*ACC]
            fh.write(np.ascontiguousarray(i16(b1 * QA)).tobytes())  # int16 [ACC]
            fh.write(np.ascontiguousarray(i16(W2 * QW)).tobytes())  # int16 [L1*2ACC]
            fh.write(np.ascontiguousarray(i32(b2 * QA * QW)).tobytes())
            fh.write(np.ascontiguousarray(i16(W3 * QW)).tobytes())
            fh.write(np.ascontiguousarray(i32(b3 * QA * QW)).tobytes())
            fh.write(np.ascontiguousarray(i16(W4 * QW)).tobytes())
            fh.write(np.ascontiguousarray(i32(b4 * QA * QW)).tobytes())
            kind = 'NNU3 int16-FT' if args.ft16 else 'NNU2 int8'

    import os
    print(f"wrote {args.out}  ({kind}, {os.path.getsize(args.out)/1e6:.1f} MB)")

if __name__ == '__main__':
    main()
