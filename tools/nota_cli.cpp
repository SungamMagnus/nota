// Offline audio -> MIDI: the same Engine the plugin runs, fed from a WAV file.
//   nota_cli in.wav out.mid [--range auto|bass|voice] [--gate dB] [--response 0..1] [--bend] [--bend-range st]
//                           [--mpe] [--mpe-range st] [--frames frames.csv] [--neural]   (MPE: one channel per note, per-note bend/pressure/slide)

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
        std::fprintf(stderr, "usage: %s in.wav out.mid [--range auto|bass|voice] [--gate dB] [--response 0..1] [--bend] [--bend-range st] [--mpe] [--mpe-range st]\n", argv[0]);
        return 2;
    }

    nota::Params p;
    const char* framesPath = nullptr;
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
        else if (!std::strcmp(argv[i], "--vel-full") && more)
            p.velocityFullDb = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--vel-range") && more)
            p.velocityRangeDb = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--vel-auto"))
            p.velocityAuto = true;
        else if (!std::strcmp(argv[i], "--bend-range") && more)
            p.bendRange = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--mpe-range") && more)
            p.mpeBendRange = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--bend"))
            p.pitchBend = true;
        else if (!std::strcmp(argv[i], "--mpe"))
            p.mpe = true;
        else if (!std::strcmp(argv[i], "--neural"))
            p.neural = true;
        else if (!std::strcmp(argv[i], "--neural-pure"))
            p.neural = p.neuralPure = true;
        else if (!std::strcmp(argv[i], "--frames") && more)
            framesPath = argv[++i];
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

    // --frames: one CSV row per analysis hop (what the tracker decided, ~2.7 ms apart), for comparing with references.
    struct CsvSink : nota::FrameSink
    {
        FILE* f = nullptr;
        double secondsPerHop = 0.0;
        void onFrame(const nota::PitchFrame& fr, int note, long long hop, const float*) override
        {
            std::fprintf(f, "%.5f,%.3f,%.4f,%.2f,%d\n", static_cast<double>(hop + 1) * secondsPerHop, fr.voiced ? fr.freqHz : 0.0, fr.clarity,
                         fr.levelDb, note);
        }
    } csv;
    if (framesPath != nullptr)
    {
        csv.f = std::fopen(framesPath, "w");
        if (csv.f == nullptr)
        {
            std::fprintf(stderr, "could not write %s\n", framesPath);
            return 1;
        }
        std::fprintf(csv.f, "time,freq_hz,clarity,level_db,note\n");
        csv.secondsPerHop = nota::Engine::kHopSamples / engine.decimatedRate();
        engine.setFrameSink(&csv);
    }

    nota::io::SmfWriter smf;
    if (p.mpe)
    {
        // Lower-zone MPE configuration: master channel 1, member channels 2..16, and the per-note bend range.
        const auto rpn = [&](int ch, int msb, int lsb, int dataMsb) {
            smf.add(0.0, static_cast<uint8_t>(0xB0 | ch), 101, static_cast<uint8_t>(msb));
            smf.add(0.0, static_cast<uint8_t>(0xB0 | ch), 100, static_cast<uint8_t>(lsb));
            smf.add(0.0, static_cast<uint8_t>(0xB0 | ch), 6, static_cast<uint8_t>(dataMsb));
            smf.add(0.0, static_cast<uint8_t>(0xB0 | ch), 101, 127);
            smf.add(0.0, static_cast<uint8_t>(0xB0 | ch), 100, 127);
        };
        rpn(0, 0, 6, 15);
        for (int ch = 1; ch <= 15; ++ch)
            rpn(ch, 0, 0, static_cast<int>(p.mpeBendRange));
    }
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
            const uint8_t ch = e.channel ? static_cast<uint8_t>(e.channel - 1) : 0;
            switch (e.type)
            {
                case nota::MidiEvent::Type::NoteOn:    smf.add(t, static_cast<uint8_t>(0x90 | ch), e.data1, e.data2); ++notes; break;
                case nota::MidiEvent::Type::NoteOff:   smf.add(t, static_cast<uint8_t>(0x80 | ch), e.data1, 0); break;
                case nota::MidiEvent::Type::PitchBend: smf.add(t, static_cast<uint8_t>(0xE0 | ch), e.bend & 0x7F, (e.bend >> 7) & 0x7F); break;
                case nota::MidiEvent::Type::Cc:        smf.add(t, static_cast<uint8_t>(0xB0 | ch), e.data1, e.data2); break;
                case nota::MidiEvent::Type::ChannelPressure: smf.add(t, static_cast<uint8_t>(0xD0 | ch), e.data1, 0); break;
            }
        }
    }

    if (csv.f != nullptr)
        std::fclose(csv.f);

    if (!smf.write(argv[2]))
    {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 1;
    }
    std::printf("%zu samples @ %.0f Hz -> %d notes -> %s\n", wav.mono.size(), wav.sampleRate, notes, argv[2]);
    return 0;
}
