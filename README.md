# Nota

Real-time **monophonic audio → MIDI** for bass guitar and voice. One tracker covers
~26 Hz – 1.3 kHz, so there is no mode to pick. A VST3 instrument, built with
[JUCE](https://juce.com). The DSP core is plain C++17 with no JUCE dependency.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it works, the latency
limits, and the neural-network roadmap.

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
./build/nota_cli in.wav out.mid --range bass
```

## Using it in Ableton Live

Nota is a VST3 *instrument* with an audio sidechain input. Live only routes a
plug-in's MIDI output when the plug-in sits on a MIDI track, and only for VST (not
AU) — see [Ableton's article](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in).

1. Load **Nota** on a MIDI track.
2. In its sidechain chooser pick the audio track with your bass / voice.
3. On the track that should play the notes: *MIDI From* → the Nota track →
   *Nota* (channel chooser). Set Monitor to *In*.

Other hosts that route plug-in MIDI output and sidechains (Bitwig, Reaper) should work the same way.

Controls: *Gate* (level below which nothing happens), *Response* (accurate ↔
fast), *Release* (note-off, in dB under the note's own peak), *Velocity range* (the output
velocity range, min–max; equal handles = fixed velocity), *Full level* (the attack level that reaches the max), *Retrigger* (re-pluck of the same note), *Range*
(Auto / Bass / Voice), *Pitch Bend* + *Bend Range* (must match the receiving
synth), *Transpose*, *Tuning*, *MIDI Channel*.
