"""Offline reference ("teacher") pitch labels for the real recordings, from pYIN.

The plug-in has to decide causally, a few milliseconds after the sound. A teacher does not: pYIN sees the whole
recording and smooths the pitch track with an HMM, so on these takes it is far more accurate than any causal
tracker (checked by eye against spectrograms: it follows the real fundamental, including a 37 Hz bass drone that the
real-time tracker mistakes for its 3rd harmonic). CREPE was tried and rejected: it is unreliable on low bass.

Files are labelled in parallel, one process each. Output: data/labels/<stem>.npz on an 8 ms time axis.

    .venv/bin/python teacher.py /path/to/recordings [workers]
"""
import sys
import time
from multiprocessing import Pool
from pathlib import Path

import librosa
import numpy as np

from audio_io import load_audio, to_mono

SR = 16000
HOP = 128  # 8 ms at 16 kHz
OUT = Path(__file__).parent / "data" / "labels"
BASS_PREFIXES = ("27", "28", "29", "30", "31", "32", "33", "48K", "audio2miditrainbass")


def label_one(path):
    path = Path(path)
    dest = OUT / f"{path.stem.replace(' ', '_')}.npz"
    if dest.exists():
        return f"skip   {path.name}"
    t0 = time.time()
    y, sr = load_audio(path)
    x16 = librosa.resample(to_mono(y), orig_sr=sr, target_sr=SR).astype(np.float32)
    bass = path.name.startswith(BASS_PREFIXES)

    # Longer window for bass (>= 2 periods of a 30 Hz note), shorter for voice (faster pitch movement).
    f0, voiced, prob = librosa.pyin(x16, fmin=28, fmax=1400, sr=SR, frame_length=4096 if bass else 2048,
                                    hop_length=HOP, fill_na=0.0)
    n = len(f0)
    pad = np.pad(x16, (HOP, HOP + n * HOP))
    frames = np.stack([pad[i * HOP: i * HOP + 2 * HOP] for i in range(n)])
    level_db = 20 * np.log10(np.sqrt((frames ** 2).mean(axis=1)) + 1e-9)

    np.savez_compressed(dest, f0=f0.astype(np.float32), voiced=np.asarray(voiced, bool), prob=prob.astype(np.float32),
                        level_db=level_db.astype(np.float32), hop_s=HOP / SR, sr=SR, bass=bass, name=path.name)
    return f"done   {path.name:<40} {len(x16) / SR:6.1f}s audio in {time.time() - t0:5.0f}s  voiced {np.mean(voiced):.2f}"


def main(folder, workers):
    OUT.mkdir(parents=True, exist_ok=True)
    files = sorted(Path(folder).glob("*.wav"), key=lambda p: -p.stat().st_size)  # biggest first: best packing
    with Pool(workers) as pool:
        for line in pool.imap_unordered(label_one, files):
            print(line, flush=True)


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 6)
