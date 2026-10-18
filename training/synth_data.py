"""Synthetic bass and voice with exact ground-truth pitch, for the cases real recordings under-represent.

The failure that motivated this: a bass with a weak fundamental and a bright 2nd harmonic. A network trained only on
11 minutes of real bass picked the harmonic. Here the fundamental can be weak (down to ~-30 dB), the 2nd or 3rd
harmonic can dominate, notes can pluck, bow or sustain, glide, vibrato and be cut by consonant gaps; the label is the
pitch that was *played*, whatever the spectrum looks like.

    .venv/bin/python synth_data.py [--chunks 120] [--aug 3] [--workers 8]

Writes data/features/{train,val}/syn_*.npz in the same format as build_dataset.py.
"""
import os

for var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ.setdefault(var, "1")

import subprocess
import sys
import tempfile
from multiprocessing import Pool
from pathlib import Path

import numpy as np
import soundfile as sf

from build_dataset import FEATURE_BIN, HOP_S, MAX_LAG, MIN_LAG, N_BINS, OUT, RATE, augment, hz_to_bin, read_features

CHUNK_S = 30.0


def midi_hz(m):
    return 440.0 * 2.0 ** ((np.asarray(m) - 69.0) / 12.0)


def make_notes(rng, kind, dur):
    lo, hi = (23, 67) if kind == "bass" else (43, 84)
    m = int(rng.integers(lo + 5, hi - 5))
    steps = np.array([-12, -7, -5, -3, -2, -1, 0, 0, 1, 2, 3, 5, 7, 12])
    probs = np.array([1, 1, 2, 3, 4, 4, 5, 5, 4, 4, 3, 2, 1, 1], float)
    probs /= probs.sum()
    notes, t = [], float(rng.uniform(0.05, 0.4))
    while t < dur - 0.4:
        legato = bool(notes) and rng.random() < (0.3 if kind == "bass" else 0.35)
        m = int(np.clip(m + rng.choice(steps, p=probs), lo, hi))
        nd = float(rng.uniform(0.1, 1.3) if kind == "bass" else rng.uniform(0.15, 1.6))
        t1 = min(dur - 0.1, t + nd)
        notes.append(dict(t0=t, t1=t1, midi=float(m), legato=legato,
                          gap=(float(rng.uniform(0.04, 0.12)) if kind == "voice" and rng.random() < 0.2 and nd > 0.5 else 0.0)))
        t = t1 if rng.random() < 0.3 else t1 + float(rng.uniform(0.02, 0.4))
        if t < t1 + 0.001:  # next note continues straight on: legato
            pass
        notes[-1]["continues"] = abs(t - t1) < 1e-9
        if notes[-1]["continues"]:
            pass
    for a, b in zip(notes[:-1], notes[1:]):
        b["legato"] = bool(a["continues"]) and rng.random() < 0.8
    return notes


def synth(rng, kind, sr, dur):
    """Returns (audio, f0 per sample (0 = no pitch), ignore mask per sample)."""
    n = int(dur * sr)
    notes = make_notes(rng, kind, dur)
    t_all = np.arange(n) / sr

    # ---- instrument for this chunk
    if kind == "bass":
        alpha = rng.uniform(0.3, 1.8)
        hc = rng.uniform(6, 40)
        g1 = 1.0 if rng.random() < 0.55 else float(rng.uniform(0.03, 0.6))  # weak fundamental
        b2 = float(rng.uniform(1, 4)) if rng.random() < 0.4 else 1.0         # bright 2nd harmonic
        b3 = float(rng.uniform(1, 3)) if rng.random() < 0.2 else 1.0
        style = rng.choice(["pluck", "pluck", "bowed", "sustain"])
        tau1 = rng.uniform(0.25, 2.5)
        beta = rng.uniform(0.2, 0.9)
        inh = rng.uniform(0, 3e-5)
        vib_depth, vib_rate = (rng.uniform(0, 15) if rng.random() < 0.3 else 0.0), rng.uniform(4, 6.5)
    else:
        alpha = rng.uniform(0.8, 1.7)
        hc = rng.uniform(25, 60)
        g1 = 1.0 if rng.random() < 0.8 else float(rng.uniform(0.1, 0.6))
        b2 = b3 = 1.0
        style = "voice"
        tau1 = beta = inh = 0.0
        vib_depth, vib_rate = (rng.uniform(15, 70) if rng.random() < 0.7 else rng.uniform(0, 12)), rng.uniform(4.5, 7)
        formants = np.array([rng.uniform(250, 850), rng.uniform(800, 2500), rng.uniform(2200, 3200)])
        breath = rng.uniform(0, 0.25) if rng.random() < 0.6 else 0.0
    glide_tau = rng.uniform(0.004, 0.09)

    midi = np.full(n, np.nan)
    active = np.zeros(n, bool)
    tsince = np.zeros(n)
    env = np.zeros(n)
    ignore = np.zeros(n, bool)
    voiced = np.zeros(n, bool)
    prev_end_midi = None

    for nt in notes:
        i0, i1 = int(nt["t0"] * sr), int(nt["t1"] * sr)
        if i1 <= i0 + 4:
            continue
        tt = np.arange(i1 - i0) / sr
        m = np.full(i1 - i0, nt["midi"])
        if nt["legato"] and prev_end_midi is not None:
            m = m + (prev_end_midi - nt["midi"]) * np.exp(-tt / glide_tau)
        elif kind == "voice" and rng.random() < 0.4:  # scoop into the pitch
            m = m - rng.uniform(0, 1.5) * np.exp(-tt / 0.04)
        delay = rng.uniform(0.1, 0.4)
        vib = vib_depth / 100.0 * np.clip((tt - delay) / 0.2, 0, 1) * np.sin(2 * np.pi * vib_rate * tt)
        m = m + vib
        midi[i0:i1] = m
        active[i0:i1] = True
        prev_end_midi = float(m[-1])

        if style == "pluck":
            if not nt["legato"] or True:
                tsince[i0:i1] = tt if not nt["legato"] else tsince[max(i0 - 1, 0)] + tt
            att = np.clip(tt / 0.003, 0, 1)
            rel = np.clip((nt["t1"] - nt["t0"] - tt) / 0.03, 0, 1) if not nt["continues"] else 1.0
            env[i0:i1] = att * rel
        elif style == "bowed":
            att = np.clip(tt / rng.uniform(0.03, 0.12), 0, 1) if not nt["legato"] else 1.0
            rel = np.clip((nt["t1"] - nt["t0"] - tt) / 0.06, 0, 1) if not nt["continues"] else 1.0
            env[i0:i1] = att * rel
            tsince[i0:i1] = 0
        elif style == "sustain":
            att = np.clip(tt / 0.01, 0, 1) if not nt["legato"] else 1.0
            rel = np.clip((nt["t1"] - nt["t0"] - tt) / 0.04, 0, 1) if not nt["continues"] else 1.0
            env[i0:i1] = att * rel
            tsince[i0:i1] = 0
        else:  # voice
            att = np.clip(tt / rng.uniform(0.01, 0.04), 0, 1) if not nt["legato"] else 1.0
            rel = np.clip((nt["t1"] - nt["t0"] - tt) / rng.uniform(0.03, 0.08), 0, 1) if not nt["continues"] else 1.0
            env[i0:i1] = att * rel
        voiced[i0:i1] = True

        # Consonant gap: the voice is cut for a moment (the label says unvoiced there; hiss is added below).
        if nt["gap"] > 0:
            g0 = i0 + int(rng.uniform(0.25, 0.5) * (i1 - i0))
            g1i = min(i1, g0 + int(nt["gap"] * sr))
            env[g0:g1i] = 0.0
            voiced[g0:g1i] = False
            ignore[max(0, g0 - int(0.02 * sr)): g1i + int(0.02 * sr)] = True

        # Ignore the first 30 ms of a fresh note (nothing can be known yet), and the span of a legato jump.
        if not nt["legato"]:
            ignore[i0: i0 + int(0.03 * sr)] = True
        else:
            ignore[max(0, i0 - int(0.015 * sr)): i0 + int(0.03 * sr)] = True

    # ---- phase and harmonics
    midi = np.where(active, midi, 0.0)
    f = midi_hz(np.where(active, midi, 60.0))
    phase = 2 * np.pi * np.cumsum(f) / sr
    fmax = float(f[active].max()) if active.any() else 100.0
    hmax = int(min(48, 5200.0 / max(f[active].min() if active.any() else 100.0, 1.0)))
    out = np.zeros(n)
    for h in range(1, hmax + 1):
        a = h ** (-alpha) * np.exp(-h / hc)
        if h == 1:
            a *= g1
        elif h == 2:
            a *= b2
        elif h == 3:
            a *= b3
        if kind == "voice":
            fh = h * f
            gain = 0.15 + sum(w / (1.0 + ((fh - F) / bw) ** 2) for w, F, bw in zip((1.0, 0.7, 0.4), formants, (90, 120, 180)))
            amp = a * gain
        else:
            amp = a * np.ones(n)
        if style == "pluck":
            amp = amp * np.exp(-tsince / (tau1 / (h ** beta)))
        hf = h * f * np.sqrt(1.0 + inh * h * h) if style == "pluck" else h * f
        amp = amp * (hf < 5200.0)
        out += amp * env * np.sin(h * phase * (hf / np.maximum(h * f, 1e-6)) + 0.7 * h)
    out *= 0.5 / (np.max(np.abs(out)) + 1e-9)

    if kind == "voice":
        out += breath * rng.standard_normal(n) * 0.05 * env
        # fricative hiss in the gaps
        gaps = active & ~voiced
        if gaps.any():
            out += gaps * rng.standard_normal(n) * 0.15
    else:
        out += rng.standard_normal(n) * 10 ** (rng.uniform(-75, -55) / 20)  # string/fret noise floor

    f0 = np.where(active & voiced, f, 0.0)
    return out.astype(np.float32), f0, ignore


def synth_labels(f0, ignore, sr, n_hops, r, level_db):
    t = (np.arange(n_hops) + 1) * HOP_S * r
    idx = np.clip(np.round(t * sr).astype(int), 0, len(f0) - 1)
    f = f0[idx] * r
    voiced = f > 0
    in_range = (f > 1.05 * RATE / MAX_LAG) & (f < RATE / MIN_LAG * 0.97)
    target = np.full(n_hops, -1, np.int16)
    b = np.round(hz_to_bin(np.where(in_range, f, 100.0))).astype(int)
    target[voiced & in_range] = np.clip(b[voiced & in_range], 0, N_BINS - 1)
    target[~voiced] = N_BINS
    target[ignore[idx]] = -1
    target[level_db < -62.0] = N_BINS
    f_out = np.where((target >= 0) & (target < N_BINS), f, 0.0).astype(np.float32)
    return f_out, target


def job(args):
    idx, kind, n_aug, split, seed = args
    rng = np.random.default_rng(seed)
    sr = int(rng.choice([44100, 48000]))
    audio, f0, ignore = synth(rng, kind, sr, CHUNK_S)
    written = 0
    with tempfile.TemporaryDirectory() as tmp:
        for v in ([0] if split == "val" else range(n_aug + 1)):
            seg, r = (audio, 1.0) if v == 0 else augment(audio, sr, rng)
            wav, binp = Path(tmp) / "c.wav", Path(tmp) / "c.bin"
            sf.write(wav, seg, sr, subtype="FLOAT")
            subprocess.run([str(FEATURE_BIN), str(wav), str(binp)], check=True, capture_output=True)
            feat, info = read_features(binp)
            f_ref, target = synth_labels(f0, ignore, sr, len(feat), r, info[:, 2])
            dest = OUT / split
            dest.mkdir(parents=True, exist_ok=True)
            np.savez(dest / f"syn_{kind}_{idx:03d}_a{v}.npz", feat=feat, target=target, f0=f_ref, info=info, bass=kind == "bass")
            written += 1
    return f"syn_{kind}_{idx:03d} ({split}) -> {written} files"


def main():
    chunks = int(sys.argv[sys.argv.index("--chunks") + 1]) if "--chunks" in sys.argv else 120
    n_aug = int(sys.argv[sys.argv.index("--aug") + 1]) if "--aug" in sys.argv else 3
    workers = int(sys.argv[sys.argv.index("--workers") + 1]) if "--workers" in sys.argv else 8
    jobs = []
    for i in range(chunks):
        kind = "bass" if i % 2 == 0 else "voice"
        split = "val" if i % 12 in (4, 5) else "train"
        jobs.append((i, kind, n_aug, split, 50_000 + i))
    with Pool(workers) as pool:
        for line in pool.imap_unordered(job, jobs):
            print(line, flush=True)


if __name__ == "__main__":
    main()
