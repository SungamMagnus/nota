"""Inventory of the training recordings: format, level, stereo content, and a rough pitch
profile (to confirm what is bass and what is voice). Read-only on the data folder.

    .venv/bin/python inventory.py /path/to/recordings
"""
import sys
from pathlib import Path

import librosa
import numpy as np
from audio_io import load_audio


def db(x):
    return 20 * np.log10(max(float(x), 1e-9))


def main(folder):
    files = sorted(Path(folder).glob("*.wav"))
    print(f"{'file':<38} {'ch':>2} {'sr':>6} {'sec':>6} {'peak':>6} {'rms':>6} {'L/R corr':>8} {'f0 p10':>7} {'p50':>6} {'p90':>6} {'voiced':>6}")
    total = 0.0
    for f in files:
        y, sr = load_audio(f)
        total += len(y) / sr
        mono = y.mean(axis=1)
        corr = ""
        if y.shape[1] == 2 and np.std(y[:, 0]) > 0 and np.std(y[:, 1]) > 0:
            corr = f"{np.corrcoef(y[:, 0], y[:, 1])[0, 1]:.2f}"

        # Rough pitch profile on up to 40 s from the middle, 16 kHz, pYIN.
        seg = mono[len(mono) // 2 - 20 * sr: len(mono) // 2 + 20 * sr] if len(mono) > 40 * sr else mono
        seg16 = librosa.resample(seg, orig_sr=sr, target_sr=16000)
        f0, voiced, _ = librosa.pyin(seg16, fmin=28, fmax=1400, sr=16000, frame_length=4096, hop_length=320)
        v = f0[voiced & ~np.isnan(f0)]
        p10, p50, p90 = (np.percentile(v, [10, 50, 90]) if len(v) else (0, 0, 0))
        print(f"{f.name[:38]:<38} {y.shape[1]:>2} {sr:>6} {len(y) / sr:>6.1f} {db(np.abs(mono).max()):>6.1f} "
              f"{db(np.sqrt(np.mean(mono ** 2))):>6.1f} {corr:>8} {p10:>7.0f} {p50:>6.0f} {p90:>6.0f} {np.mean(voiced):>6.2f}")
    print(f"\n{len(files)} files, {total / 60:.1f} minutes")


if __name__ == "__main__":
    main(sys.argv[1])
