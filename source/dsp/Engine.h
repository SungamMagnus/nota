#pragma once

#include "Biquad.h"
#include "NoteTracker.h"
#include "Params.h"
#include "PitchTracker.h"
#include "Types.h"

namespace nota
{

// Audio in -> MIDI events. Plain C++, no JUCE, no allocation after prepare().
class Engine
{
public:
    static constexpr int kHopSamples = 32; // decimated samples per analysis hop (~2.7 ms)

    void prepare(double sampleRate);
    void reset();
    void setParams(const Params& p);

    // Appends events for this block; sampleOffset is relative to `in[0]`.
    void process(const float* in, int numSamples, EventList& out);

    // Read-outs for UI / tests (written from the audio thread).
    const PitchFrame& lastFrame() const { return lastFrame_; }
    int currentNote() const { return notes_.currentNote(); }
    int lastVelocity() const { return notes_.lastVelocity(); }
    int decimation() const { return decim_; }
    double hopRateHz() const { return hopRate_; }

private:
    void runHop(int sampleOffset, EventList& out);

    Params params_;
    double fs_ = 48000.0;
    double fsDec_ = 12000.0;
    double hopRate_ = 375.0;
    int decim_ = 4;

    Biquad lp1_, lp2_, hp_;
    PitchTracker tracker_;
    NoteTracker notes_;

    int decimCounter_ = 0;
    int hopCounter_ = 0;

    float env_ = 0.0f;
    float attackCoef_ = 0.0f, releaseCoef_ = 0.0f;
    float noiseFloorDb_ = -120.0f;

    int hopsSinceOnset_ = 1000;
    float peakDb_ = -120.0f;

    float minHz_ = 28.0f, maxHz_ = 1250.0f;
    int stabilityHops_ = 3;

    PitchFrame lastFrame_;
};

} // namespace nota
