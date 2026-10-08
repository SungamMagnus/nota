#pragma once

#include <cmath>

namespace nota
{

// Double-precision biquad (transposed direct form II). Double matters for the
// very low cutoffs used by the DC blocker at a 12 kHz rate.
class Biquad
{
public:
    void setLowpass(double fc, double fs, double q)
    {
        const double w0 = 2.0 * kPi * fc / fs;
        const double c = std::cos(w0), s = std::sin(w0);
        const double alpha = s / (2.0 * q);
        set((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighpass(double fc, double fs, double q)
    {
        const double w0 = 2.0 * kPi * fc / fs;
        const double c = std::cos(w0), s = std::sin(w0);
        const double alpha = s / (2.0 * q);
        set((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void reset() { z1_ = z2_ = 0.0; }

    float process(float x)
    {
        const double in = x;
        const double y = b0_ * in + z1_;
        z1_ = b1_ * in - a1_ * y + z2_;
        z2_ = b2_ * in - a2_ * y;
        return static_cast<float>(y);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void set(double b0, double b1, double b2, double a0, double a1, double a2)
    {
        b0_ = b0 / a0;
        b1_ = b1 / a0;
        b2_ = b2 / a0;
        a1_ = a1 / a0;
        a2_ = a2 / a0;
    }

    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    double z1_ = 0, z2_ = 0;
};

} // namespace nota
