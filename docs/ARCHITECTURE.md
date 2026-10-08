# Nota — architecture and roadmap

Monophonic audio → MIDI, built for a bass guitar or a voice with no mode switch,
as little latency as the physics allows, and a path to a neural front-end.

## The latency floor (read this first)

A pitch is a *repetition*. To call a period you must hear it twice, so no
algorithm, neural or not, can name a note's fundamental before about **two
periods** of that note have been played, unless it leans on priors (timbre,
key, what was just played).

| note              | f0      | period | 2 periods |
|-------------------|---------|--------|-----------|
| B0 (5-string bass)| 31 Hz   | 32 ms  | 65 ms     |
| E1                | 41 Hz   | 24 ms  | 49 ms     |
| A2                | 110 Hz  | 9 ms   | 18 ms     |
| A3 (voice)        | 220 Hz  | 4.5 ms | 9 ms      |
| A5                | 880 Hz  | 1.1 ms | 2.3 ms    |

Everything above 150 Hz can be fast. Low bass notes cannot, which is why the AI
work targets exactly that region (see Phase 2): a model that has learned what a
plucked low string *sounds like* can commit to a note on less than two clean
periods, where a DSP tracker has to wait.

## Phase 1 — classical core (implemented)

```
input ─ mono ─ LPF 3 kHz (4th order) ─ ÷4 (12 kHz) ─ HPF 18 Hz ─┬─ ring buffer ─ PitchTracker ─┐
                                                                  └─ peak envelope ──────────────┤
                                                                                                  ▼
                                                      NoteTracker ─ note on/off, pitch bend, velocity ─ MIDI
```

* **PitchTracker** — McLeod NSDF via FFT, with a **ladder of 9 window sizes**
  (96 … 1232 samples at 12 kHz). Each 2.7 ms hop the smallest window is tried
  first and the first one that finds a clear peak wins, so high notes are
  decided in a few ms and only low notes wait for a long window. A larger window
  double-checks octave-down errors the short window cannot see. One analysis
  covers 26 Hz – 1.3 kHz, so voice and bass share the same code path
  ("automatic range"). Range = Bass/Voice only restricts what is *accepted*.
* **NoteTracker** — confirmation hops (2–4, set by *Response*), ±0.65 st
  hysteresis against vibrato, median filter, octave-jump guard, hold-over
  through unvoiced consonants, level-jump re-trigger for repeated notes,
  release relative to the note's own peak, velocity from the peak envelope.
* **Engine** — adaptive noise floor so hum/hiss never triggers, hop scheduling,
  sample-accurate event offsets.
* **Plugin** — JUCE 9, VST3 instrument with an audio sidechain input and MIDI out
  (what Ableton Live needs to route a plug-in's MIDI to other tracks; it also
  insists the instrument declares a MIDI input, which Nota ignores).

Measured on synthetic material (`build/nota_bench`, 48 kHz, Response 0.5):

| source                         | notes correct | median latency | worst |
|--------------------------------|---------------|----------------|-------|
| bass pluck, B0–G4              | 23 / 23       | 29 ms          | 80 ms (B0–D#1) |
| bass, weak fundamental         | 9 / 9         | 27 ms          | 59 ms |
| voice + 30 cent vibrato, G2–A#5| 14 / 14       | 19 ms          | 37 ms |
| CPU                            |               | ~0.6 % of one core |  |

Bass latency is ~2.5–3 periods: already close to the floor above. The headroom
is in the *low* range, which is what Phase 2 attacks.

### Known weak spots of Phase 1 (honest list)

* Not yet tested on real DI bass or real singing — only synthetic signals and
  synthesized speech. Expect to tune clarity/stability on real material.
* Fast legato (hammer-ons, slides) between low notes is slowed by window
  contamination: the long window still contains the previous note.
* Strong sub-octave content or heavy distortion/fuzz on the bass can fool NSDF.
* Breathy/falsetto voice and rap-like speech produce chatter.
* Pitch bend currently follows deviation up to the bend range, with no
  glide-vs-jump classifier (a hammer-on is a new note, a slide is a bend).

## Phase 2 — neural front-end (next)

Goal: make the same hop-synchronous interface (`PitchFrame` per 2.7 ms) come
from, or be fused with, a small **streaming neural model** running in the audio
thread.

**Model** (target ≤ 150 k parameters, a few µs per hop on Apple silicon):

* learned filterbank front-end: strided causal Conv1D on the 12 kHz signal
* 2–3 causal dilated conv blocks → 1–2 GRU layers (state carried across hops)
* heads: pitch posterior over ~260 bins (25 cent resolution, 26 Hz – 1.3 kHz),
  voicing, **onset**, and an *early-confidence* output
* cents refinement stays with the NSDF peak (sub-cent accurate); the network
  supplies **which peak** is the fundamental (octave errors), **whether** it is
  voiced, and **when** a new note starts

**Why it lowers latency:** trained on windows that *end* a fraction of a period
after an onset, with a loss that rewards a correct posterior early and a calibrated
confidence. `NoteTracker` then fires on confidence instead of waiting a fixed
number of hops, and the *Response* control becomes a real accuracy/speed knob.

**Training data** (all offline, in `training/`):

1. *Synthetic with exact labels* — grow `tools/TestSignals.h` into a generator:
   physical-modelled plucked/fretted/slapped bass, DI + amp/cab/distortion
   chains, compressor, formant-filtered voices with vibrato/glides/breath/
   consonants, room reverb, hum and noise. Free, unlimited, perfectly labelled.
2. *Teacher distillation on real audio* — label any real recordings (including
   your own bass and voice) with a heavy offline teacher (CREPE-full / pYIN /
   RMVPE-class) and train the tiny causal student on those labels. No manual
   annotation.
3. *Public sets for evaluation/fine-tuning* where licences allow (vocal: MIR-1K,
   vocadito, PTDB-TUG; guitar/bass: GuitarSet, EGDB).

**Runtime:** [RTNeural](https://github.com/jatinchowdhury18/RTNeural) —
header-only, allocation-free, built for audio threads (Conv1D, GRU, dense). Core
ML / ONNX Runtime are poor fits here: per-call overhead and non-deterministic
timing.

**Evaluation:** the same `nota_bench` sweep plus real recordings, scored by
onset latency distribution, octave-error rate, and note-level F1 (mir_eval).
Phase 1 numbers above are the baseline the model has to beat.

## Phase 3 — musical polish

Glide-vs-jump classification, MPE output, expression CC from the envelope,
scale/key snapping, UI for the early-commit threshold, latency-aware recording
(shift recorded notes back by the measured latency).
