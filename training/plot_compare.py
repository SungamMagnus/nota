"""Spectrogram of the worst stretch for the network, with pYIN (reference), the classical tracker and the network overlaid.

    .venv/bin/python plot_compare.py "<recording.wav>" out.png [--dur 8] [--start s]
"""
import sys
from pathlib import Path

import librosa
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from audio_io import load_audio, to_mono

ROOT = Path(__file__).parent / "data"


def main():
    wav, out = sys.argv[1], sys.argv[2]
    dur = float(sys.argv[sys.argv.index("--dur") + 1]) if "--dur" in sys.argv else 8.0
    stem = Path(wav).stem.replace(" ", "_")
    lab = np.load(ROOT / "labels" / f"{stem}.npz", allow_pickle=True)
    cl = np.loadtxt(ROOT / "frames" / f"{stem}.csv", delimiter=",", skiprows=1)
    nn = np.loadtxt(ROOT / "frames_nn" / f"{stem}.csv", delimiter=",", skiprows=1)
    hop = float(lab["hop_s"])
    t_ref = np.arange(len(lab["f0"])) * hop
    ref = np.where(lab["voiced"] & (lab["f0"] > 0), lab["f0"], 0.0)

    if "--start" in sys.argv:
        start = float(sys.argv[sys.argv.index("--start") + 1])
    else:  # the window where the network disagrees most with the reference
        nn_on_ref = nn[np.clip(np.searchsorted(nn[:, 0], t_ref), 0, len(nn) - 1), 1]
        bad = (ref > 0) & (nn_on_ref > 0) & (np.abs(np.log2(nn_on_ref / np.maximum(ref, 1e-6))) > 0.4)
        w = int(dur / hop)
        score = np.convolve(bad.astype(float), np.ones(w), mode="valid")
        start = float(np.argmax(score) * hop)
    print(f"window {start:.1f}s .. {start + dur:.1f}s")

    y, sr = load_audio(wav)
    x = to_mono(y)[int(start * sr): int((start + dur) * sr)]
    C = np.abs(librosa.cqt(x, sr=sr, fmin=27.5, n_bins=216, bins_per_octave=36, hop_length=256))
    t_c = librosa.frames_to_time(np.arange(C.shape[1]), sr=sr, hop_length=256) + start
    freqs = librosa.cqt_frequencies(C.shape[0], fmin=27.5, bins_per_octave=36)

    fig, ax = plt.subplots(figsize=(15, 6.5))
    ax.pcolormesh(t_c, freqs, 20 * np.log10(C / (C.max() + 1e-9) + 1e-5), shading="auto", cmap="magma", vmin=-70, vmax=0)
    ax.set_yscale("log")
    ax.set_ylim(27.5, 1000)

    def draw(t, f, color, label, ms, off=0.0):
        m = (f > 0) & (t >= start) & (t <= start + dur)
        ax.plot(t[m], f[m] * (2 ** off), ".", ms=ms, color=color, label=label)

    draw(t_ref, ref, "cyan", "pYIN (reference)", 4)
    draw(cl[:, 0], cl[:, 1], "white", "classical tracker", 2.5)
    draw(nn[:, 0], nn[:, 1], "lime", "neural picker", 2.5)
    ax.legend(loc="upper right", fontsize=9)
    ax.set_title(f"{Path(wav).name}  [{start:.1f}s +{dur:.0f}s]")
    fig.tight_layout()
    fig.savefig(out, dpi=90)


if __name__ == "__main__":
    main()
