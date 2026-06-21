"""dataset.py - fast training data access over the packed binary produced by
preprocess.py. The memmap is opened LAZILY (per process), so the dataset object
pickled to Windows DataLoader workers carries no data - otherwise np.memmap
pickles by COPYING the whole file into every worker (a multi-GB RAM bomb).

Record layout (see preprocess.py), all fields aligned:
    float32 target ; uint8 stm_count, opp_count ; uint16 stm[30] ; uint16 opp[30]
"""
import numpy as np

try:
    from torch.utils.data import Dataset as _Base
except Exception:                      # allow import without torch (preprocess/tests)
    class _Base:
        pass


def _read_meta(path):
    with open(path + '.meta') as fh:
        n, maxf, rec = map(int, fh.read().split())
    return n, maxf, rec


class PackedDataset(_Base):
    def __init__(self, path):
        self.path = path
        self.n, self.maxf, self.rec = _read_meta(path)
        self.buf = None                # opened lazily in each process
        self._foff = 6                 # features start (after f32 target + 2 counts)

    def _ensure(self):
        if self.buf is None:
            self.buf = np.memmap(self.path, dtype=np.uint8, mode='r',
                                 shape=(self.n, self.rec))

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        self._ensure()
        rec = self.buf[i]
        target = rec[0:4].view(np.float32)[0]
        scnt, ocnt = int(rec[4]), int(rec[5])
        feats = rec[self._foff:].view(np.uint16)
        stm = feats[:scnt].astype(np.int64)
        opp = feats[self.maxf:self.maxf + ocnt].astype(np.int64)
        return stm, opp, np.float32(target)

    # don't pickle the memmap handle to workers
    def __getstate__(self):
        d = self.__dict__.copy()
        d['buf'] = None
        return d


def collate(batch):
    """Pack variable-length feature lists into EmbeddingBag (idx, offsets) form."""
    import torch
    stm_idx, stm_off, opp_idx, opp_off, tgt = [], [], [], [], []
    s_cur = o_cur = 0
    for stm, opp, t in batch:
        stm_off.append(s_cur); opp_off.append(o_cur)
        stm_idx.append(stm); opp_idx.append(opp)
        s_cur += len(stm); o_cur += len(opp)
        tgt.append(t)
    stm_idx = np.concatenate(stm_idx) if stm_idx else np.zeros(0, np.int64)
    opp_idx = np.concatenate(opp_idx) if opp_idx else np.zeros(0, np.int64)
    return (torch.from_numpy(stm_idx), torch.tensor(stm_off, dtype=torch.long),
            torch.from_numpy(opp_idx), torch.tensor(opp_off, dtype=torch.long),
            torch.tensor(tgt, dtype=torch.float32))
