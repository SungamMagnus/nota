# Nota

Real-time **monophonic audio → MIDI** for bass guitar and voice. One tracker covers
~26 Hz – 1.3 kHz, so there is no mode to pick. A VST3 instrument, built with
[JUCE](https://juce.com). The DSP core is plain C++17 with no JUCE dependency.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it works, the latency
and the latency limits.

## Building

Requires CMake ≥ 3.22 and the Xcode command line tools. JUCE 9.0.1 is fetched at
configure time.

```bash
cmake -B build-plugin -DCMAKE_BUILD_TYPE=Release
cmake --build build-plugin --config Release -j
```

To reuse an existing JUCE checkout:

```bash
cmake -B build-plugin -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
```

The plugin lands in `build-plugin/source/plugin/NotaVST_artefacts/Release/VST3/Nota.vst3`. Add
`-DNOTA_INSTALL_AFTER_BUILD=ON` to copy it into `~/Library/Audio/Plug-Ins/VST3`.

DSP, tools and tests only (no JUCE):

```bash
cmake -B build -DNOTA_BUILD_PLUGIN=OFF && cmake --build build -j
./build/nota_tests          # unit + end-to-end tests
./build/nota_bench          # latency / accuracy sweep across the bass and voice range
./build/nota_cli in.wav out.mid --range bass   # add --mpe for MPE output
```

## Using it in Ableton Live

Nota is a VST3 *instrument* with an audio sidechain input. Live only routes a
plug-in's MIDI output when the plug-in sits on a MIDI track, and only for VST (not
AU) — see [Ableton's article](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in).

1. Load **Nota** on a MIDI track.
2. In its sidechain chooser pick the audio track with your bass / voice.
3. On the track that should play the notes: *MIDI From* → the Nota track →
   *Nota* (channel chooser). Set Monitor to *In*.

**Simpler in Live: Max for Live.** `maxforlive/` has two devices that make Nota behave like a MIDI effect: *Nóta source* on the
audio track and *Nóta destination* before the instrument on the instrument track, which can also play chords and a
random scale-note flourish in Live's scale or one you choose (see `maxforlive/README.md`).

Other hosts that route plug-in MIDI output and sidechains (Bitwig, Reaper) should work the same way.

## MPE mode

*Output mode → MPE* gives every note its own MIDI channel (member channels 2-16, master channel 1, lower zone), so
each note carries its own expression, as an MPE synth expects:

- **Pitch bend, per note.** The note number is the nearest semitone; the remaining deviation is that note's own bend
  (range set by *MPE bend*, default ±48, sent to the synth with RPN 0). Vibrato and glides bend one note instead of
  firing new ones. An abrupt jump (hammer-on, leap) still starts a new note.
- **Pressure** (channel pressure) follows the note's level; **Slide** (CC74) follows its brightness (harmonic richness
  relative to a sine). Each can be switched off.
- Notes rotate through the member channels, so a released note's tail is never bent by the next note.
- On switching mode (and on load, in MPE) Nota sends the MPE zone setup (RPN 6) and the per-note bend range.

The receiving synth must be in MPE mode with the same bend range.

**Ableton Live:** Live has its own MPE handling, and its manual says plug-ins with MIDI outs and MPE enabled can output MPE
([manual, "MPE in External Plug-ins"](https://www.ableton.com/en/live-manual/12/editing-mpe/)). Switch MPE on for the Nota
device (its settings are in the context menu of the device's title bar), send its MIDI to an MPE-capable instrument as
usual (*MIDI From* the Nota track), and make Live's per-note bend range match *MPE bend* (default 48). Nota uses the lower
zone: master channel 1, member channels 2-16. Not yet verified inside Live.

Controls: *Gate* (level below which nothing happens), *Response* (accurate ↔
fast), *Release* (note-off, in dB under the note's own peak), *Velocity range* (the output
velocity range, min–max; equal handles = fixed velocity), *Full level* (the attack level that reaches the max), *Retrigger* (re-pluck of the same note), *Range*
(Auto / Bass / Voice), *Pitch Bend* + *Bend Range* (must match the receiving
synth), *Transpose*, *Tuning*, *MIDI Channel* (Standard mode), *Output mode* (Standard / MPE).
