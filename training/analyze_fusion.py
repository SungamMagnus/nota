"""Where do the classical tracker and the network each go wrong, and does combining them beat both?

A frame's pitch is 'wrong peak' when the chosen log-lag bin is more than 2 bins (~150 cents; beyond what the NSDF
refinement window can repair) from the reference. Reported on frames where the reference is voiced.

    .venv/bin/python analyze_fusion.py [checkpoint.pt]
"""
import math
import sys
from pathlib import Path

import numpy as np
import torch

from train import MODELS, N_BINS, UNVOICED, Picker, hz_to_bin, load_split


def run(model, f):
    x = torch.from_numpy(f["feat"].astype(np.float32) / 127.0)[None]
    with torch.no_grad():
        lg, _ = model(x)
    return torch.softmax(lg[0], -1).numpy()


def main():
    ck = torch.load(Path(sys.argv[1]) if len(sys.argv) > 1 else MODELS / "picker.pt", map_location="cpu")
    model = Picker(dim=ck.get("dim", 482), hidden=ck["hidden"])
    model.load_state_dict(ck["model"])
    model.eval()

    groups = {}
    for split in ("val", "test"):
        for f in load_split(split):
            p = run(model, f)
            t = f["target"]
            cf = f["info"][:, 0]
            cb = np.where(cf > 0, np.round(hz_to_bin(cf)), -1).astype(int)
            nb = p[:, :N_BINS].argmax(-1)
            pv = 1.0 - p[:, UNVOICED]
            key = f"{split:<4} {'synth' if f['syn'] else 'real '} {'bass ' if f['bass'] else 'voice'}"
            groups.setdefault(key, []).append((t, cb, f["info"][:, 1], nb, pv, p[:, :N_BINS].max(-1)))

    def cat(key, i):
        return np.concatenate([g[i] for g in groups[key]])

    taus = [0.0, 0.5, 0.7, 0.85, 0.95, 1.01]
    print(f"{'group':<18} {'frames':>8}  {'':<10} " + "  ".join(f"{'wrong-peak':>10}" for _ in [0]) + f" {'voicing recall':>15}")
    for key in sorted(groups):
        t, cb, cc, nb, pv, ptop = (cat(key, i) for i in range(6))
        ref_v = (t >= 0) & (t < N_BINS)
        n = int(ref_v.sum())
        if n == 0:
            continue
        c_v = cb >= 0
        n_v = pv > 0.5

        def wrong(binv, voiced):
            m = ref_v & voiced
            return float(np.mean(np.abs(binv[m] - t[m]) > 2)) if m.any() else float("nan")

        print(f"{key:<18} {n:8d}  classical  {wrong(cb, c_v):10.4f} {float((ref_v & c_v).sum() / n):15.3f}")
        print(f"{'':<18} {'':>8}  network    {wrong(nb, n_v):10.4f} {float((ref_v & n_v).sum() / n):15.3f}")
        # Fusion: use the network's peak only when it is confident (p_top > tau) or the classical tracker found nothing.
        for tau in taus[1:-1]:
            fb = np.where((cb < 0) | (ptop > tau), nb, cb)
            fv = n_v | c_v
            print(f"{'':<18} {'':>8}  fuse t={tau:<4} {wrong(fb, fv):10.4f} {float((ref_v & fv).sum() / n):15.3f}")


if __name__ == "__main__":
    main()
