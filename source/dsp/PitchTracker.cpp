#include "PitchTracker.h"

#include <algorithm>
#include <cmath>

namespace nota
{

namespace
{
int nextPow2(int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}
} // namespace

void PitchTracker::prepare(double decimatedRate, float /*minHz*/, float maxHz)
{
    fs_ = decimatedRate;
    minLag_ = std::max(3, static_cast<int>(std::floor(fs_ / static_cast<double>(maxHz))));

    int maxN = 0;
    for (int s = 0; s < kNumScales; ++s)
    {
        const int w = kWindows[static_cast<size_t>(s)];
        const int n = nextPow2(w + w / 2);
        ffts_[static_cast<size_t>(s)].init(n);
        maxN = std::max(maxN, n);
    }

    re_.assign(static_cast<size_t>(maxN), 0.0f);
    im_.assign(static_cast<size_t>(maxN), 0.0f);
    nsdf_.assign(static_cast<size_t>(maxN), 0.0f);
    cumEnergy_.assign(static_cast<size_t>(kWindows[kNumScales - 1] + 2), 0.0);
    reset();
}

void PitchTracker::reset()
{
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    writePos_ = 0;
    filled_ = 0;
}

PitchCandidate PitchTracker::analyzeScale(int scale)
{
    PitchCandidate out;
    const int w = kWindows[static_cast<size_t>(scale)];
    if (filled_ < w)
        return out;

    const Fft& fft = ffts_[static_cast<size_t>(scale)];
    const int n = fft.size();
    const int limit = lagLimit(scale);
    if (limit <= minLag_ + 2)
        return out;

    // Copy the newest w samples (oldest first), zero-pad.
    const long long start = writePos_ - w;
    cumEnergy_[0] = 0.0;
    for (int i = 0; i < w; ++i)
    {
        const float x = ring_[static_cast<size_t>((start + i) & kRingMask)];
        re_[static_cast<size_t>(i)] = x;
        cumEnergy_[static_cast<size_t>(i) + 1] = cumEnergy_[static_cast<size_t>(i)] + static_cast<double>(x) * x;
    }
    std::fill(re_.begin() + w, re_.begin() + n, 0.0f);
    std::fill(im_.begin(), im_.begin() + n, 0.0f);

    // Autocorrelation through |X|^2.
    fft.transform(re_.data(), im_.data(), false);
    for (int k = 0; k < n; ++k)
    {
        re_[static_cast<size_t>(k)] = re_[static_cast<size_t>(k)] * re_[static_cast<size_t>(k)]
                                    + im_[static_cast<size_t>(k)] * im_[static_cast<size_t>(k)];
        im_[static_cast<size_t>(k)] = 0.0f;
    }
    fft.transform(re_.data(), im_.data(), true);

    // Normalised square difference function.
    const double total = cumEnergy_[static_cast<size_t>(w)];
    if (total < 1e-12)
        return out;

    for (int tau = 0; tau <= limit + 1; ++tau)
    {
        const double m = cumEnergy_[static_cast<size_t>(w - tau)] + (total - cumEnergy_[static_cast<size_t>(tau)]);
        nsdf_[static_cast<size_t>(tau)] = m > 1e-12 ? static_cast<float>(2.0 * re_[static_cast<size_t>(tau)] / m) : 0.0f;
    }

    // Key maxima: the highest point of every positive lobe after the first zero crossing.
    int tau = 1;
    while (tau <= limit && nsdf_[static_cast<size_t>(tau)] > 0.0f)
        ++tau;
    if (tau > limit)
        return out;

    constexpr int kMaxKeys = 24;
    int keyPos[kMaxKeys];
    int numKeys = 0;
    int bestPos = -1;
    float bestVal = 0.0f;

    for (; tau <= limit; ++tau)
    {
        const float v = nsdf_[static_cast<size_t>(tau)];
        if (v > 0.0f)
        {
            if (bestPos < 0 || v > bestVal)
            {
                bestPos = tau;
                bestVal = v;
            }
        }
        else if (bestPos >= 0)
        {
            if (bestPos >= minLag_ && numKeys < kMaxKeys)
                keyPos[numKeys++] = bestPos;
            bestPos = -1;
        }
    }
    // A lobe still open at the end of the range only counts if its peak is confirmed.
    if (bestPos >= minLag_ && bestPos < limit && numKeys < kMaxKeys
        && nsdf_[static_cast<size_t>(bestPos) + 1] < nsdf_[static_cast<size_t>(bestPos)])
        keyPos[numKeys++] = bestPos;

    if (numKeys == 0)
        return out;

    float maxKey = 0.0f;
    for (int i = 0; i < numKeys; ++i)
        maxKey = std::max(maxKey, nsdf_[static_cast<size_t>(keyPos[i])]);

    const float cutoff = kFactor_ * maxKey;
    int chosen = keyPos[0];
    for (int i = 0; i < numKeys; ++i)
    {
        if (nsdf_[static_cast<size_t>(keyPos[i])] >= cutoff)
        {
            chosen = keyPos[i];
            break;
        }
    }

    // Parabolic interpolation around the chosen peak.
    const float y0 = nsdf_[static_cast<size_t>(chosen) - 1];
    const float y1 = nsdf_[static_cast<size_t>(chosen)];
    const float y2 = nsdf_[static_cast<size_t>(chosen) + 1];
    const float denom = y0 - 2.0f * y1 + y2;
    float delta = 0.0f;
    float peak = y1;
    if (std::abs(denom) > 1e-9f)
    {
        delta = std::clamp(0.5f * (y0 - y2) / denom, -1.0f, 1.0f);
        peak = y1 - 0.25f * (y0 - y2) * delta;
    }

    out.valid = true;
    out.lag = static_cast<float>(chosen) + delta;
    out.clarity = std::min(peak, 1.0f);
    out.scale = scale;
    return out;
}

PitchCandidate PitchTracker::analyze()
{
    PitchCandidate best;
    for (int s = 0; s < kNumScales; ++s)
    {
        const PitchCandidate c = analyzeScale(s);
        if (c.valid && c.clarity >= clarityThreshold_)
        {
            best = c;
            break;
        }
    }

    if (!best.valid)
        return best;

    // The candidate came from a window that cannot see twice its own lag, so an
    // octave-down error is invisible to it. Ask the first window that can.
    if (best.lag * 1.9f > static_cast<float>(lagLimit(best.scale)))
    {
        for (int s = best.scale + 1; s < kNumScales; ++s)
        {
            if (static_cast<float>(lagLimit(s)) < best.lag * 1.9f)
                continue;

            const PitchCandidate c2 = analyzeScale(s);
            if (c2.valid && c2.clarity >= clarityThreshold_ - 0.04f
                && std::abs(c2.lag - 2.0f * best.lag) < 0.12f * 2.0f * best.lag)
                best = c2;
            break;
        }
    }

    return best;
}

} // namespace nota
