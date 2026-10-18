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

    // The scale covers velocityRangeDb under the full level, not the whole gate-to-full distance: ordinary playing
    // (a few dB below the top) must not sit at the top, and a note 30+ dB down is near the bottom.
    CHECK(velocityOf(0.2, def) < 110, "a note 14 dB under full should not be near 127 (%d)", velocityOf(0.2, def));
    CHECK(velocityOf(0.02, def) < 45, "a note 30 dB under full should be near the bottom (%d)", velocityOf(0.02, def));
    Params wide = def;
    wide.velocityRangeDb = 55.0f;
    CHECK(velocityOf(0.2, wide) > velocityOf(0.2, def), "a wider range should raise mid-level velocity");

    // Auto level: the full level follows the loudest recent attack, so a hot input and a quiet one both use the range.
    {
        const double fs = 48000.0;
        auto secondOf = [&](double loud, double soft, bool autoLevel) {
            Params p = def;
            p.velocityAuto = autoLevel;
            Engine engine;
            engine.prepare(fs);
            engine.setParams(p);
            NoteSpec a, b;
            a.midi = 38; a.start = 0.1; a.dur = 0.4; a.gain = loud;
            b.midi = 43; b.start = 0.8; b.dur = 0.4; b.gain = soft;
            const auto ons = notesOn(runEngine(engine, renderSequence(fs, 1.4, { a, b }, Timbre::BassPluck)));
            return ons.size() == 2 ? std::make_pair(static_cast<int>(ons[0].data2), static_cast<int>(ons[1].data2)) : std::make_pair(-1, -1);
        };
        const auto fixedHot = secondOf(1.0, 0.25, false);   // hot track: both notes above the fixed full level
        const auto autoHot = secondOf(1.0, 0.25, true);
        CHECK(fixedHot.first >= 125 && fixedHot.second >= 110, "fixed full level: hot input should saturate (%d, %d)", fixedHot.first, fixedHot.second);
        CHECK(autoHot.second < fixedHot.second - 15, "auto: the softer note should land well below the loud one (%d vs fixed %d)", autoHot.second, fixedHot.second);
        CHECK(autoHot.first >= 120, "auto: the loudest note is at the top (%d)", autoHot.first);
        // The auto level never falls below velocityFullDb (that dial is the way to adapt to a very quiet source).
        const auto quietAuto = secondOf(0.04, 0.04, true), quietFixed = secondOf(0.04, 0.04, false);
        CHECK(quietAuto == quietFixed, "auto: a quiet input behaves like the fixed full level (%d,%d vs %d,%d)", quietAuto.first, quietAuto.second,
              quietFixed.first, quietFixed.second);
    }
}

// ---- MPE --------------------------------------------------------------------------------------

struct MpeRun
{
    std::vector<MidiEvent> events;
    std::vector<long long> samples;
};

static MpeRun runMpe(const Params& p, const std::vector<float>& audio, double fs = 48000.0)
{
    Engine engine;
    engine.prepare(fs);
    engine.setParams(p);
    MpeRun r;
    EventList list;
    for (size_t pos = 0; pos < audio.size(); pos += 128)
    {
        const int n = static_cast<int>(std::min<size_t>(128, audio.size() - pos));
        list.clear();
        engine.process(audio.data() + pos, n, list);
        for (int i = 0; i < list.count; ++i)
        {
            r.events.push_back(list.events[i]);
            r.samples.push_back(static_cast<long long>(pos) + list.events[i].sampleOffset);
        }
    }
    return r;
}

static int lastValue(const MpeRun& r, MidiEvent::Type t, int ccNumber = -1)
{
    int v = -1;
    for (const auto& e : r.events)
        if (e.type == t && (ccNumber < 0 || e.data1 == ccNumber))
            v = t == MidiEvent::Type::PitchBend ? e.bend : (t == MidiEvent::Type::Cc ? e.data2 : e.data1);
    return v;
}

static int countOf(const MpeRun& r, MidiEvent::Type t, int ccNumber = -1)
{
    int n = 0;
    for (const auto& e : r.events)
        n += e.type == t && (ccNumber < 0 || e.data1 == ccNumber);
    return n;
}

static void testMpe()
{
    std::printf("mpe\n");
    const double fs = 48000.0;
    Params mpe;
    mpe.mpe = true;

    // A note 30 cents sharp: note number is the nearest semitone, the rest is that note's own bend.
    {
        NoteSpec n;
        n.midi = 40.3;
        n.start = 0.1;
        n.dur = 0.5;
        const auto r = runMpe(mpe, renderSequence(fs, 0.8, { n }, Timbre::BassPluck));
        int onIdx = -1;
        for (size_t i = 0; i < r.events.size(); ++i)
            if (r.events[i].type == MidiEvent::Type::NoteOn)
                onIdx = static_cast<int>(i);
        CHECK(onIdx >= 0, "no note-on");
        if (onIdx >= 0)
        {
            const auto& on = r.events[static_cast<size_t>(onIdx)];
            CHECK(on.data1 == 40, "note %d", on.data1);
            CHECK(on.channel >= 2 && on.channel <= 16, "channel %d is not a member channel", on.channel);
            bool bendBefore = false, pressureBefore = false, slideBefore = false;
            for (int i = 0; i < onIdx; ++i)
            {
                CHECK(r.events[static_cast<size_t>(i)].channel == on.channel, "expression on channel %d, note on %d",
                      r.events[static_cast<size_t>(i)].channel, on.channel);
                bendBefore |= r.events[static_cast<size_t>(i)].type == MidiEvent::Type::PitchBend;
                pressureBefore |= r.events[static_cast<size_t>(i)].type == MidiEvent::Type::ChannelPressure;
                slideBefore |= r.events[static_cast<size_t>(i)].type == MidiEvent::Type::Cc;
            }
            CHECK(bendBefore && pressureBefore && slideBefore, "expression must precede the note-on (%d %d %d)", bendBefore, pressureBefore, slideBefore);
            const int expected = 8192 + static_cast<int>(0.3 / 48.0 * 8191.0);
            const int bend = lastValue(r, MidiEvent::Type::PitchBend);
            CHECK(std::abs(bend - expected) < 45, "bend %d, expected about %d (0.3 st of 48)", bend, expected);
        }
        // Note-off lands on the note's own channel.
        for (const auto& e : r.events)
            if (e.type == MidiEvent::Type::NoteOff)
                CHECK(e.channel >= 2, "note-off on channel %d", e.channel);
    }

    // Successive notes rotate through the member channels.
    {
        std::vector<NoteSpec> notes;
        for (int i = 0; i < 4; ++i)
            notes.push_back({ 33.0 + 2 * i, 0.1 + 0.4 * i, 0.3, 0.0, 0.5 });
        const auto r = runMpe(mpe, renderSequence(fs, 2.0, notes, Timbre::BassPluck));
        std::vector<int> chans;
        for (const auto& e : r.events)
            if (e.type == MidiEvent::Type::NoteOn)
                chans.push_back(e.channel);
        CHECK(chans.size() == 4, "expected 4 notes, got %zu", chans.size());
        for (size_t i = 1; i < chans.size(); ++i)
            CHECK(chans[i] != chans[i - 1], "notes %zu and %zu share channel %d", i - 1, i, chans[i]);
    }

    // A smooth glide is one note carrying a bend, not a string of new notes.
    {
        std::vector<Seg> segs(2);
        segs[0] = { 33.0, 0.1, 0.5, false, 0.0, 0.5 };
        segs[1] = { 38.0, 0.6, 0.6, true, 250.0, 0.5 }; // legato slide up 5 semitones
        const auto sc = render("glide", fs, Timbre::BassPluck, segs, {});
        const auto r = runMpe(mpe, sc.audio);
        CHECK(countOf(r, MidiEvent::Type::NoteOn) == 1, "glide produced %d notes", countOf(r, MidiEvent::Type::NoteOn));
        const int bend = lastValue(r, MidiEvent::Type::PitchBend);
        const int expected = 8192 + static_cast<int>(5.0 / 48.0 * 8191.0);
        CHECK(std::abs(bend - expected) < 160, "glide ended at bend %d, expected about %d", bend, expected);

        // The same glide in standard mode is allowed to retrigger (it is not bend-carrying).
        Params std_;
        const auto rs = runMpe(std_, sc.audio);
        for (const auto& e : rs.events)
            CHECK(e.channel == 0, "standard mode stamped a channel (%d)", e.channel);
    }

    // Vibrato stays one note and shows up as bend.
    {
        NoteSpec n;
        n.midi = 57.0;
        n.start = 0.1;
        n.dur = 0.8;
        n.vibratoCents = 60.0;
        const auto r = runMpe(mpe, renderSequence(fs, 1.1, { n }, Timbre::Voice));
        CHECK(countOf(r, MidiEvent::Type::NoteOn) == 1, "vibrato note produced %d note-ons", countOf(r, MidiEvent::Type::NoteOn));
        CHECK(countOf(r, MidiEvent::Type::PitchBend) >= 20, "only %d bend messages for a vibrato", countOf(r, MidiEvent::Type::PitchBend));
    }

    // Pressure follows level.
    {
        NoteSpec loud, quiet;
        loud.midi = quiet.midi = 38.0;
        loud.start = quiet.start = 0.1;
        loud.dur = quiet.dur = 0.5;
        loud.gain = 0.9;
        quiet.gain = 0.04;
        const int pl = lastValue(runMpe(mpe, renderSequence(fs, 0.8, { loud }, Timbre::BassPluck)), MidiEvent::Type::ChannelPressure);
        const int pq = lastValue(runMpe(mpe, renderSequence(fs, 0.8, { quiet }, Timbre::BassPluck)), MidiEvent::Type::ChannelPressure);
        CHECK(pl >= 0 && pq >= 0 && pl > pq + 15, "pressure loud %d vs quiet %d", pl, pq);
    }

    // Slide (CC74) follows brightness: a sawtooth is brighter than a sine at the same level.
    {
        std::vector<float> sine(static_cast<size_t>(fs * 0.8)), saw(sine.size());
        for (size_t i = 0; i < sine.size(); ++i)
        {
            const double t = static_cast<double>(i) / fs;
            const double env = t < 0.1 ? 0.0 : 1.0;
            sine[i] = static_cast<float>(0.3 * env * std::sin(2.0 * kPi * 110.0 * t));
            double v = 0.0;
            for (int h = 1; h <= 25; ++h)
                v += std::sin(2.0 * kPi * 110.0 * h * t) / h;
            saw[i] = static_cast<float>(0.25 * env * v);
        }
        const int cs = lastValue(runMpe(mpe, sine), MidiEvent::Type::Cc, 74);
        const int cw = lastValue(runMpe(mpe, saw), MidiEvent::Type::Cc, 74);
        CHECK(cs >= 0 && cw >= 0 && cw > cs + 30, "slide: sine %d, saw %d", cs, cw);
    }

    // Each expression can be switched off.
    {
        NoteSpec n;
        n.midi = 38.0;
        n.start = 0.1;
        n.dur = 0.5;
        const auto audio = renderSequence(fs, 0.8, { n }, Timbre::BassPluck);
        Params noP = mpe, noS = mpe;
        noP.mpePressure = false;
        noS.mpeSlide = false;
        CHECK(countOf(runMpe(noP, audio), MidiEvent::Type::ChannelPressure) == 0, "pressure not off");
        CHECK(countOf(runMpe(noS, audio), MidiEvent::Type::Cc, 74) == 0, "slide not off");
    }
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
    testMpe();
    testScenarioSuite();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
