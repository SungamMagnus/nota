#pragma once

#include <cmath>
#include <utility>
#include <vector>

namespace nota
{

// Small in-place radix-2 complex FFT. init() allocates; transform() does not.
class Fft
{
public:
    void init(int n)
    {
        n_ = n;
        rev_.assign(static_cast<size_t>(n), 0);
        cos_.assign(static_cast<size_t>(n / 2), 0.0f);
        sin_.assign(static_cast<size_t>(n / 2), 0.0f);

        int bits = 0;
        while ((1 << bits) < n)
            ++bits;

        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev_[static_cast<size_t>(i)] = r;
        }

        const double twoPi = 6.283185307179586476925286766559;
        for (int k = 0; k < n / 2; ++k)
        {
            cos_[static_cast<size_t>(k)] = static_cast<float>(std::cos(twoPi * k / n));
            sin_[static_cast<size_t>(k)] = static_cast<float>(std::sin(twoPi * k / n));
        }
    }

    int size() const { return n_; }

    // Forward uses e^{-i...}; inverse uses e^{+i...} and scales by 1/n.
    void transform(float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < n_; ++i)
        {
            const int j = rev_[static_cast<size_t>(i)];
            if (j > i)
            {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }

        const float sgn = inverse ? 1.0f : -1.0f;
        for (int len = 2; len <= n_; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n_ / len;
            for (int i = 0; i < n_; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cos_[static_cast<size_t>(k * step)];
                    const float wi = sgn * sin_[static_cast<size_t>(k * step)];
                    const int a = i + k;
                    const int b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }

        if (inverse)
        {
            const float s = 1.0f / static_cast<float>(n_);
            for (int i = 0; i < n_; ++i)
            {
                re[i] *= s;
                im[i] *= s;
            }
        }
    }

private:
    int n_ = 0;
    std::vector<int> rev_;
    std::vector<float> cos_, sin_;
};

} // namespace nota
