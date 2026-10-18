#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace nota
{

namespace
{
constexpr float kAbsoluteMinHz = 26.0f;
constexpr float kAbsoluteMaxHz = 1300.0f;
} // namespace

void Engine::prepare(double sampleRate)
{
    fs_ = sampleRate;
    fsDec_ = 12000.0;
    ratio_ = fs_ / fsDec_;
    hopRate_ = fsDec_ / kHopSamples;

    // 4th-order Butterworth anti-alias at 3 kHz: the highest fundamental we care about
    // is ~1.2 kHz, and everything above that only feeds the tracker noise.
    lp1_.setLowpass(3000.0, fs_, 0.5412);
    lp2_.setLowpass(3000.0, fs_, 1.3066);
    hp_.setHighpass(18.0, fsDec_, 0.7071);

    attackCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.0006 * fsDec_));
    releaseCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.060 * fsDec_));

    tracker_.prepare(fsDec_, kAbsoluteMinHz, kAbsoluteMaxHz);
    notes_.prepare(hopRate_);
    setParams(params_);
    reset();
}

void Engine::reset()
{
    lp1_.reset();
    lp2_.reset();
    hp_.reset();
    tracker_.reset();
    notes_.reset();
    hopCounter_ = 0;
    nextOut_ = 1.0;
    inIndex_ = -1;
    hist_[0] = hist_[1] = hist_[2] = hist_[3] = 0.0f;
    env_ = 0.0f;
    noiseFloorDb_ = 0.0f; // drops to whatever the first levels are; starting low would never learn constant hum
    hopsSinceOnset_ = 1000;
    hopIndex_ = 0;
    peakDb_ = -120.0f;
    lastFrame_ = {};
}

void Engine::setParams(const Params& p)
{
    params_ = p;

    switch (p.range)
    {
        case RangeMode::Bass:  minHz_ = 28.0f; maxHz_ = 420.0f;  break;
        case RangeMode::Voice: minHz_ = 70.0f; maxHz_ = 1250.0f; break;
        case RangeMode::Auto:  minHz_ = 28.0f; maxHz_ = 1250.0f; break;
    }

    const float r = std::clamp(p.response, 0.0f, 1.0f);
    tracker_.setClarityThreshold(0.92f - 0.10f * r); // accurate .92 ... fast .82
    stabilityHops_ = r > 0.66f ? 2 : (r > 0.25f ? 3 : 4);
    notes_.configure(p, stabilityHops_);
}

void Engine::process(const float* in, int numSamples, EventList& out)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = lp2_.process(lp1_.process(in[i]));
        hist_[0] = hist_[1];
        hist_[1] = hist_[2];
        hist_[2] = hist_[3];
        hist_[3] = x;
        ++inIndex_;

        // Output positions p are spaced `ratio_` input samples apart; interpolate those that fall between
        // the two middle history samples (indices n-2 and n-1).
        while (nextOut_ < static_cast<double>(inIndex_ - 1))
        {
            const float t = static_cast<float>(nextOut_ - static_cast<double>(inIndex_ - 2));
            const float a = hist_[0], b = hist_[1], c = hist_[2], d = hist_[3];
            const float v = 0.5f * (2.0f * b + (-a + c) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t * t
                                    + (-a + 3.0f * b - 3.0f * c + d) * t * t * t);
            nextOut_ += ratio_;

            const float dsample = hp_.process(v);
            tracker_.push(dsample);

            const float a2 = std::abs(dsample);
            env_ += (a2 > env_ ? attackCoef_ : releaseCoef_) * (a2 - env_);

            if (++hopCounter_ >= kHopSamples)
            {
                hopCounter_ = 0;
                runHop(i, out);
            }
        }
    }
}

void Engine::runHop(int sampleOffset, EventList& out)
{
    const float levelDb = 20.0f * std::log10(env_ + 1e-9f);

    // Slowly-rising minimum tracker: a poor man's noise floor so hum/hiss does not trigger notes.
    if (levelDb < noiseFloorDb_)
        noiseFloorDb_ = levelDb;
    else
        noiseFloorDb_ += 0.0015f;
    const float effGate = std::max(params_.gateDb, std::min(noiseFloorDb_ + 12.0f, params_.gateDb + 15.0f));
    effGateDb_ = effGate;

    // Onset: the energy of the last period is well above the one before it. Windows are one period
    // of the current note long (so the cycle ripple of a bass note cancels out), which makes a
    // re-pluck of the *same* note visible even though the slow envelope barely moves.
    ++hopsSinceOnset_;
    bool onset = false;
    if (params_.retriggerDb > 0.0f && levelDb > effGate + 6.0f && hopsSinceOnset_ > 16)
    {
        const float hz = lastFrame_.voiced ? lastFrame_.freqHz : 90.0f;
        const int len = std::clamp(static_cast<int>(fsDec_ / hz), 48, 480);
        const double e1 = tracker_.energy(0, len);
        const double e0 = tracker_.energy(len, len);
        const double ratio = e1 / (e0 + 1e-9);
        // The window itself must be audible: the slow envelope lags and can still be high in a decay tail.
        const double rmsDb = 10.0 * std::log10(e1 / len + 1e-12);
        if (rmsDb > static_cast<double>(effGate) && 10.0 * std::log10(ratio + 1e-12) > static_cast<double>(params_.retriggerDb))
        {
            onset = true;
            hopsSinceOnset_ = 0;
            peakDb_ = levelDb;
        }
    }

    // Peak since the gate opened == the dynamics we map to velocity.
    if (levelDb < effGate - 6.0f)
        peakDb_ = -120.0f;
    else
        peakDb_ = std::max(peakDb_, levelDb);

    PitchFrame f;
    f.levelDb = levelDb;

    PitchCandidate c;
    if (levelDb >= effGate)
        c = tracker_.analyze();

    if (c.valid)
    {
        const float hz = tracker_.lagToHz(c.lag);
        if (hz >= minHz_ && hz <= maxHz_)
        {
            f.freqHz = hz;
            f.clarity = c.clarity;
            f.voiced = true;

            if (params_.mpe && params_.mpeSlide)
            {
                // Brightness: RMS of the first difference over RMS, relative to what a pure sine at this
                // pitch would give. 1 = sine; richer harmonics push it up (log-scaled onto 0..1).
                const int len = std::clamp(static_cast<int>(2.0 * fsDec_ / hz), 64, 480);
                const double e = tracker_.energy(0, len);
                const double ed = tracker_.energyDiff(0, len);
                if (e > 1e-12)
                {
                    const double sine = 2.0 * std::sin(3.14159265358979 * hz / fsDec_);
                    const double ratio = std::sqrt(ed / e) / std::max(sine, 1e-6);
                    f.brightness = static_cast<float>(std::clamp(std::log2(std::max(ratio, 1.0)) / 4.0, 0.0, 1.0));
                }
            }
        }
    }

    lastFrame_ = f;
    notes_.update(f, effGate, peakDb_, onset, sampleOffset, out);
    if (sink_ != nullptr)
        sink_->onFrame(f, notes_.currentNote(), hopIndex_);
    ++hopIndex_;
}

} // namespace nota
