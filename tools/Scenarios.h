#pragma once

// Harder, phrase-level synthetic material with note-level ground truth: legato, hammer-ons,
// slides, distortion, hum/noise, reverb, portamento, consonant gaps, wide vibrato, falsetto.
// Deterministic. Used by nota_eval to score the tracker.

#include "TestSignals.h"

#include <functional>
#include <string>

namespace nota::test
{

struct Seg
{
    double midi = 40.0;
    double start = 0.0;
    double dur = 0.3;
    bool legato = false;     // keep ringing / keep phase: a hammer-on or sung legato, no new attack
    double glideMs = 0.0;    // pitch moves to the target over this long (slide / portamento)
    double gain = 0.5;
    double gapStart = -1.0;  // voice: an unvoiced (fricative) gap inside the note, seconds from note start
    double gapDur = 0.0;
    bool dead = false;       // bass: percussive dead note - noise thump, no pitch
};

struct TruthNote
{
    int midi;
    double onset;
    double offset;
};

// Pitches strictly between lo and hi sounding in [t0, t1] are the intended glide, not errors.
struct PassingWindow
{
    double t0, t1;
    int lo, hi;
};

struct Scenario
{
    std::string name;
    std::vector<float> audio;
    std::vector<TruthNote> truth;
    std::vector<PassingWindow> passing;
    double fs = 48000.0;
};

struct Fx
{
    double driveDb = 0.0;
    double humDb = -200.0;
    double hissDb = -90.0;
    double reverbMix = 0.0;   // 0..1
    double reverbRt60 = 0.6;
    double vibCents = 0.0;
    double vibHz = 5.5;
    bool breathy = false;     // voice: more aspiration noise, fewer harmonics (falsetto-like)
    double fretNoiseDb = -200.0; // bass: noise burst at every attack
};

// A tiny Schroeder reverb: 4 combs + 2 allpasses.
inline void applyReverb(std::vector<float>& x, double fs, double mix, double rt60)
{
    if (mix <= 0.0)
        return;
    const int combLen[4] = { 1557, 1617, 1491, 1422 };
    const int apLen[2] = { 225, 556 };
    std::vector<float> wet(x.size(), 0.0f);
    for (int c = 0; c < 4; ++c)
    {
        const int len = static_cast<int>(combLen[c] * fs / 44100.0);
        const float g = std::pow(10.0f, -3.0f * static_cast<float>(len) / static_cast<float>(fs * rt60));
        std::vector<float> buf(static_cast<size_t>(len), 0.0f);
        size_t pos = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const float y = buf[pos];
            buf[pos] = x[i] + g * y;
            wet[i] += 0.25f * y;
            pos = (pos + 1) % buf.size();
        }
    }
    for (int a = 0; a < 2; ++a)
    {
        const int len = static_cast<int>(apLen[a] * fs / 44100.0);
        std::vector<float> buf(static_cast<size_t>(len), 0.0f);
        size_t pos = 0;
        for (size_t i = 0; i < wet.size(); ++i)
        {
            const float in = wet[i];
            const float b = buf[pos];
            const float out = -0.5f * in + b;
            buf[pos] = in + 0.5f * out;
            wet[i] = out;
            pos = (pos + 1) % buf.size();
        }
    }
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>((1.0 - 0.5 * mix) * x[i] + mix * wet[i] * 1.5);
}

inline Scenario render(const std::string& name, double fs, Timbre timbre, const std::vector<Seg>& segs, const Fx& fx,
                       double tail = 0.4)
{
    Scenario sc;
    sc.name = name;
    sc.fs = fs;

    double end = 0.0;
    for (const auto& s : segs)
        end = std::max(end, s.start + s.dur);
    std::vector<float> buf(static_cast<size_t>((end + tail) * fs), 0.0f);

    Rng rng, rng2;
    rng2.s = 99;
    double phase = 0.0, m = segs.empty() ? 40.0 : segs[0].midi;
    double prevMidi = m;
    double tPluck = 10.0; // time since last pluck (bass), seconds
    double voiceEnv = 0.0;

    for (size_t k = 0; k < segs.size(); ++k)
    {
        const Seg& sg = segs[k];
        const int i0 = static_cast<int>(sg.start * fs);
        const int len = static_cast<int>(sg.dur * fs);

        if (!sg.dead)
        {
            if (sg.gapStart >= 0.0 && sg.gapDur >= 0.05)
            {
                // A long fricative ("s") ends the note: the vowel after it is a new syllable.
                sc.truth.push_back({ static_cast<int>(std::lround(sg.midi)), sg.start, sg.start + sg.gapStart });
                sc.truth.push_back({ static_cast<int>(std::lround(sg.midi)), sg.start + sg.gapStart + sg.gapDur, sg.start + sg.dur });
            }
            else
                sc.truth.push_back({ static_cast<int>(std::lround(sg.midi)), sg.start, sg.start + sg.dur });
            const double glide = std::max(sg.glideMs, timbre == Timbre::Voice && sg.legato ? 45.0 : 0.0);
            if (sg.legato && glide > 0.0 && std::abs(prevMidi - sg.midi) > 1.5)
                sc.passing.push_back({ sg.start, sg.start + glide / 1000.0 * 2.5 + 0.06,
                                       static_cast<int>(std::min(prevMidi, sg.midi)), static_cast<int>(std::max(prevMidi, sg.midi)) });
            prevMidi = sg.midi;
        }

        if (!sg.legato)
        {
            tPluck = 0.0;
            if (timbre != Timbre::Voice && !sg.glideMs)
                m = sg.midi;
        }
        const double glideTau = std::max(sg.glideMs, timbre == Timbre::Voice && sg.legato ? 45.0 : 0.0) / 3000.0;
        double lastFret = 0.0;

        for (int i = 0; i < len && i0 + i < static_cast<int>(buf.size()); ++i)
        {
            const double t = i / fs;
            if (glideTau > 0.0)
                m += (sg.midi - m) * (1.0 - std::exp(-1.0 / (glideTau * fs)));
            else
                m = sg.midi;

            double mm = m;
            if (fx.vibCents != 0.0 && t > 0.15)
                mm += fx.vibCents / 100.0 * std::sin(2.0 * kPi * fx.vibHz * (t - 0.15));
            const double f0 = midiToHz(mm);
            phase += 2.0 * kPi * f0 / fs;
            tPluck += 1.0 / fs;

            double s = 0.0;
            if (sg.dead)
            {
                // Dead note: damped noise thump with a bit of low rumble, no stable pitch.
                const double e = std::exp(-t / 0.025);
                s = e * (0.8 * rng.uni() + 0.4 * std::sin(2.0 * kPi * (60.0 + 200.0 * rng2.uni() * 0.0) * t));
                buf[static_cast<size_t>(i0 + i)] += static_cast<float>(sg.gain * s);
                continue;
            }

            // No release fade when the next note continues legato from exactly here (a hammer-on or slide has no dip).
            const bool continues = k + 1 < segs.size() && segs[k + 1].legato && std::abs(segs[k + 1].start - (sg.start + sg.dur)) < 1e-6;
            const double rel = continues ? 1.0 : std::min(1.0, std::max(0.0, (sg.dur - t) / (timbre == Timbre::Voice ? 0.05 : 0.03)));

            if (timbre == Timbre::Voice)
            {
                bool gap = sg.gapStart >= 0.0 && t >= sg.gapStart && t < sg.gapStart + sg.gapDur;
                const double target = gap ? 0.0 : 1.0;
                voiceEnv += (target - voiceEnv) * (target > voiceEnv ? 1.0 - std::exp(-1.0 / (0.012 * fs)) : 1.0 - std::exp(-1.0 / (0.006 * fs)));
                if (sg.legato || t > 0.0)
                {
                    // attack for fresh notes only
                }
                const double att = sg.legato ? 1.0 : std::min(1.0, t / 0.02);

                const int maxHarm = std::max(1, std::min(fx.breathy ? 4 : 60, static_cast<int>(5000.0 / f0)));
                for (int h = 1; h <= maxHarm; ++h)
                {
                    const double fh = h * f0;
                    double g = 0.25;
                    g += 1.0 / (1.0 + std::pow((fh - 650.0) / 90.0, 2.0));
                    g += 0.7 / (1.0 + std::pow((fh - 1100.0) / 110.0, 2.0));
                    g += 0.4 / (1.0 + std::pow((fh - 2500.0) / 180.0, 2.0));
                    s += g / (fx.breathy ? h * h : h) * std::sin(h * phase);
                }
                if (fx.breathy)
                    s *= 2.5;
                s *= voiceEnv * att;
                s += (fx.breathy ? 0.25 : 0.02) * rng.uni() * (1.0 - 0.7 * voiceEnv * att);
                if (gap) // fricative: loud broadband hiss
                    s += 0.35 * rng.uni();
                s *= rel;
            }
            else
            {
                const int maxHarm = std::max(1, std::min(40, static_cast<int>(5000.0 / f0)));
                for (int h = 1; h <= maxHarm; ++h)
                {
                    const double a = 1.0 / std::pow(h, 1.1);
                    const double tau = 1.5 / std::sqrt(static_cast<double>(h));
                    s += a * std::exp(-tPluck / tau) * std::sin(h * phase + 0.3 * h);
                }
                const double att = std::min(1.0, tPluck / 0.002);
                s *= 0.5 * att * rel;

                // Fret/pick noise at every fresh attack.
                if (fx.fretNoiseDb > -150.0 && !sg.legato)
                    s += std::pow(10.0, fx.fretNoiseDb / 20.0) * rng2.uni() * std::exp(-t / 0.01);
                (void) lastFret;
            }
            buf[static_cast<size_t>(i0 + i)] += static_cast<float>(sg.gain * s);
        }
    }

    // Chain: drive -> reverb -> hum / hiss
    if (fx.driveDb > 0.0)
    {
        const float k = std::pow(10.0f, static_cast<float>(fx.driveDb) / 20.0f);
        const float norm = 1.0f / std::tanh(k);
        for (auto& v : buf)
            v = std::tanh(k * v) * norm * 0.5f;
    }
    applyReverb(buf, fs, fx.reverbMix, fx.reverbRt60);

    const float hum = std::pow(10.0f, static_cast<float>(fx.humDb) / 20.0f);
    const float hiss = std::pow(10.0f, static_cast<float>(fx.hissDb) / 20.0f);
    Rng nr;
    nr.s = 4242;
    for (size_t i = 0; i < buf.size(); ++i)
    {
        buf[i] += hum * (static_cast<float>(std::sin(2.0 * kPi * 50.0 * i / fs)) + 0.5f * static_cast<float>(std::sin(2.0 * kPi * 100.0 * i / fs)));
        buf[i] += hiss * nr.uni();
    }

    sc.audio = std::move(buf);
    return sc;
}

// ---- the suite -------------------------------------------------------------------------------

inline std::vector<Scenario> buildSuite(double fs = 48000.0)
{
    std::vector<Scenario> suite;
    auto seq = [](std::initializer_list<int> notes, double t0, double dur, double pitchStep, bool legato, double glide = 0.0) {
        std::vector<Seg> v;
        double t = t0;
        for (int n : notes)
        {
            Seg s;
            s.midi = n;
            s.start = t;
            s.dur = dur;
            s.legato = legato && !v.empty();
            s.glideMs = s.legato ? glide : 0.0;
            v.push_back(s);
            t += pitchStep;
        }
        return v;
    };

    // Bass
    suite.push_back(render("bass: plucked line E1-G2", fs, Timbre::BassPluck,
                           seq({ 28, 31, 33, 35, 38, 36, 33, 31, 28, 40, 43, 38, 35, 33, 31, 28 }, 0.1, 0.24, 0.30, false), {}));
    suite.push_back(render("bass: low B string B0-E1", fs, Timbre::BassPluck, seq({ 23, 25, 27, 28, 25, 23, 28, 27 }, 0.1, 0.36, 0.44, false), {}));
    suite.push_back(render("bass: hammer-ons (legato)", fs, Timbre::BassPluck, seq({ 28, 30, 31, 33, 35, 33, 31, 30, 28 }, 0.1, 0.20, 0.20, true), {}));
    {
        // Slides of 2 and 5 semitones over 120 ms.
        std::vector<Seg> v = seq({ 33, 35 }, 0.1, 0.5, 0.5, true, 120.0);
        auto w = seq({ 28, 33 }, 1.2, 0.5, 0.5, true, 120.0);
        v.insert(v.end(), w.begin(), w.end());
        suite.push_back(render("bass: slides (2 + 5 st)", fs, Timbre::BassPluck, v, {}));
    }
    {
        Fx fx;
        fx.driveDb = 18.0;
        fx.hissDb = -70.0;
        suite.push_back(render("bass: heavy distortion", fs, Timbre::BassPluck,
                               seq({ 28, 31, 33, 35, 38, 36, 33, 31, 28, 40 }, 0.1, 0.24, 0.30, false), fx));
    }
    {
        Fx fx;
        fx.humDb = -46.0;
        fx.hissDb = -58.0;
        fx.fretNoiseDb = -22.0;
        suite.push_back(render("bass: hum + hiss + fret noise", fs, Timbre::BassPluck,
                               seq({ 28, 31, 33, 35, 38, 36, 33, 31 }, 0.1, 0.24, 0.30, false), fx));
    }
    {
        std::vector<Seg> v;
        for (int i = 0; i < 8; ++i)
            v.push_back({ 33.0, 0.1 + 0.18 * i, 0.16, false, 0.0, 0.5 });
        suite.push_back(render("bass: repeated 16ths (A1 x8)", fs, Timbre::BassPluck, v, {}));
    }
    {
        std::vector<Seg> v;
        for (int i = 0; i < 6; ++i)
        {
            Seg s;
            s.dead = true;
            s.start = 0.1 + 0.3 * i;
            s.dur = 0.12;
            s.gain = 0.6;
            v.push_back(s);
        }
        suite.push_back(render("bass: dead notes (expect nothing)", fs, Timbre::BassPluck, v, {}));
    }

    // Voice
    {
        Fx fx;
        fx.vibCents = 35.0;
        suite.push_back(render("voice: sustained notes + vibrato", fs, Timbre::Voice,
                               seq({ 55, 57, 59, 60, 62, 64, 62, 60, 57, 55 }, 0.1, 0.42, 0.50, false), fx));
    }
    suite.push_back(render("voice: legato melody (portamento)", fs, Timbre::Voice,
                           seq({ 57, 60, 62, 64, 62, 60, 57, 55, 57 }, 0.1, 0.30, 0.30, true, 60.0), {}));
    {
        std::vector<Seg> v;
        for (int i = 0; i < 6; ++i)
        {
            Seg s;
            s.midi = 55 + (i * 5) % 11;
            s.start = 0.1 + 0.6 * i;
            s.dur = 0.5;
            s.gapStart = 0.2;
            s.gapDur = 0.07; // "s"/"f" in the middle of a held note: one note, not two
            v.push_back(s);
        }
        suite.push_back(render("voice: long fricatives (2 syllables)", fs, Timbre::Voice, v, {}));
    }
    {
        std::vector<Seg> v;
        for (int i = 0; i < 6; ++i)
        {
            Seg s;
            s.midi = 55 + (i * 5) % 11;
            s.start = 0.1 + 0.6 * i;
            s.dur = 0.5;
            s.gapStart = 0.2;
            s.gapDur = 0.03; // short "t"/"k": the note carries on
            v.push_back(s);
        }
        suite.push_back(render("voice: short consonants (1 note)", fs, Timbre::Voice, v, {}));
    }
    {
        Fx fx;
        fx.vibCents = 80.0;
        fx.breathy = true;
        suite.push_back(render("voice: falsetto, wide vibrato, breathy", fs, Timbre::Voice,
                               seq({ 69, 71, 73, 76, 73, 71, 69 }, 0.1, 0.45, 0.52, false), fx));
    }
    {
        Fx fx;
        fx.reverbMix = 0.5;
        fx.reverbRt60 = 0.9;
        suite.push_back(render("voice: staccato in a reverberant room", fs, Timbre::Voice,
                               seq({ 55, 59, 62, 59, 55, 62, 64, 60 }, 0.1, 0.20, 0.45, false), fx, 1.0));
    }
    return suite;
}

} // namespace nota::test
