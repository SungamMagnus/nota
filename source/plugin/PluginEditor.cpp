#include "PluginEditor.h"

using namespace sungam;

namespace
{
juce::String pct(float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }

juce::String noteName(int n) { return juce::MidiMessage::getMidiNoteName(n, true, true, 3); }

// Layout, in design pixels. Three columns under a readout band, a status strip under them.
constexpr float kFrameL = 12.0f, kFrameT = 12.0f, kFrameR = 708.0f, kFrameB = 452.0f;
constexpr float kBandBottom = 112.0f, kStripTop = 336.0f, kStatusTop = 416.0f;
constexpr float kDiv1 = 244.0f, kDiv2 = 476.0f;
constexpr float kRowA = 148.0f; // top of the first knob row
constexpr float kRowBCentre = 288.0f;
} // namespace

NotaEditor::NotaEditor(NotaProcessor& p) : juce::AudioProcessorEditor(&p), proc_(p)
{
    auto& s = proc_.apvts;
    auto param = [&](const char* id) -> juce::RangedAudioParameter& { return *s.getParameter(id); };

    // DETECT — the signal path (coral).
    gate_ = std::make_unique<Knob>(param("gate"), "Gate", kCoral, kCoral, 20.0f, false,
                                   [](float v) { return juce::String(juce::roundToInt(v)) + " dB"; });
    response_ = std::make_unique<Knob>(param("response"), "Response", kCoral, kCoral, 26.0f, false, [](float v) { return pct(v); });
    release_ = std::make_unique<Knob>(param("release"), "Release", kCoral, kCoral, 20.0f, false,
                                      [](float v) { return juce::String(juce::roundToInt(v)) + " dB"; });
    range_ = std::make_unique<Selector>(static_cast<juce::AudioParameterChoice&>(param("range")), kCoral);
    ai_ = std::make_unique<Latch>(param("neural"), "AI ASSIST", kCoral, 192.0f);
    addAndMakeVisible(*ai_);

    // NOTE — what a note does (teal).
    retrigger_ = std::make_unique<Knob>(param("retrigger"), "Retrigger", kTeal, kTeal, 20.0f, false,
                                        [](float v) { return v < 0.25f ? juce::String("OFF") : juce::String(v, 1) + " dB"; });
    bend_ = std::make_unique<Latch>(param("bend"), "PITCH BEND", kTeal, 192.0f);
    bendRange_ = std::make_unique<Knob>(param("bendRange"), "Bend range", kTeal, kTeal, 20.0f, false,
                                        [](float v) { return juce::String(juce::roundToInt(v)) + " st"; });

    // VELOCITY — how hard the note was played, mapped onto an output range (teal: it is a note property).
    velRange_ = std::make_unique<RangeBar>(param("velMin"), param("velMax"), "Range", kTeal, 200.0f);
    velFull_ = std::make_unique<Knob>(param("velFull"), "Full level", kTeal, kTeal, 13.0f, false,
                                      [](float v) { return juce::String(v, 1) + " dB"; });

    // OUTPUT — what leaves the plug-in (steel).
    transpose_ = std::make_unique<Knob>(param("transpose"), "Transpose", kSteel, kSteelText, 20.0f, true, [](float v) {
        const int n = juce::roundToInt(v);
        return (n > 0 ? "+" : "") + juce::String(n) + " st";
    });
    tuning_ = std::make_unique<Knob>(param("tuning"), "Tuning", kSteel, kSteelText, 20.0f, true,
                                     [](float v) { return juce::String(v, 1) + " Hz"; });
    channel_ = std::make_unique<Knob>(param("channel"), "Channel", kSteel, kSteelText, 20.0f, false,
                                      [](float v) { return juce::String(juce::roundToInt(v)); });

    for (juce::Component* c : { static_cast<juce::Component*>(gate_.get()), static_cast<juce::Component*>(response_.get()),
                                static_cast<juce::Component*>(release_.get()), static_cast<juce::Component*>(range_.get()),
                                static_cast<juce::Component*>(retrigger_.get()), static_cast<juce::Component*>(velRange_.get()),
                                static_cast<juce::Component*>(velFull_.get()),
                                static_cast<juce::Component*>(bend_.get()), static_cast<juce::Component*>(bendRange_.get()),
                                static_cast<juce::Component*>(transpose_.get()), static_cast<juce::Component*>(tuning_.get()),
                                static_cast<juce::Component*>(channel_.get()) })
        addAndMakeVisible(*c);

    // MPE: one channel per note, so each note carries its own bend / pressure / slide.
    mpeBend_ = std::make_unique<Knob>(param("mpeBend"), "MPE bend", kTeal, kTeal, 20.0f, false,
                                      [](float v) { return juce::String(juce::roundToInt(v)) + " st"; });
    pressure_ = std::make_unique<Latch>(param("mpePressure"), "PRESSURE", kTeal, 92.0f);
    slide_ = std::make_unique<Latch>(param("mpeSlide"), "SLIDE", kTeal, 92.0f);
    mode_ = std::make_unique<Selector>(static_cast<juce::AudioParameterChoice&>(param("mode")), kSteel);
    for (juce::Component* c : { static_cast<juce::Component*>(mpeBend_.get()), static_cast<juce::Component*>(pressure_.get()),
                                static_cast<juce::Component*>(slide_.get()), static_cast<juce::Component*>(mode_.get()),
                                })
        addAndMakeVisible(*c);

    // The NOTE column and the channel knob follow the output mode.
    modeWatcher_ = std::make_unique<juce::ParameterAttachment>(param("mode"), [this](float) { updateModeVisibility(); });
    modeWatcher_->sendInitialUpdate();

    // Bend range means nothing until pitch bend is on.
    bendWatcher_ = std::make_unique<juce::ParameterAttachment>(param("bend"), [this](float v) { bendRange_->setActive(v > 0.5f); });
    bendWatcher_->sendInitialUpdate();

    setResizeLimits(540, 348, 1440, 928);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(kDesignW) / kDesignH);
    setSize(900, 580);
    startTimerHz(30);
}

NotaEditor::~NotaEditor() = default;

void NotaEditor::updateModeVisibility()
{
    const bool mpe = proc_.apvts.getRawParameterValue("mode")->load() > 0.5f;
    bend_->setVisible(!mpe);
    bendRange_->setVisible(!mpe);
    mpeBend_->setVisible(mpe);
    pressure_->setVisible(mpe);
    slide_->setVisible(mpe);
    channel_->setActive(!mpe); // MPE chooses its own channels
}

juce::Rectangle<int> NotaEditor::toPixels(juce::Rectangle<float> d) const
{
    return (d * scale()).getSmallestIntegerContainer();
}

void NotaEditor::placeAt(juce::Component& c, float x, float y)
{
    auto& ds = dynamic_cast<DsComponent&>(c);
    c.setBounds(toPixels({ x, y, ds.designWidth(), ds.designHeight() }));
}

void NotaEditor::place(juce::Component& c, float cx, float top)
{
    auto& ds = dynamic_cast<DsComponent&>(c);
    placeAt(c, cx - ds.designWidth() * 0.5f, top);
}

void NotaEditor::resized()
{
    place(*gate_, 52.0f, kRowA);
    place(*response_, 128.0f, kRowA - 6.0f);
    place(*release_, 204.0f, kRowA);
    placeAt(*range_, 36.0f, kRowBCentre - 9.0f);
    placeAt(*ai_, 36.0f, kRowBCentre + 21.0f);

    place(*retrigger_, 300.0f, kRowA);
    place(*bendRange_, 420.0f, kRowA);
    placeAt(*bend_, 264.0f, kRowBCentre - 6.5f);

    place(*mpeBend_, 420.0f, kRowA);
    placeAt(*pressure_, 264.0f, kRowBCentre - 6.5f);
    placeAt(*slide_, 364.0f, kRowBCentre - 6.5f);

    place(*transpose_, 504.0f, kRowA);
    place(*tuning_, 588.0f, kRowA);
    place(*channel_, 672.0f, kRowA);
    placeAt(*mode_, 496.0f, kRowBCentre - 9.0f);

    placeAt(*velRange_, 140.0f, kStripTop + 14.0f);
    place(*velFull_, 400.0f, kStripTop + 10.0f);
}

void NotaEditor::setDisplayForPreview(int note, float freqHz, float levelDb, float clarity, int velocity)
{
    velocity_ = velocity;
    note_ = note;
    if (note >= 0)
        lastNote_ = note;
    freq_ = freqHz;
    level_ = shownLevel_ = levelDb;
    clarity_ = clarity;
}

void NotaEditor::timerCallback()
{
    const int n = proc_.display.note.load();
    const float f = proc_.display.freqHz.load();
    const float l = proc_.display.levelDb.load();
    const float c = proc_.display.clarity.load();
    const int vel = proc_.display.velocity.load();

    const float shown = juce::jmax(l, shownLevel_ - 2.0f); // fast attack, ~60 dB/s release
    const bool changed = n != note_ || vel != velocity_ || !juce::exactlyEqual(f, freq_) || !juce::exactlyEqual(c, clarity_)
                      || !juce::exactlyEqual(shown, shownLevel_);
    note_ = n;
    if (n >= 0)
        lastNote_ = n;
    freq_ = f;
    level_ = l;
    clarity_ = c;
    velocity_ = vel;
    shownLevel_ = shown;

    if (changed)
    {
        repaint(toPixels({ kFrameL, 12.0f, kFrameR - kFrameL, kBandBottom - 12.0f }));
        repaint(toPixels({ kFrameL, kStripTop, kFrameR - kFrameL, kStatusTop - kStripTop }));
        repaint(toPixels({ kFrameL, kStatusTop, kFrameR - kFrameL, kFrameB - kStatusTop }));
    }
}

void NotaEditor::drawReadout(juce::Graphics& g) const
{
    const juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::translation(0.0f, -16.0f));

    // The note, large; dim while nothing is held, remembering the last one.
    g.setColour(ink(0.38f));
    g.setFont(mono(8.5f, false, 0.1f));
    g.drawText("DETECTED", juce::Rectangle<float>(40.0f, 44.0f, 120.0f, 11.0f), juce::Justification::left, false);

    const int shown = note_ >= 0 ? note_ : lastNote_;
    g.setColour(note_ >= 0 ? kCoral : ink(0.28f));
    g.setFont(mono(shown >= 0 ? 54.0f : 30.0f, true));
    g.drawText(shown >= 0 ? noteName(shown) : juce::String("--"), juce::Rectangle<float>(38.0f, 48.0f, 180.0f, 66.0f), juce::Justification::centredLeft, false);

    // Frequency and MIDI number.
    const auto pair = [&](float x, const char* label, const juce::String& value) {
        g.setColour(ink(0.62f));
        g.setFont(mono(8.5f, false, 0.02f));
        g.drawText(label, juce::Rectangle<float>(x, 44.0f, 100.0f, 11.0f), juce::Justification::left, false);
        g.setColour(ink(0.85f));
        g.setFont(mono(16.0f, true));
        g.drawText(value, juce::Rectangle<float>(x, 58.0f, 110.0f, 22.0f), juce::Justification::left, false);
    };
    pair(236.0f, "FREQ", freq_ > 0.0f ? juce::String(freq_, 1) + " Hz" : juce::String("--"));
    pair(356.0f, "MIDI", note_ >= 0 ? juce::String(note_) : juce::String("--"));

    // Cents off the nearest semitone, as a bipolar meter.
    const float tuning = proc_.apvts.getRawParameterValue("tuning")->load();
    float cents = 0.0f;
    if (freq_ > 0.0f)
    {
        const float midi = 69.0f + 12.0f * std::log2(freq_ / tuning);
        cents = (midi - std::round(midi)) * 100.0f;
    }
    g.setColour(ink(0.62f));
    g.setFont(mono(8.5f, false, 0.02f));
    g.drawText("CENTS", juce::Rectangle<float>(478.0f, 44.0f, 100.0f, 11.0f), juce::Justification::left, false);
    g.setColour(ink(0.85f));
    g.setFont(mono(16.0f, true));
    g.drawText(freq_ > 0.0f ? (cents > 0.5f ? "+" : "") + juce::String(juce::roundToInt(cents)) : juce::String("--"),
               juce::Rectangle<float>(478.0f, 58.0f, 80.0f, 22.0f), juce::Justification::left, false);

    drawMeter(g, { 560.0f, 66.0f, 124.0f, 9.0f }, freq_ > 0.0f ? cents / 50.0f : 0.0f, kCoral, true);
    g.setColour(ink(0.38f));
    g.setFont(mono(7.5f));
    g.drawText("-50", juce::Rectangle<float>(560.0f, 80.0f, 30.0f, 10.0f), juce::Justification::left, false);
    g.drawText("+50", juce::Rectangle<float>(654.0f, 80.0f, 30.0f, 10.0f), juce::Justification::right, false);
}

void NotaEditor::drawVelocity(juce::Graphics& g) const
{
    // Section title, in the same tracked caps as the columns.
    g.setColour(kTeal);
    g.setFont(mono(11.0f, true, 0.28f));
    g.drawText("VELOCITY", juce::Rectangle<float>(kFrameL + 24.0f, kStripTop + 14.0f, 120.0f, 16.0f), juce::Justification::left, false);

    // The meter for the most recent note-on. Lit while the note is held, dim (but still readable) after.
    const bool held = note_ >= 0;
    const juce::Rectangle<float> bar(470.0f, kStripTop + 36.0f, 214.0f, 10.0f);
    const float vel = static_cast<float>(velocity_);

    const juce::Graphics::ScopedSaveState state(g);
    if (!held)
        g.beginTransparencyLayer(0.45f);

    g.setColour(ink(0.62f));
    g.setFont(mono(8.5f, false, 0.02f));
    g.drawText("NOTE VELOCITY", juce::Rectangle<float>(bar.getX(), kStripTop + 14.0f, 120.0f, 12.0f), juce::Justification::left, false);
    g.setColour(ink(0.85f));
    g.setFont(mono(13.0f, true));
    g.drawText(velocity_ > 0 ? juce::String(velocity_) : juce::String("--"),
               juce::Rectangle<float>(bar.getRight() - 60.0f, kStripTop + 11.0f, 60.0f, 16.0f), juce::Justification::right, false);

    drawMeter(g, bar, velocity_ > 0 ? (vel - 1.0f) / 126.0f : 0.0f, kTeal);

    // The chosen range, marked on the meter so you can see where the note landed inside it.
    const float lo = proc_.apvts.getRawParameterValue("velMin")->load();
    const float hi = proc_.apvts.getRawParameterValue("velMax")->load();
    const auto xFor = [&](float v) { return bar.getX() + (v - 1.0f) / 126.0f * bar.getWidth(); };
    g.setColour(ink(0.55f));
    for (float v : { lo, hi })
        g.fillRect(xFor(v) - 0.5f, bar.getY() - 5.0f, 1.0f, 5.0f);

    g.setColour(ink(0.38f));
    g.setFont(mono(7.5f));
    g.drawText("1", juce::Rectangle<float>(bar.getX(), bar.getBottom() + 4.0f, 30.0f, 10.0f), juce::Justification::left, false);
    g.drawText("127", juce::Rectangle<float>(bar.getRight() - 30.0f, bar.getBottom() + 4.0f, 30.0f, 10.0f), juce::Justification::right, false);

    if (!held)
        g.endTransparencyLayer();
}

void NotaEditor::drawStatus(juce::Graphics& g) const
{
    const auto caption = [&](float x, const char* text) {
        g.setColour(ink(0.62f));
        g.setFont(mono(8.5f, false, 0.02f));
        g.drawText(text, juce::Rectangle<float>(x, (kStatusTop + 11.0f), 70.0f, 14.0f), juce::Justification::left, false);
    };

    caption(36.0f, "LEVEL");
    const juce::Rectangle<float> levelBar(82.0f, (kStatusTop + 13.5f), 150.0f, 9.0f);
    drawMeter(g, levelBar, (shownLevel_ + 70.0f) / 70.0f, kSteel);
    const float gate = proc_.apvts.getRawParameterValue("gate")->load();
    const float gx = levelBar.getX() + juce::jlimit(0.0f, 1.0f, (gate + 70.0f) / 70.0f) * levelBar.getWidth();
    g.setColour(kCoral);
    g.fillRect(gx - 0.75f, (kStatusTop + 10.0f), 1.5f, 16.0f);

    caption(258.0f, "CLARITY");
    drawMeter(g, { 316.0f, (kStatusTop + 13.5f), 110.0f, 9.0f }, clarity_, kSteel);

    drawLamp(g, { 452.0f, (kStatusTop + 13.5f) }, note_ >= 0, kCoral);
    caption(468.0f, "NOTE");

    g.setColour(ink(0.38f));
    g.setFont(mono(8.5f, false, 0.02f));
    g.drawText("audio to midi", juce::Rectangle<float>(520.0f, (kStatusTop + 11.0f), 112.0f, 14.0f), juce::Justification::right, false);
    g.setColour(ink(0.55f));
    g.setFont(mono(11.0f, true, 0.28f));
    g.drawText("NOTA", juce::Rectangle<float>(630.0f, (kStatusTop + 10.0f), 62.0f, 16.0f), juce::Justification::right, false);
}

void NotaEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPaper);
    g.addTransform(juce::AffineTransform::scale(scale()));

    // Frame and rules: hairlines at partial ink, square corners, nothing else.
    g.setColour(ink(0.22f));
    g.drawRect(juce::Rectangle<float>(kFrameL, kFrameT, kFrameR - kFrameL, kFrameB - kFrameT), 1.2f);
    g.setColour(ink(0.18f));
    g.drawLine(kFrameL, kBandBottom, kFrameR, kBandBottom, 1.0f);
    g.drawLine(kFrameL, kStripTop, kFrameR, kStripTop, 1.0f);
    g.drawLine(kFrameL, kStatusTop, kFrameR, kStatusTop, 1.0f);
    g.drawLine(kDiv1, kBandBottom, kDiv1, kStripTop, 1.0f);
    g.drawLine(kDiv2, kBandBottom, kDiv2, kStripTop, 1.0f);

    drawReadout(g);

    // Section titles: tracked bold caps in the section's colour.
    const auto title = [&](float x, const char* text, juce::Colour c) {
        g.setColour(c);
        g.setFont(mono(11.0f, true, 0.28f));
        g.drawText(text, juce::Rectangle<float>(x, kBandBottom + 10.0f, 180.0f, 16.0f), juce::Justification::left, false);
    };
    title(kFrameL + 24.0f, "DETECT", kCoral);
    title(kDiv1 + 24.0f, "NOTE", kTeal);
    title(kDiv2 + 24.0f, "OUTPUT", kSteelText);

    g.setColour(ink(0.62f));
    g.setFont(mono(8.5f, false, 0.02f));
    g.drawText("RANGE", juce::Rectangle<float>(36.0f, kRowBCentre - 27.0f, 192.0f, 12.0f), juce::Justification::centred, false);

    g.drawText("MODE", juce::Rectangle<float>(496.0f, kRowBCentre - 27.0f, 192.0f, 12.0f), juce::Justification::centred, false);

    drawVelocity(g);
    drawStatus(g);
}
