// Sweeps single notes across the bass and voice ranges and reports the note-on
// latency, wrong/extra notes and CPU cost. This is the yardstick for every change
// to the tracker.

#include "TestSignals.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace nota;
using namespace nota::test;

struct Row
{
    int midi;
    double latencyMs;
    int firstNote;
    int noteOns;
};

static Row runOne(double fs, Timbre timbre, int midi, double vibratoCents, const Params& p)
{
    Engine engine;
    engine.prepare(fs);
    engine.setParams(p);

    NoteSpec n;
    n.midi = midi;
    n.start = 0.2;
    n.dur = 0.8;
    n.vibratoCents = vibratoCents;
    const auto audio = renderSequence(fs, 1.3, { n }, timbre);
    const auto events = runEngine(engine, audio);

    Row r { midi, -1.0, -1, 0 };
    for (const auto& e : events)
    {
        if (e.type != MidiEvent::Type::NoteOn)
            continue;
        if (r.noteOns++ == 0)
        {
            r.firstNote = e.data1;
            r.latencyMs = (static_cast<double>(e.sample) / fs - n.start) * 1000.0;
        }
    }
    return r;
}

static void sweep(double fs, const char* name, Timbre timbre, int lo, int hi, int step, double vib, const Params& p)
{
    std::printf("\n== %s  (fs %.0f, vibrato %.0f cents, response %.2f)\n", name, fs, vib, p.response);
    std::printf("%5s %8s %9s %8s %7s  %s\n", "midi", "Hz", "lat(ms)", "periods", "notes", "result");

    std::vector<double> lats;
    int ok = 0, total = 0;
    for (int m = lo; m <= hi; m += step)
    {
        const Row r = runOne(fs, timbre, m, vib, p);
        const double hz = midiToHz(m);
        const bool correct = r.firstNote == m && r.noteOns == 1;
        ++total;
        if (correct)
        {
            ++ok;
            lats.push_back(r.latencyMs);
        }
        std::printf("%5d %8.1f %9.1f %8.2f %7d  %s", m, hz, r.latencyMs, r.latencyMs * hz / 1000.0, r.noteOns,
                    correct ? "ok" : "WRONG");
        if (!correct)
            std::printf("  (first note %d)", r.firstNote);
        std::printf("\n");
    }

    std::sort(lats.begin(), lats.end());
    if (!lats.empty())
        std::printf("-- %d/%d correct, latency median %.1f ms, p90 %.1f ms, max %.1f ms\n", ok, total,
                    lats[lats.size() / 2], lats[static_cast<size_t>(static_cast<double>(lats.size()) * 0.9)], lats.back());
    else
        std::printf("-- 0/%d correct\n", total);
}

int main(int argc, char** argv)
{
    double fs = 48000.0;
    Params p;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--fs") && i + 1 < argc)
            fs = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--response") && i + 1 < argc)
            p.response = static_cast<float>(std::atof(argv[++i]));
    }

    sweep(fs, "bass pluck", Timbre::BassPluck, 23, 67, 2, 0.0, p);
    sweep(fs, "bass, strong 2nd harmonic", Timbre::BassStrongH2, 28, 60, 4, 0.0, p);
    sweep(fs, "voice, 30 cent vibrato", Timbre::Voice, 43, 84, 3, 30.0, p);

    // CPU: 20 s of voice at the requested rate.
    {
        std::vector<NoteSpec> notes;
        for (int i = 0; i < 40; ++i)
            notes.push_back({ 50.0 + (i * 7) % 24, i * 0.5, 0.45, 25.0, 0.5 });
        const auto audio = renderSequence(fs, 20.0, notes, Timbre::Voice);

        Engine engine;
        engine.prepare(fs);
        const auto t0 = std::chrono::steady_clock::now();
        runEngine(engine, audio, 64);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\nCPU: %.2f s of audio in %.3f s  ->  %.2f%% of one core (block 64)\n", 20.0, secs, secs / 20.0 * 100.0);
    }
    return 0;
}
