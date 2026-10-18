"""Train the pitch picker: a small GRU over the plug-in's own NSDF features.

For every 2.7 ms hop the network sees four NSDF windows on a log-lag grid (+ level) and outputs a distribution over
97 classes: 96 log-lag bins (the pitch) or 'unvoiced'. The precise pitch still comes from the NSDF peak next to the
chosen bin; the network's job is to pick the *right* peak (not a harmonic or sub-octave) and to decide voicing,
using temporal context. It is scored against the classical tracker on the same held-out frames.

    .venv/bin/python train.py [--epochs 30] [--steps 300] [--hidden 128] [--device mps|cpu]
"""
import glob
import math
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = Path(__file__).parent
DATA = HERE / "data" / "features"
MODELS = HERE / "data" / "models"
N_BINS = 96
N_CLASSES = N_BINS + 1  # + unvoiced
UNVOICED = N_BINS
BINS_PER_OCTAVE = (N_BINS - 1) / math.log2(554.0 / 8.0)


def arg(name, default, cast=int):
    return cast(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


class Picker(nn.Module):
    def __init__(self, dim=482, emb=128, hidden=128):
        super().__init__()
        self.inp = nn.Sequential(nn.Linear(dim, emb), nn.ReLU())
        self.gru = nn.GRU(emb, hidden, batch_first=True)
        self.head = nn.Linear(hidden + emb, N_CLASSES)

    def forward(self, x, h=None):
        e = self.inp(x)
        g, h = self.gru(e, h)
        return self.head(torch.cat([g, e], dim=-1)), h


def load_split(split):
    out = []
    for f in sorted(glob.glob(str(DATA / split / "*.npz"))):
        d = np.load(f)
        out.append(dict(name=Path(f).stem, feat=d["feat"], target=d["target"].astype(np.int64), info=d["info"], bass=bool(d["bass"]),
                        syn=Path(f).stem.startswith("syn_")))
    return out


def soft_targets(target, sigma=0.8):
    """[B,T] class ids (-1 ignore) -> [B,T,97] soft labels and a validity mask."""
    valid = target >= 0
    t = target.clamp(min=0)
    idx = torch.arange(N_CLASSES, device=target.device)
    gauss = torch.exp(-((idx[None, None, :] - t[..., None]) ** 2) / (2 * sigma ** 2))
    gauss = gauss * (idx[None, None, :] < N_BINS)  # never leak onto the 'unvoiced' class
    onehot = F.one_hot(t, N_CLASSES).float()
    q = torch.where((t == UNVOICED)[..., None], onehot, gauss)
    q = q / q.sum(-1, keepdim=True).clamp(min=1e-9)
    return q, valid


def make_batch(files, weights, rng, batch, T, device):
    xs, ts = [], []
    for i in rng.choice(len(files), size=batch, p=weights):
        f = files[i]
        n = len(f["target"])
        s = rng.integers(0, max(1, n - T))
        x, t = f["feat"][s: s + T], f["target"][s: s + T]
        if len(x) < T:  # short chunk: pad with ignored frames
            pad = T - len(x)
            x = np.pad(x, ((0, pad), (0, 0)))
            t = np.pad(t, (0, pad), constant_values=-1)
        xs.append(x)
        ts.append(t)
    x = torch.from_numpy(np.stack(xs).astype(np.float32) / 127.0).to(device)
    t = torch.from_numpy(np.stack(ts)).to(device)
    return x, t


def hz_to_bin(f):
    lag = 12000.0 / np.maximum(f, 1e-6)
    return np.log(lag / 8.0) / math.log(554.0 / 8.0) * (N_BINS - 1)


@torch.no_grad()
def evaluate(model, files, device, label):
    """Per-frame metrics vs the teacher, for the network and for the classical tracker, on the same frames."""
    model.eval()
    rows = {"real bass": [], "real voice": [], "synth bass": [], "synth voice": []}
    for f in files:
        x = torch.from_numpy(f["feat"].astype(np.float32) / 127.0)[None].to(device)
        logits, _ = model(x)
        pred = logits[0].argmax(-1).cpu().numpy()
        t = f["target"]
        classical_f = f["info"][:, 0]
        cbin = np.where(classical_f > 0, np.round(hz_to_bin(classical_f)), UNVOICED).astype(int)
        cbin = np.clip(cbin, 0, UNVOICED)
        rows[("synth " if f["syn"] else "real ") + ("bass" if f["bass"] else "voice")].append((pred, cbin, t))

    def metrics(pairs, which):
        ref_v = ref_u = rec = fa = near = octv = 0
        for pred, cbin, t in pairs:
            p = pred if which == "net" else cbin
            v = (t >= 0) & (t < N_BINS)
            u = t == UNVOICED
            pv = p < N_BINS
            ref_v += v.sum()
            ref_u += u.sum()
            rec += (v & pv).sum()
            fa += (u & pv).sum()
            both = v & pv
            d = np.abs(p[both] - t[both])
            near += (d <= 1).sum()
            mod = np.abs(d - np.round(d / BINS_PER_OCTAVE) * BINS_PER_OCTAVE)
            octv += ((d > 2) & (np.round(d / BINS_PER_OCTAVE) >= 1) & (mod <= 1.6)).sum()
        both_n = max(rec, 1)
        return dict(recall=rec / max(ref_v, 1), false_alarm=fa / max(ref_u, 1), acc=near / both_n, octave=octv / both_n)

    print(f"  {label}:  {'':<10}{'voicing recall':>15}{'false alarm':>12}{'pitch ok':>10}{'octave err':>11}")
    out = {}
    for group, pairs in rows.items():
        if not pairs:
            continue
        for which, name in (("classical", "classical"), ("net", "network  ")):
            m = metrics(pairs, "net" if which == "net" else "classical")
            out[(group, which)] = m
            print(f"  {label}:  {group:<11} {name:<9}{m['recall']:>13.3f}{m['false_alarm']:>12.3f}{m['acc']:>10.3f}{m['octave']:>11.4f}")
    model.train()
    return out


def main():
    device = arg("--device", "mps" if torch.backends.mps.is_available() else "cpu", str)
    epochs, steps = arg("--epochs", 30), arg("--steps", 300)
    hidden = arg("--hidden", 128)
    T, batch = arg("--T", 384), arg("--batch", 64)
    torch.manual_seed(0)
    rng = np.random.default_rng(0)

    train, val, test = load_split("train"), load_split("val"), load_split("test")
    print(f"train {len(train)} files / {sum(len(f['target']) for f in train)} hops, val {len(val)}, test {len(test)}, device {device}")
    w = np.array([len(f["target"]) for f in train], float)
    w /= w.sum()

    dim = train[0]["feat"].shape[1]
    model = Picker(dim=dim, hidden=hidden).to(device)
    print(f"parameters: {sum(p.numel() for p in model.parameters()):,}")
    opt = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-4)
    total = epochs * steps
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=2e-3, total_steps=total, pct_start=0.1)
    MODELS.mkdir(parents=True, exist_ok=True)

    t0, best = time.time(), 1e9
    for ep in range(epochs):
        run = 0.0
        for _ in range(steps):
            x, t = make_batch(train, w, rng, batch, T, device)
            logits, _ = model(x)
            q, valid = soft_targets(t)
            valid[:, :24] = False  # let the recurrent state warm up before scoring
            logp = F.log_softmax(logits, -1)
            loss = -(q * logp).sum(-1)[valid].mean()
            opt.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            sched.step()
            run += float(loss)
        print(f"epoch {ep + 1:3d}/{epochs}  train loss {run / steps:.4f}  ({time.time() - t0:.0f}s)", flush=True)

        if (ep + 1) % 3 == 0 or ep + 1 == epochs:
            m = evaluate(model, val, device, "val ")
            vloss = sum(1 - m[k]["acc"] + m[k]["octave"] + (1 - m[k]["recall"]) for k in m if k[1] == "net" and k[0].startswith("real"))
            if vloss < best:
                best = vloss
                torch.save({"model": model.state_dict(), "hidden": hidden, "dim": dim}, MODELS / "picker.pt")

    print("\nbest checkpoint on validation; held-out test files:")
    ck = torch.load(MODELS / "picker.pt", map_location=device)
    model.load_state_dict(ck["model"])
    evaluate(model, test, device, "test")


if __name__ == "__main__":
    main()
