# Training the pitch picker

Everything here is offline Python (PyTorch, native arm64, Apple GPU via MPS). The data lives outside the repo and is
never committed (`training/data/` and `.venv/` are git-ignored).

```bash
python3 -m venv .venv && .venv/bin/pip install torch soundfile scipy librosa matplotlib
cmake -B ../build -DNOTA_BUILD_PLUGIN=OFF && cmake --build ../build -j          # nota_features, nota_cli, ...

.venv/bin/python teacher.py   /path/to/recordings          # offline pYIN reference labels (parallel, ~10 min)
.venv/bin/python build_dataset.py /path/to/recordings      # features from the C++ front-end + labels + augmentation
.venv/bin/python synth_data.py                             # synthetic bass/voice with exact labels
.venv/bin/python train.py --epochs 28 --steps 250          # ~45 min on an M-series GPU
.venv/bin/python export_model.py data/models/picker.pt     # bakes weights into ../source/dsp/PickerWeights.cpp
```

Evaluation against the offline reference: `compare.py`, `note_eval.py` (frame-level note accuracy), `count_notes.py`,
`analyze_*.py`; `plot_compare.py` draws a spectrogram with the trackers overlaid.

## Why it is built this way

* **Features come from the plug-in's own C++** (`nota_features`), so training and runtime cannot drift apart.
* **Teacher = pYIN**, run offline over whole recordings (it sees the future; the plug-in cannot). CREPE was tried and
  rejected: unreliable on low bass. pYIN's voicing boundaries are blurry, so +-40 ms around any change is ignored.
* **The network sees the classical tracker's answer** and learns when to override it. A first version without that
  prior generalised badly on bass (it chose a loud 2nd harmonic over a weak fundamental).
* **Synthetic data** with exact labels covers weak/missing fundamentals, dominant harmonics, glides, vibrato, plucks,
  bowing and consonant gaps.

## Data and licensing

The recordings are the author's own plus sample-library loops (the "Riffs" files are tagged Zero-G). Check the
licence of any library before publishing weights trained on it. **The trained weights (`source/dsp/PickerWeights.cpp`) and
the reference vectors (`tests/picker_reference.h`) are therefore not part of this repository** and are git-ignored; train
your own with these scripts (`export_model.py` writes both files). Without them the build uses `PickerWeightsStub.cpp`
and AI Assist is unavailable.
