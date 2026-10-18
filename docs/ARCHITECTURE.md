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

## Phase 2 — neural picker (built, optional "AI Assist")

A small recurrent network (GRU, 186k weights, ~170k multiply-adds per 2.7 ms hop, ~6% of one core) reads the multi-scale
NSDF the plug-in already computes (four windows on a log-lag grid), the classical tracker's own answer, and the level.
It outputs a distribution over 96 log-lag bins plus "unvoiced". The exact pitch still comes from the NSDF peak next to
the chosen bin; the network picks *which* peak (fixing octave and harmonic slips) and whether anything is voiced.
`training/` holds the whole pipeline; the C++ forward pass is checked against PyTorch to 2e-3 in the tests.

**Hybrid rule (what ships behind the switch).** Measured on 34 minutes of real bass and voice against an offline pYIN
reference, the network is clearly better above ~160 Hz and not better in the bass. So the bass range stays classical,
and the network corrects and vetoes: it may start a pitch the classical tracker missed only when 97% sure.

| real recordings, note held vs reference note | classical | AI Assist |
|---|---|---|
| voice: right-note frames | 0.770 | **0.801** |
| voice: octave slips | 3.2% | **0.4%** |
| voice: notes shorter than 80 ms (blips) | 585 | **515** |
| bass: right-note frames | 0.807 | 0.806 (unchanged by design) |
| held-out bass file / held-out voice file | 0.77 / 0.82 | 0.77 / 0.81 (no regression, no gain) |
| synthetic hard suite: notes found / false notes | 115 / 2 | 115 / 5 |
| CPU | 0.5% of a core | ~6% |

**Honest limits.** The reference is pYIN, not a human transcription. The large voice gain is on two high-register
takes whose validation chunks come from the same sessions as training; on a second, unseen singer the two modes were
level. Latency is unchanged: the network sees the same windows, so it cannot beat the two-period physical limit.
Next: more bass material (the bass range is where the network is weakest), then evaluate on other singers and rooms.

## Phase 3 — musical polish

Glide-vs-jump classification, MPE output, expression CC from the envelope,
scale/key snapping, UI for the early-commit threshold, latency-aware recording
(shift recorded notes back by the measured latency).
