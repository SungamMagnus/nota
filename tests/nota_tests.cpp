#include "Fft.h"
#include "Eval.h"
#include "PitchTracker.h"
#include "TestSignals.h"

#include <cmath>
#include <complex>
#include <cstdio>

using namespace nota;
using namespace nota::test;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_failures;                                                         \
            std::printf("  FAIL %s:%d  %s  -- ", __FILE__, __LINE__, #cond);      \
            std::printf(__VA_ARGS__);                                             \
            std::printf("\n");                                                    \
        }                                                                         \
    } while (0)

static std::vector<AbsEvent> notesOn(const std::vector<AbsEvent>& ev)
{
    std::vector<AbsEvent> r;
    for (const auto& e : ev)
        if (e.type == MidiEvent::Type::NoteOn)
            r.push_back(e);
    return r;
}

// ---------------------------------------------------------------------------------

static void testFft()
{
    std::printf("fft\n");
    const int n = 64;
    Fft fft;
    fft.init(n);

    std::vector<float> re(n), im(n, 0.0f);
    std::vector<std::complex<double>> ref(n);
    Rng rng;
    for (int i = 0; i < n; ++i)
        re[static_cast<size_t>(i)] = rng.uni();

    for (int k = 0; k < n; ++k)
    {
        std::complex<double> acc = 0;
        for (int i = 0; i < n; ++i)
            acc += static_cast<double>(re[static_cast<size_t>(i)]) * std::polar(1.0, -2.0 * kPi * k * i / n);
        ref[static_cast<size_t>(k)] = acc;
    }

    std::vector<float> re0 = re;
    fft.transform(re.data(), im.data(), false);
    double maxErr = 0;
    for (int k = 0; k < n; ++k)
        maxErr = std::max(maxErr, std::abs(std::complex<double>(re[static_cast<size_t>(k)], im[static_cast<size_t>(k)]) - ref[static_cast<size_t>(k)]));
    CHECK(maxErr < 1e-4, "forward error %g", maxErr);

    fft.transform(re.data(), im.data(), true);
    double rt = 0;
    for (int i = 0; i < n; ++i)
        rt = std::max(rt, static_cast<double>(std::abs(re[static_cast<size_t>(i)] - re0[static_cast<size_t>(i)])));
    CHECK(rt < 1e-5, "round trip error %g", rt);
}

// Steady-state pitch accuracy of the tracker alone.
static void testTrackerAccuracy()
{
    std::printf("tracker accuracy\n");
    const double fs = 48000.0;
    for (Timbre timbre : { Timbre::BassPluck, Timbre::Voice })
    {
        const int lo = timbre == Timbre::Voice ? 45 : 23;
        const int hi = timbre == Timbre::Voice ? 84 : 67;
        for (int m = lo; m <= hi; m += 3)
        {
            Engine engine;
            engine.prepare(fs);
            NoteSpec n;
            n.midi = m + 0.25; // 25 cents sharp, so we can see the fraction
            n.start = 0.0;
            n.dur = 1.0;
            const auto audio = renderSequence(fs, 1.0, { n }, timbre);
            EventList scratch;
            engine.process(audio.data(), static_cast<int>(audio.size() * 0.7), scratch);
            const PitchFrame& f = engine.lastFrame();
            const double cents = 1200.0 * std::log2(f.freqHz / midiToHz(m + 0.25));
            CHECK(f.voiced, "midi %d not voiced", m);
            CHECK(std::abs(cents) < 15.0, "midi %d off by %.1f cents (%.2f Hz)", m, cents, f.freqHz);
        }
    }
}

// Every note across both ranges is found, exactly once, at an acceptable latency.
static void testSingleNotes()
{
    std::printf("single notes\n");
    struct Case { Timbre t; int lo, hi, step; double vib; };
    const Case cases[] = {
        { Timbre::BassPluck, 23, 67, 2, 0.0 },
        { Timbre::BassStrongH2, 28, 60, 4, 0.0 },
        { Timbre::Voice, 43, 84, 3, 30.0 },
    };

    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto& c : cases)
        {
            for (int m = c.lo; m <= c.hi; m += c.step)
            {
                Engine engine;
                engine.prepare(fs);
                NoteSpec n;
                n.midi = m;
                n.start = 0.2;
                n.dur = 0.7;
                n.vibratoCents = c.vib;
                const auto audio = renderSequence(fs, 1.2, { n }, c.t);
                const auto ev = runEngine(engine, audio);
                const auto ons = notesOn(ev);

                CHECK(ons.size() == 1, "fs %.0f timbre %d midi %d: %zu note-ons", fs, static_cast<int>(c.t), m, ons.size());
                if (!ons.empty())
                {
                    CHECK(ons[0].data1 == m, "fs %.0f timbre %d: expected %d got %d", fs, static_cast<int>(c.t), m, ons[0].data1);
                    const double latMs = (ons[0].sample / fs - n.start) * 1000.0;
                    const double periodMs = 1000.0 / midiToHz(m);
                    // ~2 periods to see the repetition + confirmation hops + a little slack.
                    const double budget = 3.0 * periodMs + 14.0;
                    CHECK(latMs <= budget, "midi %d latency %.1f ms > %.1f ms", m, latMs, budget);
                }

                int offs = 0;
                for (const auto& e : ev)
                    offs += e.type == MidiEvent::Type::NoteOff;
                CHECK(offs == 1, "midi %d: %d note-offs", m, offs);
            }
        }
    }
}

static void testSequence()
{
    std::printf("legato sequence + repeated note\n");
    const double fs = 48000.0;
    // E1 G1 A1 A1(repeat) D2 octave-up E2: bass line with a gap-less run and a re-pluck.
    std::vector<NoteSpec> notes;
    const int midi[] = { 28, 31, 33, 33, 38, 40 };
    double t = 0.1;
    for (int m : midi)
    {
        notes.push_back({ static_cast<double>(m), t, 0.38, 0.0, 0.5 });
        t += 0.40;
    }
    const auto audio = renderSequence(fs, t + 0.3, notes, Timbre::BassPluck);

    Engine engine;
    engine.prepare(fs);
    const auto ons = notesOn(runEngine(engine, audio));

    CHECK(ons.size() == 6, "expected 6 note-ons, got %zu", ons.size());
    for (size_t i = 0; i < std::min<size_t>(6, ons.size()); ++i)
        CHECK(ons[i].data1 == midi[i], "note %zu: expected %d got %d", i, midi[i], ons[i].data1);
}

static void testSilenceAndNoise()
{
    std::printf("silence / noise\n");
    const double fs = 48000.0;
    Engine engine;
    engine.prepare(fs);

    std::vector<float> silence(static_cast<size_t>(fs), 0.0f);
    CHECK(runEngine(engine, silence).empty(), "silence produced events");

    // Quiet broadband noise well under the gate.
    auto noise = renderSequence(fs, 2.0, {}, Timbre::BassPluck, -62.0f);
    CHECK(runEngine(engine, noise).empty(), "quiet noise produced events");

    // Hum-level 50 Hz at -66 dB must not trigger either.
    std::vector<float> hum(static_cast<size_t>(fs * 2));
    for (size_t i = 0; i < hum.size(); ++i)
        hum[i] = 0.0005f * static_cast<float>(std::sin(2.0 * kPi * 50.0 * static_cast<double>(i) / fs));
    CHECK(runEngine(engine, hum).empty(), "hum produced events");
}

static void testPitchBend()
{
    std::printf("pitch bend\n");
    const double fs = 48000.0;
    Params p;
    p.pitchBend = true;
    p.bendRange = 2.0f;

    Engine engine;
    engine.prepare(fs);
    engine.setParams(p);

    // A voice note that scoops 50 cents sharp over its length: one note, bends rise.
    NoteSpec n;
    n.midi = 57.0;
    n.start = 0.1;
    n.dur = 0.9;
    auto audio = renderSequence(fs, 0.3, { n }, Timbre::Voice);
    // Re-render with a +40 cent offset for the second half so we get a measurable bend.
    NoteSpec n2 = n;
    n2.midi = 57.4;
    n2.start = 0.6;
    n2.dur = 0.4;
    audio = renderSequence(fs, 1.2, { NoteSpec { 57.0, 0.1, 0.5, 0.0, 0.5 }, NoteSpec { 57.4, 0.5, 0.6, 0.0, 0.5 } }, Timbre::Voice);

    const auto ev = runEngine(engine, audio);
    int bends = 0, maxBend = 8192;
    for (const auto& e : ev)
        if (e.type == MidiEvent::Type::PitchBend)
        {
            ++bends;
            maxBend = std::max(maxBend, e.bend);
        }
    CHECK(bends >= 1, "no bend events");
    CHECK(notesOn(ev).size() <= 2, "too many notes: %zu", notesOn(ev).size());
}

static int velocityOf(double gain, const Params& p, Timbre t = Timbre::BassPluck, int midi = 38)
{
    const double fs = 48000.0;
    Engine engine;
    engine.prepare(fs);
    engine.setParams(p);
    NoteSpec n;
    n.midi = midi;
    n.start = 0.1;
    n.dur = 0.5;
    n.gain = gain;
    const auto ons = notesOn(runEngine(engine, renderSequence(fs, 0.8, { n }, t)));
    return ons.empty() ? -1 : ons[0].data2;
}

static void testVelocity()
{
    std::printf("velocity\n");
    const double gains[] = { 0.02, 0.05, 0.12, 0.3, 0.8 };

    // Louder playing -> higher velocity, never decreasing, and a real spread across the range.
    Params def;
    int prev = 0;
    for (double g : gains)
    {
        const int v = velocityOf(g, def);
        CHECK(v >= prev, "gain %.2f: velocity %d fell below %d", g, v, prev);
        CHECK(v >= def.velocityMin && v <= def.velocityMax, "gain %.2f: velocity %d outside %d..%d", g, v, def.velocityMin, def.velocityMax);
        prev = v;
    }
    CHECK(velocityOf(0.8, def) - velocityOf(0.02, def) >= 40, "velocity spread too small: %d..%d", velocityOf(0.02, def), velocityOf(0.8, def));

    // The range control clamps the output.
    Params narrow;
    narrow.velocityMin = 60;
    narrow.velocityMax = 90;
    for (double g : gains)
    {
        const int v = velocityOf(g, narrow);
        CHECK(v >= 60 && v <= 90, "narrow range: gain %.2f gave %d", g, v);
    }

    // min == max is a fixed velocity; min > max is treated as the same range, not an error.
    Params fixed;
    fixed.velocityMin = fixed.velocityMax = 77;
    CHECK(velocityOf(0.03, fixed) == 77 && velocityOf(0.8, fixed) == 77, "fixed velocity not fixed");
    Params swapped;
    swapped.velocityMin = 100;
    swapped.velocityMax = 40;
    const int sv = velocityOf(0.3, swapped);
    CHECK(sv >= 40 && sv <= 100, "swapped range gave %d", sv);

    // The same holds for a voice, and for a lower 'full level' (more sensitive) the velocity rises.
    Params sens;
    sens.velocityFullDb = -30.0f;
    CHECK(velocityOf(0.12, sens) > velocityOf(0.12, def), "lower full level should raise velocity (%d vs %d)",
          velocityOf(0.12, sens), velocityOf(0.12, def));
    CHECK(velocityOf(0.5, def, Timbre::Voice, 57) > velocityOf(0.03, def, Timbre::Voice, 57), "voice velocity does not follow level");
}

// The hard-scenario suite (tools/Scenarios.h): legato, slides, distortion, hum, reverb, vibrato...
// These are floors, not targets - tighten them when the tracker improves.
static void testScenarioSuite()
{
    std::printf("hard scenario suite\n");
    const double fs = 48000.0;
    int truth = 0, hits = 0, octave = 0, other = 0;
    std::vector<double> lat;
    for (const auto& sc : buildSuite(fs))
    {
        Engine engine;
        engine.prepare(fs);
        const Score s = score(sc, toNotes(runEngine(engine, sc.audio), fs));
        truth += s.truthCount;
        hits += s.hits;
        octave += s.octaveErrors;
        other += s.otherFalse;
        lat.insert(lat.end(), s.latenciesMs.begin(), s.latenciesMs.end());
        CHECK(s.hits == s.truthCount, "%s: %d/%d notes found", sc.name.c_str(), s.hits, s.truthCount);
        CHECK(s.octaveErrors == 0, "%s: %d octave errors", sc.name.c_str(), s.octaveErrors);
        CHECK(s.otherFalse <= 2, "%s: %d false notes", sc.name.c_str(), s.otherFalse);
    }
    CHECK(other <= 4, "total false notes %d", other);
    CHECK(Score::pct(lat, 0.5) <= 55.0, "median latency %.1f ms", Score::pct(lat, 0.5));
    CHECK(Score::pct(lat, 0.9) <= 95.0, "p90 latency %.1f ms", Score::pct(lat, 0.9));
    (void) truth; (void) hits; (void) octave;
}

int main()
{
    testFft();
    testTrackerAccuracy();
    testSingleNotes();
    testSequence();
    testSilenceAndNoise();
    testPitchBend();
    testVelocity();
    testScenarioSuite();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
