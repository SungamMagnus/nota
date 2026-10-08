// Offline audio -> MIDI: the same Engine the plugin runs, fed from a WAV file.
//   nota_cli in.wav out.mid [--range auto|bass|voice] [--gate dB] [--response 0..1] [--bend] [--bend-range st]

#include "Engine.h"
#include "smf_writer.h"
#include "wav_io.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s in.wav out.mid [--range auto|bass|voice] [--gate dB] [--response 0..1] [--bend] [--bend-range st]\n", argv[0]);
        return 2;
    }

    nota::Params p;
    for (int i = 3; i < argc; ++i)
    {
        const bool more = i + 1 < argc;
        if (!std::strcmp(argv[i], "--range") && more)
        {
            const char* r = argv[++i];
            p.range = !std::strcmp(r, "bass") ? nota::RangeMode::Bass : !std::strcmp(r, "voice") ? nota::RangeMode::Voice : nota::RangeMode::Auto;
        }
        else if (!std::strcmp(argv[i], "--gate") && more)
            p.gateDb = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--response") && more)
            p.response = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--bend-range") && more)
            p.bendRange = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--bend"))
            p.pitchBend = true;
    }

    nota::io::Wav wav;
    if (!nota::io::readWav(argv[1], wav))
    {
        std::fprintf(stderr, "could not read %s (16/24/32-bit PCM or 32-bit float WAV only)\n", argv[1]);
        return 1;
    }

    nota::Engine engine;
    engine.prepare(wav.sampleRate);
    engine.setParams(p);

    nota::io::SmfWriter smf;
    nota::EventList list;
    int notes = 0;
    constexpr int kBlock = 128;
    for (size_t pos = 0; pos < wav.mono.size(); pos += kBlock)
    {
        const int n = static_cast<int>(std::min<size_t>(kBlock, wav.mono.size() - pos));
        list.clear();
        engine.process(wav.mono.data() + pos, n, list);
        for (int i = 0; i < list.count; ++i)
        {
            const auto& e = list.events[i];
            const double t = (static_cast<double>(pos) + e.sampleOffset) / wav.sampleRate;
            switch (e.type)
            {
                case nota::MidiEvent::Type::NoteOn:    smf.add(t, 0x90, e.data1, e.data2); ++notes; break;
                case nota::MidiEvent::Type::NoteOff:   smf.add(t, 0x80, e.data1, 0); break;
                case nota::MidiEvent::Type::PitchBend: smf.add(t, 0xE0, e.bend & 0x7F, (e.bend >> 7) & 0x7F); break;
                case nota::MidiEvent::Type::Cc:        smf.add(t, 0xB0, e.data1, e.data2); break;
            }
        }
    }

    if (!smf.write(argv[2]))
    {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 1;
    }
    std::printf("%zu samples @ %.0f Hz -> %d notes -> %s\n", wav.mono.size(), wav.sampleRate, notes, argv[2]);
    return 0;
}
