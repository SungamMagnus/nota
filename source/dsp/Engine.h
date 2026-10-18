#pragma once

#include "Biquad.h"
#include "NoteTracker.h"
#include "PickerNet.h"
#include "Params.h"
#include "PitchTracker.h"
#include "Types.h"

namespace nota
{

// Optional tap on every analysis hop, for tools and tests (never set in the plug-in).
struct FrameSink
{
    virtual ~FrameSink() = default;
    // hopIndex counts hops since reset(); the hop completed at (hopIndex + 1) * kHopSamples analysis samples.
    // features is null unless wantsFeatures(); otherwise Engine::kFeatureDim floats (layout in Engine::kFeatureDim).
    virtual void onFrame(const PitchFrame& frame, int note, long long hopIndex, const float* features) = 0;
    virtual bool wantsFeatures() const { return false; }
};

// Audio in -> MIDI events. Plain C++, no JUCE, no allocation after prepare().
class Engine
{
public:
    static constexpr int kHopSamples = 32; // analysis samples (12 kHz) per hop, ~2.7 ms
    // Network input: NSDF of four windows on the log-lag grid, then the classical tracker's own answer (a bump on the
    // log-lag grid + its clarity) so the network only has to learn when to *override* it, then the level.
    static constexpr int kPriorBins = PitchTracker::kFeatureBins;
    static constexpr int kFeatureDim = PitchTracker::kFeatureScales * PitchTracker::kFeatureBins + kPriorBins + 1 + 1;

    void prepare(double sampleRate);
    void reset();
    void setParams(const Params& p);
    void setFrameSink(FrameSink* sink) { sink_ = sink; }
    double decimatedRate() const { return fsDec_; }

    // Appends events for this block; sampleOffset is relative to `in[0]`.
    void process(const float* in, int numSamples, EventList& out);

    // Read-outs for UI / tests (written from the audio thread).
    const PitchFrame& lastFrame() const { return lastFrame_; }
    // The level a note needs to be analysed at all: the Gate setting, raised by the adaptive noise floor when the input is noisy.
    float effectiveGateDb() const { return effGateDb_; }
    int currentNote() const { return notes_.currentNote(); }
    int lastVelocity() const { return notes_.lastVelocity(); }
    // Input samples per 12 kHz analysis sample (fractional for 44.1 kHz etc.).
    double inputPerAnalysisSample() const { return ratio_; }
    double hopRateHz() const { return hopRate_; }

private:
    void runHop(int sampleOffset, EventList& out);

    Params params_;
    double fs_ = 48000.0;
    double fsDec_ = 12000.0; // analysis rate: always 12 kHz, whatever the host rate
    double hopRate_ = 375.0;
    double ratio_ = 4.0;

    Biquad lp1_, lp2_, hp_;
    PitchTracker tracker_;
    PickerNet net_;
    float netLogits_[PickerNet::kOut] {};
    NoteTracker notes_;

    // Exact 12 kHz resampling: Catmull-Rom between the last four low-passed input samples.
    double nextOut_ = 1.0;
    long long inIndex_ = -1;
    float hist_[4] {};
    int hopCounter_ = 0;

    float env_ = 0.0f;
    float attackCoef_ = 0.0f, releaseCoef_ = 0.0f;
    float noiseFloorDb_ = -120.0f;
    float effGateDb_ = -55.0f;

    FrameSink* sink_ = nullptr;
    float features_[kFeatureDim] {};
    long long hopIndex_ = 0;
    int hopsSinceOnset_ = 1000;
    float peakDb_ = -120.0f;

    float minHz_ = 28.0f, maxHz_ = 1250.0f;
    int stabilityHops_ = 3;

    PitchFrame lastFrame_;
};

} // namespace nota
