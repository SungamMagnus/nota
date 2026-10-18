"""Build the training set: features from the plug-in's own C++ front-end + labels from the pYIN teacher.

Each recording is cut into chunks; every chunk is run through nota_features once as-is and several times
augmented (gain, hiss, hum, EQ tilt, distortion, room reverb, pitch shift by resampling). Labels follow the
audio: a pitch shift by r scales every f0 by r and the time axis by 1/r.

    .venv/bin/python build_dataset.py /path/to/recordings [--aug 6] [--workers 8]

Output: data/features/<split>/<stem>_c<chunk>_a<aug>.npz with
    feat   int8  [hops, dim]    NSDF on a log-lag grid for four windows + level (x127)
    target int16 [hops]         log-lag bin of the reference pitch (0..95), 96 = unvoiced, -1 = ignore
    f0     float32 [hops]       reference f0 in Hz (0 = unvoiced)
    info   float32 [hops, 5]    the classical tracker's own output (freq, clarity, level_db, brightness, note)
"""
import os

for var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ.setdefault(var, "1")

import subprocess
import sys
import tempfile
from fractions import Fraction
from multiprocessing import Pool
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

from audio_io import load_audio, to_mono

HERE = Path(__file__).parent
FEATURE_BIN = HERE.parent / "build" / "nota_features"
LABELS = HERE / "data" / "labels"
OUT = HERE / "data" / "features"

N_BINS = 96
MIN_LAG, MAX_LAG = 8.0, 554.0
RATE = 12000.0
HOP_S = 32 / RATE
CHUNK_S = 30.0

# Files never trained on (reported separately): one bass, one voice. Everything else is train, with the last
# chunk in seven (spread over every file) held out for validation.
TEST_STEMS = ("33-Electric_Bass_Riffs_3-_1-8", "Audio_8-15")
VAL_FRACTION = 0.15


def hz_to_bin(f0):
    lag = RATE / np.maximum(f0, 1e-6)
    b = np.log(lag / MIN_LAG) / np.log(MAX_LAG / MIN_LAG) * (N_BINS - 1)
    return b


# ---------------------------------------------------------------- augmentation
def biquad_shelf(rng, sr):
    """Random gentle low- or high-shelf (RBJ), up to +-9 dB."""
    gain_db = rng.uniform(-9, 9)
    f0 = rng.choice([150.0, 400.0, 1500.0])
    high = rng.random() < 0.5
    A = 10 ** (gain_db / 40)
    w0 = 2 * np.pi * f0 / sr
    cw, sw = np.cos(w0), np.sin(w0)
    alpha = sw / 2 * np.sqrt(2)
    sA = 2 * np.sqrt(A) * alpha
    if high:
        b0, b1, b2 = A * ((A + 1) + (A - 1) * cw + sA), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sA)
        a0, a1, a2 = (A + 1) - (A - 1) * cw + sA, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sA
    else:
        b0, b1, b2 = A * ((A + 1) - (A - 1) * cw + sA), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sA)
        a0, a1, a2 = (A + 1) + (A - 1) * cw + sA, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sA
    return np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])


def augment(x, sr, rng):
    """Returns (augmented audio, pitch factor r, time factor 1/r). Variant 0 is handled by the caller (clean)."""
    r = 1.0
    if rng.random() < 0.7:
        r = float(2 ** (rng.uniform(-7, 7) / 12))
        frac = Fraction(1.0 / r).limit_denominator(48)
        x = signal.resample_poly(x, frac.numerator, frac.denominator).astype(np.float32)
        r = 1.0 / float(frac)  # the factor actually applied

    if rng.random() < 0.5:
        x = signal.sosfilt(biquad_shelf(rng, sr), x).astype(np.float32)

    if rng.random() < 0.3:  # room: exponentially decaying noise, RT60 0.15 .. 0.9 s, 10-35% wet
        rt60 = rng.uniform(0.15, 0.9)
        n = int(rt60 * sr)
        ir = rng.standard_normal(n) * np.exp(-6.9 * np.arange(n) / n)
        ir[0] = 0
        wet = signal.fftconvolve(x, ir / np.sqrt(np.sum(ir ** 2)) * 0.5, mode="full")[: len(x)]
        x = ((1 - rng.uniform(0.1, 0.35)) * x + rng.uniform(0.1, 0.35) * wet).astype(np.float32)

    if rng.random() < 0.3:  # overdrive
        peak = np.max(np.abs(x)) + 1e-9
        drive = 10 ** (rng.uniform(3, 18) / 20)
        x = (np.tanh(drive * x / peak) / np.tanh(drive) * peak).astype(np.float32)

    gain = 10 ** (rng.uniform(-24, 0) / 20) / (np.max(np.abs(x)) + 1e-9)
    x = x * gain

    if rng.random() < 0.5:  # hiss
        x = x + rng.standard_normal(len(x)).astype(np.float32) * 10 ** (rng.uniform(-80, -52) / 20)
    if rng.random() < 0.3:  # mains hum
        t = np.arange(len(x)) / sr
        hz = rng.choice([50.0, 60.0])
        hum = np.sin(2 * np.pi * hz * t) + 0.5 * np.sin(4 * np.pi * hz * t) + 0.25 * np.sin(6 * np.pi * hz * t)
        x = x + (hum * 10 ** (rng.uniform(-70, -48) / 20)).astype(np.float32)
    return x.astype(np.float32), r


# ---------------------------------------------------------------- labels
def labels_for(lab, t0, n_hops, r, level_db):
    """Reference f0 / class for each plug-in hop of a chunk starting at t0 (s, in the original timeline).

    Hop k completes at (k+1)*HOP_S in the (possibly resampled) chunk, which is (k+1)*HOP_S*r into the original.

    pYIN looks ~130 ms either side of each frame, so its voiced/unvoiced boundaries are blurry. A causal model
    trained on blurry boundaries would learn to anticipate notes, so frames within 40 ms of a boundary are
    ignored (-1), and anything quieter than -62 dB is silence whatever pYIN says.
    """
    hop = float(lab["hop_s"])
    t = t0 + (np.arange(n_hops) + 1) * HOP_S * r
    idx = np.clip(np.round(t / hop).astype(int), 0, len(lab["f0"]) - 1)

    v = lab["voiced"] & (lab["f0"] > 0)
    changes = np.flatnonzero(np.diff(v.astype(np.int8)) != 0)
    near = np.zeros(len(v), bool)
    for c in changes:
        near[max(0, c - 5): c + 7] = True  # +-5 teacher frames of 8 ms = 40 ms

    voiced = v[idx]
    f0 = np.where(voiced, lab["f0"][idx] * r, 0.0).astype(np.float32)
    in_range = (f0 > 1.05 * RATE / MAX_LAG) & (f0 < RATE / MIN_LAG * 0.97)

    target = np.full(n_hops, -1, np.int16)
    b = np.round(hz_to_bin(np.where(in_range, f0, 100.0))).astype(int)
    target[voiced & in_range] = np.clip(b[voiced & in_range], 0, N_BINS - 1)
    target[~voiced] = N_BINS
    target[near[idx]] = -1
    target[level_db < -62.0] = N_BINS
    f0 = np.where(target < N_BINS, np.where(target >= 0, f0, 0.0), 0.0).astype(np.float32)
    return f0, target


def read_features(path):
    with open(path, "rb") as fh:
        h = fh.read(32)
        hops, dim = np.frombuffer(h[4:12], dtype=np.int32)
        feat = np.frombuffer(fh.read(int(hops) * int(dim)), dtype=np.int8).reshape(int(hops), int(dim))
        info = np.frombuffer(fh.read(int(hops) * 5 * 4), dtype=np.float32).reshape(int(hops), 5)
    return feat.copy(), info.copy()


def process_file(args):
    path, n_aug, seed = args
    path = Path(path)
    stem = path.stem.replace(" ", "_")
    lab = dict(np.load(LABELS / f"{stem}.npz", allow_pickle=True))
    y, sr = load_audio(path)
    x = to_mono(y)
    rng = np.random.default_rng(seed)
    n_chunks = max(1, int(np.ceil(len(x) / sr / CHUNK_S)))
    is_test = stem in TEST_STEMS
    written = 0
    with tempfile.TemporaryDirectory() as tmp:
        for c in range(n_chunks):
            a, b = int(c * CHUNK_S * sr), int(min(len(x), (c + 1) * CHUNK_S * sr))
            if b - a < sr:  # drop a sub-second tail
                continue
            t0 = a / sr
            if is_test:
                split = "test"
            else:
                # About one chunk in seven is held out, spread over every file (deterministic).
                split = "val" if (c + sum(map(ord, stem))) % 7 == 0 else "train"
            variants = [0] if split in ("val", "test") else list(range(n_aug + 1))
            for v in variants:
                seg = x[a:b].astype(np.float32)
                r = 1.0
                if v > 0:
                    seg, r = augment(seg, sr, rng)
                wav = Path(tmp) / "chunk.wav"
                sf.write(wav, seg, sr, subtype="FLOAT")
                bin_path = Path(tmp) / "chunk.bin"
                subprocess.run([str(FEATURE_BIN), str(wav), str(bin_path)], check=True, capture_output=True)
                feat, info = read_features(bin_path)
                f0, target = labels_for(lab, t0, len(feat), r, info[:, 2])
                dest = OUT / split
                dest.mkdir(parents=True, exist_ok=True)
                np.savez(dest / f"{stem}_c{c:02d}_a{v}.npz", feat=feat, target=target, f0=f0, info=info,
                         bass=bool(lab["bass"]))
                written += 1
    return f"{path.name:<40} {n_chunks} chunks -> {written} files ({'test' if is_test else 'train/val'})"


def main():
    folder = sys.argv[1]
    n_aug = int(sys.argv[sys.argv.index("--aug") + 1]) if "--aug" in sys.argv else 6
    workers = int(sys.argv[sys.argv.index("--workers") + 1]) if "--workers" in sys.argv else 8
    files = [p for p in sorted(Path(folder).glob("*.wav")) if (LABELS / f"{p.stem.replace(' ', '_')}.npz").exists()]
    print(f"{len(files)} labelled files, {n_aug} augmented variants per training chunk")
    jobs = [(str(p), n_aug, 1000 + i) for i, p in enumerate(files)]
    with Pool(workers) as pool:
        for line in pool.imap_unordered(process_file, jobs):
            print(line, flush=True)


if __name__ == "__main__":
    main()
