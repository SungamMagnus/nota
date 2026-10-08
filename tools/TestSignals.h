#pragma once

// Synthetic bass / voice material with exact ground truth, shared by the tests and
// the bench. Deliberately self-contained and deterministic.

#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nota::test
{

constexpr double kPi = 3.14159265358979323846;

inline double midiToHz(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }

struct Rng
{
    uint32_t s = 0x1234abcd;
    float uni() // [-1, 1)
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

struct NoteSpec
{
    double midi = 40.0;     // may be fractional
    double start = 0.0;     // seconds
    double dur = 0.5;       // seconds
    double vibratoCents = 0.0;
    double gain = 0.5;
};

enum class Timbre { BassPluck, BassStrongH2, Voice };

// Adds one note into `buf` (mixing). Returns nothing; everything is deterministic.
inline void renderNote(std::vector<float>& buf, double fs, const NoteSpec& n, Timbre timbre, Rng& rng)
{
    const double f0 = midiToHz(n.midi);
    const int i0 = static_cast<int>(n.start * fs);
    const int len = static_cast<int>(n.dur * fs);
    const int maxHarm = std::max(1, static_cast<int>(5000.0 / f0));

    double phase = 0.0;
    for (int i = 0; i < len && i0 + i < static_cast<int>(buf.size()); ++i)
    {
        const double t = i / fs;
        double f = f0;
        if (n.vibratoCents != 0.0 && t > 0.12)
            f *= std::pow(2.0, n.vibratoCents / 1200.0 * std::sin(2.0 * kPi * 5.5 * (t - 0.12)));
        phase += 2.0 * kPi * f / fs;

        double s = 0.0;
        double env = 1.0;

        if (timbre == Timbre::Voice)
        {
            for (int h = 1; h <= maxHarm; ++h)
            {
                const double fh = h * f0;
                double g = 0.25;
                g += 1.0 / (1.0 + std::pow((fh - 650.0) / 90.0, 2.0));
                g += 0.7 / (1.0 + std::pow((fh - 1100.0) / 110.0, 2.0));
                g += 0.4 / (1.0 + std::pow((fh - 2500.0) / 180.0, 2.0));
                s += g / h * std::sin(h * phase);
            }
            s += 0.02 * rng.uni(); // aspiration noise
            const double a = std::min(1.0, t / 0.02);
            const double r = std::min(1.0, (n.dur - t) / 0.04);
            env = a * std::max(0.0, r);
        }
        else
        {
            for (int h = 1; h <= maxHarm; ++h)
            {
                double a = 1.0 / std::pow(h, 1.1);
                if (timbre == Timbre::BassStrongH2 && h == 2)
                    a = 1.6;
                if (timbre == Timbre::BassStrongH2 && h == 1)
                    a = 0.45;
                const double tau = 1.5 / std::sqrt(static_cast<double>(h));
                s += a * std::exp(-t / tau) * std::sin(h * phase + 0.3 * h);
            }
            const double a = std::min(1.0, t / 0.002);
            const double r = std::min(1.0, (n.dur - t) / 0.03);
            env = a * std::max(0.0, r);
            s *= 0.5;
        }

        buf[static_cast<size_t>(i0 + i)] += static_cast<float>(n.gain * env * s);
    }
}

inline std::vector<float> renderSequence(double fs, double totalSec, const std::vector<NoteSpec>& notes, Timbre timbre,
                                         float noiseDb = -90.0f)
{
    std::vector<float> buf(static_cast<size_t>(totalSec * fs), 0.0f);
    Rng rng;
    for (const auto& n : notes)
        renderNote(buf, fs, n, timbre, rng);

    const float na = std::pow(10.0f, noiseDb / 20.0f);
    Rng nrng;
    nrng.s = 777;
    for (auto& v : buf)
        v += na * nrng.uni();
    return buf;
}

struct AbsEvent
{
    MidiEvent::Type type;
    int data1;
    int data2;
    int bend;
    long long sample;
};

inline std::vector<AbsEvent> runEngine(Engine& engine, const std::vector<float>& audio, int blockSize = 128)
{
    std::vector<AbsEvent> all;
    EventList list;
    for (size_t pos = 0; pos < audio.size(); pos += static_cast<size_t>(blockSize))
    {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(blockSize), audio.size() - pos));
        list.clear();
        engine.process(audio.data() + pos, n, list);
        for (int i = 0; i < list.count; ++i)
        {
            const auto& e = list.events[i];
            all.push_back({ e.type, e.data1, e.data2, e.bend, static_cast<long long>(pos) + e.sampleOffset });
        }
    }
    return all;
}

} // namespace nota::test
