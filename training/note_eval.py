"""End-to-end: does the MIDI note the engine is holding match the reference note, frame by frame?

Reference notes come from the pYIN contour (rounded to semitones, median-filtered; +-40 ms around any note change or
voicing change is ignored because pYIN is blurry there). The engine is causal, so latency also costs recall here - the
same for every mode, so differences between modes are real.

    .venv/bin/python note_eval.py frames_dir [frames_dir ...]
"""
import sys
from pathlib import Path

import numpy as np
from scipy.signal import medfilt

ROOT = Path(__file__).parent / "data"


def reference_notes(npz):
    d = np.load(npz, allow_pickle=True)
    f0, voiced = d["f0"], d["voiced"] & (d["f0"] > 0)
    midi = np.where(voiced, 69 + 12 * np.log2(np.maximum(f0, 1e-6) / 440.0), 0.0)
    note = np.where(voiced, np.round(medfilt(midi, 7)), -1).astype(int)
    changes = np.flatnonzero(np.diff(note) != 0)
    ignore = np.zeros(len(note), bool)
    for c in changes:
        ignore[max(0, c - 5): c + 7] = True
    return note, ignore, float(d["hop_s"]), d["level_db"], bool(d["bass"])


def score(frames_dir):
    tot = {"bass": np.zeros(6), "voice": np.zeros(6)}
    per_file = []
    for npz in sorted((ROOT / "labels").glob("*.npz")):
        csv = ROOT / frames_dir / (npz.stem + ".csv")
        if not csv.exists():
            continue
        ref, ignore, hop, level, bass = reference_notes(npz)
        a = np.loadtxt(csv, delimiter=",", skiprows=1)
        t = np.arange(len(ref)) * hop
        idx = np.clip(np.searchsorted(a[:, 0], t, side="right") - 1, 0, len(a) - 1)
        eng = a[idx, 4].astype(int)

        rv = (ref >= 0) & ~ignore
        ru = (ref < 0) & ~ignore & (level > -50)
        right = rv & (eng == ref)
        active = eng >= 0
        wrong_oct = rv & active & (eng != ref) & ((eng - ref) % 12 == 0)
        wrong_other = rv & active & (eng != ref) & ((eng - ref) % 12 != 0)
        spurious = ru & active
        v = np.array([rv.sum(), right.sum(), wrong_oct.sum(), wrong_other.sum(), ru.sum(), spurious.sum()], float)
        tot["bass" if bass else "voice"] += v
        per_file.append((npz.stem, bass, v))
    return tot, per_file


def main():
    dirs = sys.argv[1:] or ["frames"]
    results = {d: score(d) for d in dirs}
    print(f"{'':<14}{'':<8}" + "".join(f"{d:>26}" for d in dirs))
    print(f"{'':<14}{'':<8}" + "".join(f"{'right  octave other  spur':>26}" for _ in dirs))
    for grp in ("bass", "voice"):
        row = f"{grp:<14}{'all':<8}"
        for d in dirs:
            v = results[d][0][grp]
            row += f"{v[1] / v[0]:>9.3f}{v[2] / v[0]:>8.3f}{v[3] / v[0]:>7.3f}{v[5] / max(v[4], 1):>6.2f}  "
        print(row)
    print("\nper file (right / octave / other):")
    names = [p[0] for p in results[dirs[0]][1]]
    for i, n in enumerate(names):
        row = f"{n[:32]:<34}"
        for d in dirs:
            _, _, v = results[d][1][i]
            row += f"  {v[1] / v[0]:5.2f} {v[2] / v[0]:5.3f} {v[3] / v[0]:5.3f} "
        print(row)


if __name__ == "__main__":
    main()
