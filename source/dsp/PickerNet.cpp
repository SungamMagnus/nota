#include "PickerNet.h"

#include <algorithm>
#include <cmath>

namespace nota
{

namespace
{
inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }
} // namespace

void PickerNet::reset()
{
    std::fill(std::begin(h_), std::end(h_), 0.0f);
}

void PickerNet::step(const float* x, float* logits)
{
    // The network was trained on int8-quantised features (x127): reproduce that exactly.
    float q[kIn];
    for (int i = 0; i < kIn; ++i)
        q[i] = std::round(std::clamp(x[i], -1.0f, 1.0f) * 127.0f) * (1.0f / 127.0f);

    // Embedding: ReLU(W q + b)
    for (int o = 0; o < kEmb; ++o)
    {
        const float* w = kPickerWInp + o * kIn;
        float acc = kPickerBInp[o];
        for (int i = 0; i < kIn; ++i)
            acc += w[i] * q[i];
        emb_[o] = acc > 0.0f ? acc : 0.0f;
    }

    // GRU (PyTorch gate order r, z, n):
    //   r = s(Wir e + bir + Whr h + bhr)   z = s(Wiz e + biz + Whz h + bhz)
    //   n = tanh(Win e + bin + r * (Whn h + bhn))   h' = (1 - z) n + z h
    for (int o = 0; o < 3 * kHidden; ++o)
    {
        const float* wi = kPickerWIh + o * kEmb;
        const float* wh = kPickerWHh + o * kHidden;
        float ai = kPickerBIh[o], ah = kPickerBHh[o];
        for (int i = 0; i < kEmb; ++i)
            ai += wi[i] * emb_[i];
        for (int i = 0; i < kHidden; ++i)
            ah += wh[i] * h_[i];
        gates_[o] = ai;
        hh_[o] = ah;
    }
    for (int j = 0; j < kHidden; ++j)
    {
        const float r = sigmoid(gates_[j] + hh_[j]);
        const float z = sigmoid(gates_[kHidden + j] + hh_[kHidden + j]);
        const float n = std::tanh(gates_[2 * kHidden + j] + r * hh_[2 * kHidden + j]);
        h_[j] = (1.0f - z) * n + z * h_[j];
    }

    // Head over [h, embedding]
    for (int o = 0; o < kOut; ++o)
    {
        const float* w = kPickerWOut + o * (kHidden + kEmb);
        float acc = kPickerBOut[o];
        for (int i = 0; i < kHidden; ++i)
            acc += w[i] * h_[i];
        for (int i = 0; i < kEmb; ++i)
            acc += w[kHidden + i] * emb_[i];
        logits[o] = acc;
    }
}

} // namespace nota
