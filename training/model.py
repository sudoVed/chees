"""model.py — HalfKP NNUE network (PyTorch). Architecture matches the C++
inference in engine/nn/nnue.cpp exactly:

    accumulator[persp] = EmbeddingBag(sum) over active features + b1   (ACC=256)
    x   = concat( crelu(acc[stm]), crelu(acc[opp]) )                   (512)
    h1  = crelu( Linear(512->256)(x) )
    h2  = crelu( Linear(256->64)(h1) )
    out = Linear(64->1)(h2)                       # centipawns, mover-relative

Training targets are centipawns, and the raw network output is regressed
directly with MSELoss(out, target_cp). Convert to win probability only outside
training, for UI display or reporting.

The feature transformer is an EmbeddingBag in sum mode — that IS the NNUE
accumulator, and its weight matrix [NUM_FEATURES, ACC] exports directly into the
engine's W1 (row per feature).
"""
import torch
import torch.nn as nn
from halfkp import NUM_FEATURES, ACC, L1, L2


def crelu(x):
    return torch.clamp(x, 0.0, 1.0)


class NNUE(nn.Module):
    def __init__(self):
        super().__init__()
        self.transformer = nn.EmbeddingBag(NUM_FEATURES, ACC, mode='sum')
        self.b1 = nn.Parameter(torch.zeros(ACC))
        self.l1 = nn.Linear(2 * ACC, L1)
        self.l2 = nn.Linear(L1, L2)
        self.l3 = nn.Linear(L2, 1)
        nn.init.normal_(self.transformer.weight, std=0.01)
        nn.init.zeros_(self.l3.weight)
        nn.init.zeros_(self.l3.bias)

    def accumulator(self, idx, offsets):
        # idx: 1D LongTensor of feature ids; offsets: per-sample start indices
        return self.transformer(idx, offsets) + self.b1

    def forward(self, stm_idx, stm_off, opp_idx, opp_off):
        acc_s = crelu(self.accumulator(stm_idx, stm_off))
        acc_o = crelu(self.accumulator(opp_idx, opp_off))
        x = torch.cat([acc_s, acc_o], dim=1)
        h1 = crelu(self.l1(x))
        h2 = crelu(self.l2(h1))
        return self.l3(h2).squeeze(1)          # raw centipawn output
