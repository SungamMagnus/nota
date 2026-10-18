#pragma once

#include "PickerWeights.h"

namespace nota
{

// False when built with NOTA_NEURAL=OFF (no trained weights compiled in): the neural picker cannot run.
#ifdef NOTA_NO_NEURAL
constexpr bool kNeuralBuilt = false;
#else
constexpr bool kNeuralBuilt = true;
#endif

// Forward pass of the trained pitch picker (training/train.py): Linear+ReLU -> GRU -> Linear over
// [GRU output, embedding]. Hand-written: ~170k multiply-adds per 2.7 ms hop, no allocation, no dependencies.
//
//   input   kPickerIn floats = PitchTracker::features() + level, quantised exactly as in training
//   output  kPickerOut logits: log-lag bins 0..95 and, last, "unvoiced"
class PickerNet
{
public:
    static constexpr int kIn = kPickerIn;
    static constexpr int kEmb = kPickerEmb;
    static constexpr int kHidden = kPickerHidden;
    static constexpr int kOut = kPickerOut;
    static constexpr int kUnvoiced = kPickerOut - 1;

    void reset();
    void step(const float* x, float* logits);

private:
    float emb_[kEmb] {};
    float h_[kHidden] {};
    float gates_[3 * kHidden] {};
    float hh_[3 * kHidden] {};
};

} // namespace nota
