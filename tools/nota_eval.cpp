// Scores the engine on the hard-scenario suite (legato, slides, distortion, noise, reverb, ...).
//   nota_eval [--fs 48000] [--response r] [--verbose]
// The same suite will score the neural path later, so numbers here are the baseline to beat.

#include "Eval.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace nota;
using namespace nota::test;

int main(int argc, char** argv)
{
    double fs = 48000.0;
    bool verbose = false;
    Params p;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--fs") && i + 1 < argc)
            fs = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--response") && i + 1 < argc)
            p.response = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--verbose"))
            verbose = true;
    }

    const auto suite = buildSuite(fs);
    std::printf("%-40s %5s %5s %5s %5s %5s %5s %8s %8s\n", "scenario", "notes", "hit", "miss", "oct", "false", "pass", "lat med", "lat p90");

    int tTruth = 0, tHit = 0, tOct = 0, tFalse = 0;
    std::vector<double> allLat;
    for (const auto& sc : suite)
    {
        Engine engine;
        engine.prepare(fs);
        engine.setParams(p);
        const auto det = toNotes(runEngine(engine, sc.audio), fs);
        const Score s = score(sc, det);

        std::printf("%-40s %5d %5d %5d %5d %5d %5d %7.1fms %7.1fms\n", sc.name.c_str(), s.truthCount, s.hits, s.missed, s.octaveErrors,
                    s.otherFalse, s.passing, s.median(), s.p90());

        if (verbose)
        {
            std::printf("    truth:");
            for (const auto& t : sc.truth)
                std::printf(" %d@%.2f", t.midi, t.onset);
            std::printf("\n    got:  ");
            for (const auto& d : det)
                std::printf(" %d@%.2f", d.midi, d.onset);
            std::printf("\n");
        }

        tTruth += s.truthCount;
        tHit += s.hits;
        tOct += s.octaveErrors;
        tFalse += s.otherFalse;
        allLat.insert(allLat.end(), s.latenciesMs.begin(), s.latenciesMs.end());
    }

    std::printf("\nTOTAL  recall %d/%d (%.1f%%)   octave errors %d   other false notes %d   latency median %.1f ms  p90 %.1f ms\n", tHit,
                tTruth, 100.0 * tHit / std::max(1, tTruth), tOct, tFalse, Score::pct(allLat, 0.5), Score::pct(allLat, 0.9));
    return 0;
}
