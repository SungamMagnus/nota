#pragma once

#include "Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

// Nota is a VST3 *instrument* with an audio sidechain input: the audio to analyse comes in on the
// sidechain, MIDI goes out. (Ableton Live only routes MIDI out of a plug-in that sits on a MIDI
// track, and refuses an instrument that has no MIDI input, hence acceptsMidi() == true.)
class NotaProcessor : public juce::AudioProcessor
{
public:
    NotaProcessor();
    ~NotaProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; } // required by Live; incoming MIDI is ignored
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState apvts;

    // For the editor (audio thread writes, UI thread reads).
    struct Display
    {
        std::atomic<int> note { -1 };
        std::atomic<float> freqHz { 0.0f };
        std::atomic<float> levelDb { -120.0f };
        std::atomic<float> clarity { 0.0f };
        std::atomic<int> velocity { 0 };   // velocity of the most recent note-on
    } display;

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    nota::Params readParams() const;
    void emit(const nota::EventList& events, juce::MidiBuffer& midi);
    void releaseHeldNote(juce::MidiBuffer* midi);

    nota::Engine engine_;
    std::vector<float> mono_;
    nota::EventList events_;

    int heldNote_ = -1; // note number currently sounding on the output (after transpose)
    int heldChannel_ = 1;

    std::atomic<float>* pGate_ = nullptr;
    std::atomic<float>* pRelease_ = nullptr;
    std::atomic<float>* pResponse_ = nullptr;
    std::atomic<float>* pBend_ = nullptr;
    std::atomic<float>* pBendRange_ = nullptr;
    std::atomic<float>* pTranspose_ = nullptr;
    std::atomic<float>* pRange_ = nullptr;
    std::atomic<float>* pVelMin_ = nullptr;
    std::atomic<float>* pVelMax_ = nullptr;
    std::atomic<float>* pVelFull_ = nullptr;
    std::atomic<float>* pTuning_ = nullptr;
    std::atomic<float>* pRetrigger_ = nullptr;
    std::atomic<float>* pChannel_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotaProcessor)
};
