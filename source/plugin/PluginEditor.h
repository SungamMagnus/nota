#pragma once

#include "PluginProcessor.h"
#include "SungamUi.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

// The panel is drawn on a fixed 720 x 464 design grid and scaled as one block, like every Sungam panel.
class NotaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NotaEditor(NotaProcessor&);
    ~NotaEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr float kDesignW = 720.0f;
    static constexpr float kDesignH = 464.0f;

    // Test hook: set what the readout shows without any audio running.
    void setDisplayForPreview(int note, float freqHz, float levelDb, float clarity, int velocity = 0);

private:
    void timerCallback() override;
    void place(juce::Component& c, float cx, float top);
    void placeAt(juce::Component& c, float x, float y);
    float scale() const { return static_cast<float>(getWidth()) / kDesignW; }
    juce::Rectangle<int> toPixels(juce::Rectangle<float> design) const;
    void drawReadout(juce::Graphics&) const;
    void updateModeVisibility();
    void drawVelocity(juce::Graphics&) const;
    void drawStatus(juce::Graphics&) const;

    NotaProcessor& proc_;

    std::unique_ptr<sungam::Knob> gate_, response_, release_, retrigger_, bendRange_, transpose_, tuning_, channel_, velFull_, mpeBend_;
    std::unique_ptr<sungam::Latch> bend_;
    std::unique_ptr<sungam::Latch> pressure_, slide_;
    std::unique_ptr<sungam::Selector> mode_;
    std::unique_ptr<sungam::RangeBar> velRange_;
    std::unique_ptr<juce::ParameterAttachment> modeWatcher_;
    std::unique_ptr<sungam::Selector> range_;
    std::unique_ptr<juce::ParameterAttachment> bendWatcher_;

    int note_ = -1;
    int lastNote_ = -1;
    float freq_ = 0.0f, level_ = -120.0f, clarity_ = 0.0f;
    int velocity_ = 0;
    float shownLevel_ = -120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotaEditor)
};
