"""How well does the plug-in's tracker agree with the offline references on the real recordings?

Reads data/labels/*.npz (teacher.py: pYIN) and data/frames/*.csv (nota_cli --frames) and reports, per file:
  ref        share of frames the teacher is confident are a note (the reference)
  voicing recall of the reference-voiced frames, how many the tracker also calls voiced
  RPA            raw pitch accuracy: tracker within 50 cents of the reference (on frames both call voiced)
  octave err     tracker is a whole number of octaves (+-1200 cents) away from the reference
  false          tracker voiced while the reference is clearly silent / unvoiced and loud enough to matter
  lag            time offset (ms) between the two that gives the best agreement: how late the causal tracker is

    .venv/bin/python compare.py [--frames frames|frames_nn] [--no-lag]
"""
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).parent / "data"


def cents(f, ref=55.0):
    return 1200.0 * np.log2(np.maximum(f, 1e-6) / ref)


def load_reference(npz):
    d = np.load(npz, allow_pickle=True)
    f0, voiced, prob, level = d["f0"], d["voiced"], d["prob"], d["level_db"]
    ref_v = voiced & (f0 > 0)          # pYIN's HMM decision: steadier than thresholding its per-frame probability
    clear_unvoiced = ~voiced
    return dict(ref_f=np.where(ref_v, f0, 0.0), agree=ref_v, clear_unvoiced=clear_unvoiced, level=level,
                hop=float(d["hop_s"]), bass=bool(d["bass"]), name=str(d["name"]),
                agree_frac=float(ref_v.mean()), disagree_frac=float(((~ref_v) & (~clear_unvoiced)).mean()))


def load_frames(csv):
    a = np.loadtxt(csv, delimiter=",", skiprows=1)
    return a[:, 0], a[:, 1], a[:, 2], a[:, 3], a[:, 4]


def resample_to(t_src, v_src, t_dst, delay):
    """Value of the (causal) tracker at teacher time t, looking `delay` seconds later in the tracker output."""
    idx = np.searchsorted(t_src, t_dst + delay, side="right") - 1
    idx = np.clip(idx, 0, len(v_src) - 1)
    return v_src[idx]


def score(ref, t_f, f_n, delay):
    n = len(ref["ref_f"])
    t = np.arange(n) * ref["hop"]
    nf = resample_to(t_f, f_n, t, delay)
    ref_v = ref["agree"]
    nv = nf > 0
    ok = ref_v & nv
    d = np.abs(cents(nf[ok]) - cents(ref["ref_f"][ok])) if ok.any() else np.array([])
    rpa = float(np.mean(d < 50)) if len(d) else 0.0
    octerr = float(np.mean(np.abs(d - 1200 * np.round(d / 1200)) < 50) - np.mean(d < 50)) if len(d) else 0.0
    recall = float(ok.sum() / max(ref_v.sum(), 1))
    loud = ref["level"] > -45
    fa = float(np.sum(nv & ref["clear_unvoiced"] & loud) / max(np.sum(ref["clear_unvoiced"] & loud), 1))
    return rpa, octerr, recall, fa


def main(lag_search=True, frames_dir="frames"):
    rows = []
    print(f"{'file':<36} {'ref':>5} {'unsure':>8} {'voicing':>7} {'RPA':>6} {'octave':>6} {'false':>6} {'lag ms':>6}")
    for npz in sorted((ROOT / "labels").glob("*.npz")):
        csv = ROOT / frames_dir / (npz.stem + ".csv")
        if not csv.exists():
            continue
        ref = load_reference(npz)
        t_f, f_n, _, _, _ = load_frames(csv)

        best = None
        lags = np.arange(0, 0.121, 0.004) if lag_search else [0.0]
        for lag in lags:
            r = score(ref, t_f, f_n, lag)
            key = r[2] * r[0]  # recall x RPA
            if best is None or key > best[0]:
                best = (key, lag, r)
        _, lag, (rpa, octerr, recall, fa) = best
        rows.append((ref, rpa, octerr, recall, fa, lag))
        print(f"{npz.stem[:36]:<36} {ref['agree_frac']:>5.2f} {ref['disagree_frac']:>8.2f} {recall:>7.2f} {rpa:>6.2f} {octerr:>6.2f} {fa:>6.2f} {lag * 1000:>6.0f}")

    for label, sel in (("bass", True), ("voice", False)):
        sub = [r for r in rows if r[0]["bass"] == sel]
        if not sub:
            continue
        w = np.array([r[0]["agree_frac"] * len(r[0]["ref_f"]) for r in sub])
        m = lambda k: float(np.average([r[k] for r in sub], weights=w))
        print(f"\n{label:<6} (weighted): voicing recall {m(3):.2f}  RPA {m(1):.2f}  octave errors {m(2):.2f}  false-alarm {m(4):.2f}  median lag {np.median([r[5] for r in sub]) * 1000:.0f} ms")


if __name__ == "__main__":
    fd = sys.argv[sys.argv.index("--frames") + 1] if "--frames" in sys.argv else "frames"
    main(lag_search="--no-lag" not in sys.argv, frames_dir=fd)
