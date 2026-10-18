"""Evaluate hybrid rules offline: voicing from the network; pitch peak from the network only in its reliable range.

  rule(fN_min, fC_min): use the network's peak if it is >= fN_min Hz AND the classical tracker is unvoiced or >= fC_min Hz;
  otherwise the classical peak (or the network's if the classical tracker found nothing).
"""
import itertools
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


def bin_hz(b):
    return 12000.0 / (8.0 * (554.0 / 8.0) ** (np.clip(b, 0, 95) / 95.0))


data = []
for split in ("val", "test"):
    for f in load_split(split):
        p = run(model, f)
        cf = f["info"][:, 0]
        data.append(dict(split=split, syn=f["syn"], bass=f["bass"], t=f["target"], cf=cf,
                         cb=np.where(cf > 0, np.round(hz_to_bin(cf)), -1).astype(int),
                         nb=p[:, :N_BINS].argmax(-1), pv=1 - p[:, UNVOICED]))


def score(rule, sel):
    ref = wrong = vc = 0
    for d in data:
        if not sel(d):
            continue
        t = d["t"]
        ref_v = (t >= 0) & (t < N_BINS)
        nv = d["pv"] > 0.5
        cv = d["cb"] >= 0
        if rule is None:        # classical
            b, v = d["cb"], cv
        elif rule == "net":     # network
            b, v = d["nb"], nv
        else:
            fN, fC = rule
            use_net = (bin_hz(d["nb"]) >= fN) & ((~cv) | (d["cf"] >= fC))
            b = np.where(cv & ~use_net, d["cb"], d["nb"])
            v = nv | cv if False else nv   # voicing from the network
        m = ref_v & v
        ref += ref_v.sum()
        vc += m.sum()
        wrong += (np.abs(b[m] - t[m]) > 2).sum()
    return wrong / max(vc, 1), vc / max(ref, 1)


sets = {
    "real bass  (val+test)": lambda d: (not d["syn"]) and d["bass"],
    "real voice (val+test)": lambda d: (not d["syn"]) and (not d["bass"]),
    "real test bass": lambda d: (not d["syn"]) and d["bass"] and d["split"] == "test",
    "real test voice": lambda d: (not d["syn"]) and (not d["bass"]) and d["split"] == "test",
    "synth (val)": lambda d: d["syn"],
}
rules = [None, "net"] + [(fn, fc) for fn, fc in itertools.product([110, 140, 160, 200, 250], [0, 90, 110, 140])]
names = lambda r: "classical" if r is None else ("network" if r == "net" else f"hybrid N>={r[0]} C>={r[1]}")
print(f"{'rule':<22}" + "".join(f"{k:>26}" for k in sets))
for r in rules:
    row = f"{names(r):<22}"
    for k, sel in sets.items():
        w, rec = score(r, sel)
        row += f"   wrong {w:6.4f} recall {rec:4.2f} "
    print(row)
