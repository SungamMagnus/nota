#pragma once

#include "Fft.h"

#include <array>
#include <vector>

namespace nota
{

struct PitchCandidate
{
    bool valid = false;
    float lag = 0.0f;      // fractional lag in (decimated) samples
    float clarity = 0.0f;  // NSDF peak height, ~1 for a clean periodic signal
    int scale = -1;        // which analysis window produced it
};

// Multi-scale McLeod pitch tracker (NSDF + key-maximum picking).
//
// Latency is the whole point, so instead of one big window there is a ladder of
// windows. Each hop the smallest window is tried first and the first scale with
// a clear peak wins. A high note is therefore decided after ~2 periods of the
// *note* (a few ms), and only low notes pay for a long window - which is a
// physical limit anyway (you cannot call the period of a 41 Hz note before you
// have heard roughly two of them, ~50 ms).
class PitchTracker
{
public:
    static constexpr int kNumScales = 9;

    void prepare(double decimatedRate, float minHz, float maxHz);
    void reset();

    void setClarityThreshold(float t) { clarityThreshold_ = t; }
    void setKFactor(float k) { kFactor_ = k; }

    void push(float x)
    {
        ring_[static_cast<size_t>(writePos_ & kRingMask)] = x;
        ++writePos_;
        if (filled_ < kRingSize)
            ++filled_;
    }

    PitchCandidate analyze();

    // Sum of squares of `len` samples ending `back` samples before the newest one.
    double energy(int back, int len) const
    {
        double e = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const float x = ring_[static_cast<size_t>((writePos_ - 1 - back - i) & kRingMask)];
            e += static_cast<double>(x) * x;
        }
        return e;
    }

    // Same window as energy(), but of x[n] - x[n-1]: relative to energy() this measures spectral tilt.
    double energyDiff(int back, int len) const
    {
        double e = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const float a = ring_[static_cast<size_t>((writePos_ - 1 - back - i) & kRingMask)];
            const float b = ring_[static_cast<size_t>((writePos_ - 2 - back - i) & kRingMask)];
            e += static_cast<double>(a - b) * (a - b);
        }
        return e;
    }

    float lagToHz(float lag) const { return lag > 0.0f ? static_cast<float>(fs_) / lag : 0.0f; }
    double rate() const { return fs_; }

private:
    static constexpr int kRingSize = 4096;
    static constexpr int kRingMask = kRingSize - 1;
    static constexpr float kLagFraction = 0.45f; // usable lag range as a fraction of the window

    static constexpr std::array<int, kNumScales> kWindows { 96, 136, 192, 264, 360, 488, 664, 904, 1232 };

    PitchCandidate analyzeScale(int scale);
    int computeNsdf(int scale);
    int lagLimit(int scale) const { return static_cast<int>(kLagFraction * static_cast<float>(kWindows[static_cast<size_t>(scale)])); }

    double fs_ = 12000.0;
    int minLag_ = 8;
    float clarityThreshold_ = 0.86f;
    float kFactor_ = 0.90f;

    std::vector<float> ring_ = std::vector<float>(kRingSize, 0.0f);
    long long writePos_ = 0;
    int filled_ = 0;

    std::array<Fft, kNumScales> ffts_;
    std::vector<float> re_, im_, nsdf_;
    std::vector<double> cumEnergy_;
};

} // namespace nota
