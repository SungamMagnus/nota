"""Note counts per recording: pYIN reference vs the engine in each mode (splits and phantom notes show up here)."""
import sys
from pathlib import Path

import numpy as np
from scipy.signal import medfilt

ROOT = Path(__file__).parent / "data"


def ref_count(npz, min_s=0.06):
    d = np.load(npz, allow_pickle=True)
    f0, v = d["f0"], d["voiced"] & (d["f0"] > 0)
    midi = np.where(v, 69 + 12 * np.log2(np.maximum(f0, 1e-6) / 440.0), 0)
    note = np.where(v, np.round(medfilt(midi, 7)), -1).astype(int)
    hop = float(d["hop_s"])
    n, i = 0, 0
    while i < len(note):
        j = i
        while j < len(note) and note[j] == note[i]:
            j += 1
        if note[i] >= 0 and (j - i) * hop >= min_s:
            n += 1
        i = j
    return n, bool(d["bass"])


def eng_count(csv):
    a = np.loadtxt(csv, delimiter=",", skiprows=1)
    note = a[:, 4].astype(int)
    starts = np.flatnonzero((note >= 0) & (np.r_[-1, note[:-1]] != note))
    return len(starts)


dirs = sys.argv[1:] or ["frames", "frames_hy"]
print(f"{'file':<34}{'ref':>6}" + "".join(f"{d:>12}" for d in dirs))
tot = {}
for npz in sorted((ROOT / "labels").glob("*.npz")):
    r, bass = ref_count(npz)
    row = f"{npz.stem[:33]:<34}{r:>6}"
    for d in dirs:
        c = eng_count(ROOT / d / (npz.stem + ".csv"))
        row += f"{c:>12}"
        tot.setdefault((bass, d), 0)
        tot[(bass, d)] += c
    tot.setdefault((bass, "ref"), 0)
    tot[(bass, "ref")] += r
    print(row)
for bass in (True, False):
    print(f"{'TOTAL ' + ('bass' if bass else 'voice'):<34}{tot[(bass, 'ref')]:>6}" + "".join(f"{tot[(bass, d)]:>12}" for d in dirs))
