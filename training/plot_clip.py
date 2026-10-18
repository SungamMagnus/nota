"""Spectrogram of a clip with the pitch tracks overlaid, to judge by eye which tracker (if any) to trust.

    .venv/bin/python plot_clip.py "<recording.wav>" <start_s> <dur_s> out.png [--frames data/frames/x.csv]
"""
import os
import sys
from pathlib import Path

os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")

import librosa
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import torch
import torchcrepe

from audio_io import load_audio, to_mono


def main():
    wav, start, dur, out = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    frames = sys.argv[sys.argv.index("--frames") + 1] if "--frames" in sys.argv else None

    y, sr = load_audio(wav)
    x = to_mono(y)
    x = x[int(start * sr): int((start + dur) * sr)]
    x16 = librosa.resample(x, orig_sr=sr, target_sr=16000).astype(np.float32)

    # CQT 27.5 Hz .. ~1.7 kHz, 36 bins per octave.
    C = np.abs(librosa.cqt(x, sr=sr, fmin=27.5, n_bins=36 * 6, bins_per_octave=36, hop_length=256))
    t_c = librosa.frames_to_time(np.arange(C.shape[1]), sr=sr, hop_length=256)

    f_py, v_py, p_py = librosa.pyin(x16, fmin=28, fmax=1400, sr=16000, frame_length=4096, hop_length=64)
    t_py = np.arange(len(f_py)) * 64 / 16000

    a = torch.from_numpy(x16)[None]
    f_cr, per = torchcrepe.predict(a, 16000, hop_length=64, fmin=30, fmax=1400, model="full", batch_size=512,
                                   device="cpu", return_periodicity=True, decoder=torchcrepe.decode.argmax, pad=True)
    f_cr, per = f_cr[0].numpy(), per[0].numpy()
    t_cr = np.arange(len(f_cr)) * 64 / 16000

    fig, ax = plt.subplots(figsize=(15, 6))
    freqs = librosa.cqt_frequencies(C.shape[0], fmin=27.5, bins_per_octave=36)
    ax.pcolormesh(t_c, freqs, 20 * np.log10(C / (C.max() + 1e-9) + 1e-5), shading="auto", cmap="magma", vmin=-70, vmax=0)
    ax.set_yscale("log")
    ax.set_ylim(27.5, 1600)
    ax.plot(t_py[v_py & (f_py > 0)], f_py[v_py & (f_py > 0)], ".", ms=2.5, color="cyan", label="pYIN")
    m = per > 0.5
    ax.plot(t_cr[m], f_cr[m], ".", ms=2.5, color="lime", label="CREPE (argmax, periodicity>0.5)")
    if frames:
        d = np.loadtxt(frames, delimiter=",", skiprows=1)
        sel = (d[:, 0] >= start) & (d[:, 0] <= start + dur) & (d[:, 1] > 0)
        ax.plot(d[sel, 0] - start, d[sel, 1], ".", ms=2.5, color="white", label="Nota (causal)")
    ax.legend(loc="upper right", fontsize=8)
    ax.set_title(f"{Path(wav).name}  [{start:.0f}s +{dur:.0f}s]")
    ax.set_xlabel("s")
    ax.set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(out, dpi=90)


if __name__ == "__main__":
    main()
