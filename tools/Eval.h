#pragma once

// Note-level scoring of detected MIDI against ground truth.

#include "Scenarios.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nota::test
{

struct DetectedNote
{
    int midi;
    double onset;
    double offset; // < 0 when never released
};

inline std::vector<DetectedNote> toNotes(const std::vector<AbsEvent>& ev, double fs)
{
    std::vector<DetectedNote> out;
    for (const auto& e : ev)
    {
        const double t = static_cast<double>(e.sample) / fs;
        if (e.type == MidiEvent::Type::NoteOn)
            out.push_back({ e.data1, t, -1.0 });
        else if (e.type == MidiEvent::Type::NoteOff)
            for (auto it = out.rbegin(); it != out.rend(); ++it)
                if (it->midi == e.data1 && it->offset < 0.0)
                {
                    it->offset = t;
                    break;
                }
    }
    return out;
}

struct Score
{
    int truthCount = 0;
    int detectedCount = 0;
    int hits = 0;
    int octaveErrors = 0;   // false notes exactly +-12 from a truth note sounding at the time
    int otherFalse = 0;     // any other false note
    int passing = 0;        // intermediate notes inside an intended slide / portamento
    int missed = 0;
    std::vector<double> latenciesMs;
    std::vector<double> offsetErrMs;

    double recall() const { return truthCount ? static_cast<double>(hits) / truthCount : 1.0; }
    int falseNotes() const { return octaveErrors + otherFalse; }
    double median() const { return pct(latenciesMs, 0.5); }
    double p90() const { return pct(latenciesMs, 0.9); }

    static double pct(std::vector<double> v, double p)
    {
        if (v.empty())
            return 0.0;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())))];
    }
};

// A truth note is "hit" by a detected note of the same pitch whose onset lands between 30 ms before
// and 350 ms after the true onset (and before the true note ends). Everything left over is false.
inline Score score(const Scenario& sc, const std::vector<DetectedNote>& det)
{
    Score s;
    s.truthCount = static_cast<int>(sc.truth.size());
    s.detectedCount = static_cast<int>(det.size());

    std::vector<bool> used(det.size(), false);
    for (const auto& t : sc.truth)
    {
        int best = -1;
        for (size_t i = 0; i < det.size(); ++i)
        {
            if (used[i] || det[i].midi != t.midi)
                continue;
            const double dt = det[i].onset - t.onset;
            if (dt < -0.030 || dt > 0.350 || det[i].onset > t.offset)
                continue;
            if (best < 0 || det[i].onset < det[static_cast<size_t>(best)].onset)
                best = static_cast<int>(i);
        }
        if (best >= 0)
        {
            used[static_cast<size_t>(best)] = true;
            ++s.hits;
            s.latenciesMs.push_back((det[static_cast<size_t>(best)].onset - t.onset) * 1000.0);
            if (det[static_cast<size_t>(best)].offset >= 0.0)
                s.offsetErrMs.push_back((det[static_cast<size_t>(best)].offset - t.offset) * 1000.0);
        }
        else
            ++s.missed;
    }

    for (size_t i = 0; i < det.size(); ++i)
    {
        if (used[i])
            continue;
        bool pass = false;
        for (const auto& w : sc.passing)
            if (det[i].onset >= w.t0 && det[i].onset <= w.t1 && det[i].midi > w.lo && det[i].midi < w.hi)
                pass = true;
        if (pass)
        {
            ++s.passing;
            continue;
        }

        bool octave = false;
        for (const auto& t : sc.truth)
            if (det[i].onset >= t.onset - 0.03 && det[i].onset <= t.offset + 0.35 && std::abs(det[i].midi - t.midi) % 12 == 0
                && det[i].midi != t.midi)
                octave = true;
        (octave ? s.octaveErrors : s.otherFalse)++;
    }
    return s;
}

} // namespace nota::test
