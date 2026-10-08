#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr const char* kGate = "gate";
constexpr const char* kRelease = "release";
constexpr const char* kResponse = "response";
constexpr const char* kBend = "bend";
constexpr const char* kBendRange = "bendRange";
constexpr const char* kTranspose = "transpose";
constexpr const char* kRange = "range";
constexpr const char* kVelMin = "velMin";
constexpr const char* kVelMax = "velMax";
constexpr const char* kVelFull = "velFull";
constexpr const char* kTuning = "tuning";
constexpr const char* kRetrigger = "retrigger";
constexpr const char* kChannel = "channel";

juce::ParameterID pid(const char* id) { return { id, 1 }; }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout NotaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add(std::make_unique<AudioParameterFloat>(pid(kGate), "Gate", NormalisableRange<float>(-80.0f, -20.0f, 0.1f), -55.0f,
                                                AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterFloat>(pid(kRelease), "Release", NormalisableRange<float>(6.0f, 60.0f, 0.5f), 30.0f,
                                                AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterFloat>(pid(kResponse), "Response", NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    l.add(std::make_unique<AudioParameterChoice>(pid(kRange), "Range", StringArray { "Auto", "Bass", "Voice" }, 0));
    l.add(std::make_unique<AudioParameterInt>(pid(kVelMin), "Velocity Min", 1, 127, 20));
    l.add(std::make_unique<AudioParameterInt>(pid(kVelMax), "Velocity Max", 1, 127, 127));
    l.add(std::make_unique<AudioParameterFloat>(pid(kVelFull), "Velocity Full Level", NormalisableRange<float>(-40.0f, 0.0f, 0.5f), -12.0f,
                                                AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterFloat>(pid(kRetrigger), "Retrigger", NormalisableRange<float>(0.0f, 20.0f, 0.5f), 7.0f,
                                                AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterBool>(pid(kBend), "Pitch Bend", false));
    l.add(std::make_unique<AudioParameterFloat>(pid(kBendRange), "Bend Range", NormalisableRange<float>(1.0f, 12.0f, 1.0f), 2.0f,
                                                AudioParameterFloatAttributes().withLabel("st")));
    l.add(std::make_unique<AudioParameterInt>(pid(kTranspose), "Transpose", -36, 36, 0));
    l.add(std::make_unique<AudioParameterFloat>(pid(kTuning), "Tuning", NormalisableRange<float>(415.0f, 466.0f, 0.1f), 440.0f,
                                                AudioParameterFloatAttributes().withLabel("Hz")));
    l.add(std::make_unique<AudioParameterInt>(pid(kChannel), "MIDI Channel", 1, 16, 1));
    return l;
}

NotaProcessor::NotaProcessor()
    : AudioProcessor(makeBuses()),
      apvts(*this, nullptr, "STATE", createLayout())
{
    pGate_ = apvts.getRawParameterValue(kGate);
    pRelease_ = apvts.getRawParameterValue(kRelease);
    pResponse_ = apvts.getRawParameterValue(kResponse);
    pBend_ = apvts.getRawParameterValue(kBend);
    pBendRange_ = apvts.getRawParameterValue(kBendRange);
    pTranspose_ = apvts.getRawParameterValue(kTranspose);
    pRange_ = apvts.getRawParameterValue(kRange);
    pVelMin_ = apvts.getRawParameterValue(kVelMin);
    pVelMax_ = apvts.getRawParameterValue(kVelMax);
    pVelFull_ = apvts.getRawParameterValue(kVelFull);
    pTuning_ = apvts.getRawParameterValue(kTuning);
    pRetrigger_ = apvts.getRawParameterValue(kRetrigger);
    pChannel_ = apvts.getRawParameterValue(kChannel);
}

// No main input; the audio to analyse arrives on an optional sidechain; the (silent) output is stereo.
juce::AudioProcessor::BusesProperties NotaProcessor::makeBuses()
{
    return BusesProperties()
        .withInput("Input", juce::AudioChannelSet::disabled(), false)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

bool NotaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto isMonoOrStereo = [](const juce::AudioChannelSet& c) {
        return c == juce::AudioChannelSet::mono() || c == juce::AudioChannelSet::stereo();
    };
    const auto side = layouts.getChannelSet(true, 1);
    return isMonoOrStereo(layouts.getMainOutputChannelSet()) && layouts.getMainInputChannelSet().isDisabled()
        && (side.isDisabled() || isMonoOrStereo(side));
}

nota::Params NotaProcessor::readParams() const
{
    nota::Params p;
    p.gateDb = pGate_->load();
    p.releaseDb = pRelease_->load();
    p.response = pResponse_->load();
    p.pitchBend = pBend_->load() > 0.5f;
    p.bendRange = pBendRange_->load();
    p.transpose = static_cast<int>(pTranspose_->load());
    p.range = static_cast<nota::RangeMode>(static_cast<int>(pRange_->load()));
    p.velocityMin = static_cast<int>(pVelMin_->load());
    p.velocityMax = static_cast<int>(pVelMax_->load());
    p.velocityFullDb = pVelFull_->load();
    p.tuningHz = pTuning_->load();
    p.retriggerDb = pRetrigger_->load();
    return p;
}

void NotaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate);
    mono_.assign(static_cast<size_t>(std::max(samplesPerBlock, 1024)), 0.0f);
    heldNote_ = -1;
}

void NotaProcessor::releaseResources()
{
    releaseHeldNote(nullptr);
    engine_.reset();
}

void NotaProcessor::releaseHeldNote(juce::MidiBuffer* midi)
{
    if (heldNote_ < 0)
        return;

    if (midi != nullptr)
        midi->addEvent(juce::MidiMessage::noteOff(heldChannel_, heldNote_), 0);
    heldNote_ = -1;
}

void NotaProcessor::emit(const nota::EventList& events, juce::MidiBuffer& midi)
{
    const int ch = std::clamp(static_cast<int>(pChannel_->load()), 1, 16);

    for (int i = 0; i < events.count; ++i)
    {
        const auto& e = events.events[i];
        using T = nota::MidiEvent::Type;
        switch (e.type)
        {
            case T::NoteOn:
                midi.addEvent(juce::MidiMessage::noteOn(ch, e.data1, static_cast<juce::uint8>(e.data2)), e.sampleOffset);
                heldNote_ = e.data1;
                heldChannel_ = ch;
                break;
            case T::NoteOff:
                midi.addEvent(juce::MidiMessage::noteOff(ch, e.data1), e.sampleOffset);
                if (heldNote_ == e.data1)
                    heldNote_ = -1;
                break;
            case T::PitchBend:
                midi.addEvent(juce::MidiMessage::pitchWheel(ch, e.bend), e.sampleOffset);
                break;
            case T::Cc:
                midi.addEvent(juce::MidiMessage::controllerEvent(ch, e.data1, e.data2), e.sampleOffset);
                break;
        }
    }
}

void NotaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numSamples = buffer.getNumSamples();

    // The audio to analyse is the sidechain bus.
    const auto input = getBusBuffer(buffer, true, 1);
    const int numIn = input.getNumChannels();

    engine_.setParams(readParams());

    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min(numSamples - done, static_cast<int>(mono_.size()));

        // Mono mix of however many input channels the host gave us.
        for (int i = 0; i < n; ++i)
            mono_[static_cast<size_t>(i)] = 0.0f;
        for (int c = 0; c < numIn; ++c)
        {
            const float* src = input.getReadPointer(c) + done;
            for (int i = 0; i < n; ++i)
                mono_[static_cast<size_t>(i)] += src[i];
        }
        if (numIn > 1)
            for (int i = 0; i < n; ++i)
                mono_[static_cast<size_t>(i)] /= static_cast<float>(numIn);

        events_.clear();
        engine_.process(mono_.data(), n, events_);

        // Event offsets are relative to this chunk; shift them into the host block.
        for (int i = 0; i < events_.count; ++i)
            events_.events[i].sampleOffset += done;
        emit(events_, midi);

        done += n;
    }

    const auto& f = engine_.lastFrame();
    display.note.store(engine_.currentNote());
    display.freqHz.store(f.voiced ? f.freqHz : 0.0f);
    display.levelDb.store(f.levelDb);
    display.clarity.store(f.clarity);
    if (engine_.lastVelocity() > 0)
        display.velocity.store(engine_.lastVelocity());

    buffer.clear(); // an instrument has nothing to say in the audio path
}

void NotaProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer& midi)
{
    midi.clear();
    releaseHeldNote(&midi);
    engine_.reset();
}

juce::AudioProcessorEditor* NotaProcessor::createEditor()
{
    return new NotaEditor(*this);
}

void NotaProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void NotaProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NotaProcessor();
}
