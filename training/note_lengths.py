import sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).parent / "data"
for d in sys.argv[1:]:
    short = med = tot = 0; lens = []
    for csv in sorted((ROOT / d).glob("*.csv")):
        if csv.stem.startswith(("27", "28", "29", "30", "31", "32", "33", "48K", "audio2miditrainbass")):
            continue
        a = np.loadtxt(csv, delimiter=",", skiprows=1); note = a[:, 4].astype(int); hop = a[1, 0] - a[0, 0]
        i = 0
        while i < len(note):
            j = i
            while j < len(note) and note[j] == note[i]: j += 1
            if note[i] >= 0: lens.append((j - i) * hop)
            i = j
    lens = np.array(lens)
    print(f"{d:<12} voice notes {len(lens):5d}   <80ms {np.sum(lens < 0.08):4d}   <150ms {np.sum(lens < 0.15):4d}   median {np.median(lens) * 1000:5.0f} ms")
