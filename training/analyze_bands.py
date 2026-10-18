"""Wrong-peak rate of the classical tracker and the network by the reference pitch's frequency band (real audio + synthetic)."""
import sys
from pathlib import Path

import numpy as np
import torch

from analyze_fusion import run
from train import MODELS, N_BINS, UNVOICED, Picker, hz_to_bin, load_split

ck = torch.load(Path(sys.argv[1]) if len(sys.argv) > 1 else MODELS / "picker.pt", map_location="cpu")
model = Picker(dim=ck.get("dim", 482), hidden=ck["hidden"])
model.load_state_dict(ck["model"])
model.eval()

edges = [0, 45, 60, 80, 110, 160, 250, 400, 2000]
acc = {}
for split in ("val", "test"):
    for f in load_split(split):
        p = run(model, f)
        t = f["target"]
        ref_v = (t >= 0) & (t < N_BINS)
        # frequency of the reference bin
        lag = 8.0 * (554.0 / 8.0) ** (np.clip(t, 0, 95) / 95.0)
        hz = 12000.0 / lag
        cf = f["info"][:, 0]
        cb = np.where(cf > 0, np.round(hz_to_bin(cf)), -1).astype(int)
        nb = p[:, :N_BINS].argmax(-1)
        nv = (1 - p[:, UNVOICED]) > 0.5
        src = "synth" if f["syn"] else "real"
        for lo, hi in zip(edges[:-1], edges[1:]):
            m = ref_v & (hz >= lo) & (hz < hi)
            if not m.any():
                continue
            d = acc.setdefault((src, lo, hi), [0, 0, 0, 0, 0])
            d[0] += m.sum()
            mc = m & (cb >= 0)
            d[1] += (np.abs(cb[mc] - t[mc]) > 2).sum()
            d[2] += mc.sum()
            mn = m & nv
            d[3] += (np.abs(nb[mn] - t[mn]) > 2).sum()
            d[4] += mn.sum()

print(f"{'source':<6} {'band Hz':<10} {'frames':>8} {'classical wrong':>16} {'network wrong':>14}")
for (src, lo, hi), d in sorted(acc.items()):
    print(f"{src:<6} {lo:>4}-{hi:<5} {d[0]:8d} {d[1] / max(d[2], 1):16.4f} {d[3] / max(d[4], 1):14.4f}")
