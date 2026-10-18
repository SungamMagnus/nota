"""Audio loading that survives odd WAV headers: falls back to macOS afconvert (cached, git-ignored)."""
import subprocess
from pathlib import Path

import numpy as np
import soundfile as sf

CACHE = Path(__file__).parent / "data" / "cache"


def load_audio(path):
    """Returns (float32 array [n, channels], sample rate)."""
    path = Path(path)
    try:
        y, sr = sf.read(path, dtype="float32", always_2d=True)
        return y, sr
    except Exception:
        CACHE.mkdir(parents=True, exist_ok=True)
        out = CACHE / (path.stem.replace(" ", "_") + ".wav")
        if not out.exists():
            subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEF32", str(path), str(out)], check=True)
        y, sr = sf.read(out, dtype="float32", always_2d=True)
        return y, sr


def to_mono(y):
    return y.mean(axis=1) if y.ndim == 2 else y
