#pragma once

// The Sungam design system (github.com/SungamMagnus/sungam-design-system) as JUCE primitives,
// in its dark, simplified form: the same flat monospace panel, hairlines and arc knobs, drawn
// with the system's own ink as the surface and its paper as the ink. No circuit wiring, no
// dashed trim legs, no shadows. Colour is signal: coral = detection path, teal = note
// behaviour, steel = output. (Violet is modulation only and amber the limiter only; Nota has
// neither, so neither appears.)

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace sungam
{

inline const juce::Colour kPaper(0xff1a1a17);      // the system's --ink, now the surface
inline const juce::Colour kCream(0xfff0ece2);      // the system's --paper, now the ink
inline const juce::Colour kCoral(0xffed8159);
inline const juce::Colour kTeal(0xff52b0a4);
inline const juce::Colour kSteel(0xff4f7ea8);
inline const juce::Colour kSteelText(0xff7aa6cf);  // steel lifted for small type on dark

inline juce::Colour ink(float alpha) { return kCream.withAlpha(alpha); }

inline juce::FontOptions mono(float height, bool bold = false, float tracking = 0.0f)
{
    return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, bold ? juce::Font::bold : juce::Font::plain)
        .withKerningFactor(tracking);
}

// Draws in design pixels; the component's real size only sets the scale.
class DsComponent : public juce::Component
{
public:
    DsComponent(float designW, float designH) : dw(designW), dh(designH) { setOpaque(false); }

    float designWidth() const { return dw; }
    float designHeight() const { return dh; }

    void paint(juce::Graphics& g) override
    {
        g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / dw));
        draw(g);
    }

protected:
    virtual void draw(juce::Graphics& g) = 0;
    float scale() const { return static_cast<float>(getWidth()) / dw; }

    float dw, dh;
};

// One circle, one arc, one pointer. Unipolar arcs grow from the anticlockwise stop; bipolar ones
// from the parameter's default (noon). Drag vertically; shift = fine; double-click = default.
class Knob : public DsComponent
{
public:
    using Format = std::function<juce::String(float)>;

    Knob(juce::RangedAudioParameter& p, const juce::String& labelText, juce::Colour accentColour, juce::Colour valueColour,
         float radius, bool isBipolar, Format fmt)
        : DsComponent(juce::jmax(2.0f * radius + 24.0f, 68.0f), 44.0f + 2.0f * radius),
          param(p), label(labelText), accent(accentColour), valueText(valueColour), r(radius), bipolar(isBipolar), format(std::move(fmt)),
          attachment(p, [this](float) { repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void setActive(bool a)
    {
        if (active != a)
        {
            active = a;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStart = param.getValue();
        attachment.beginGesture();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float perPixel = e.mods.isShiftDown() ? 1.0f / 900.0f : 1.0f / 220.0f;
        const float norm = juce::jlimit(0.0f, 1.0f, dragStart - static_cast<float>(e.getDistanceFromDragStartY()) * perPixel);
        attachment.setValueAsPartOfGesture(param.convertFrom0to1(norm));
    }

    void mouseUp(const juce::MouseEvent&) override { attachment.endGesture(); }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        attachment.setValueAsCompleteGesture(param.convertFrom0to1(param.getDefaultValue()));
    }

    void draw(juce::Graphics& g) override
    {
        if (!active)
            g.beginTransparencyLayer(0.38f);

        constexpr float sweep = 158.6f * juce::MathConstants<float>::pi / 180.0f;
        const auto angleFor = [&](float n) { return -sweep + 2.0f * sweep * juce::jlimit(0.0f, 1.0f, n); };

        const float norm = param.getValue();
        const float cx = dw * 0.5f, cy = 34.0f + r;
        const float fs = r >= 30.0f ? 10.0f : r >= 18.0f ? 9.0f : 8.0f;

        g.setColour(ink(0.62f));
        g.setFont(mono(fs, false, 0.02f));
        g.drawText(label.toUpperCase(), juce::Rectangle<float>(0.0f, 0.0f, dw, fs + 3.0f), juce::Justification::centred, false);
        g.setColour(valueText);
        g.setFont(mono(fs + 2.0f, true));
        g.drawText(format(param.convertFrom0to1(norm)), juce::Rectangle<float>(0.0f, fs + 3.0f, dw, fs + 5.0f), juce::Justification::centred, false);

        const float aStart = angleFor(0.0f), aEnd = angleFor(1.0f), aNow = angleFor(norm);
        const float from = bipolar ? angleFor(param.getDefaultValue()) : aStart;
        const float track = r + 4.5f;
        const float w = r >= 24.0f ? 3.4f : r >= 16.0f ? 2.8f : 2.2f;

        g.setColour(kPaper);
        g.fillEllipse(cx - r, cy - r, 2.0f * r, 2.0f * r);
        g.setColour(ink(0.38f));
        g.drawEllipse(cx - r, cy - r, 2.0f * r, 2.0f * r, 1.2f);

        juce::Path trackPath;
        trackPath.addCentredArc(cx, cy, track, track, 0.0f, aStart, aEnd, true);
        g.setColour(ink(0.13f));
        g.strokePath(trackPath, juce::PathStrokeType(w));

        if (std::abs(aNow - from) > 1e-3f)
        {
            juce::Path arc;
            arc.addCentredArc(cx, cy, track, track, 0.0f, juce::jmin(from, aNow), juce::jmax(from, aNow), true);
            g.setColour(accent);
            g.strokePath(arc, juce::PathStrokeType(w));
        }

        const auto pt = [&](float radius, float a) {
            return juce::Point<float>(cx + radius * std::sin(a), cy - radius * std::cos(a));
        };
        g.setColour(ink(0.85f));
        g.drawLine(juce::Line<float>(pt(r * 0.16f, aNow), pt(r * 0.78f, aNow)), r >= 24.0f ? 2.0f : 1.5f);
        g.setColour(ink(0.28f));
        g.drawLine(juce::Line<float>(pt(r + 1.5f, aStart), pt(r + 6.5f, aStart)), 1.0f);

        if (!active)
            g.endTransparencyLayer();
    }

private:
    juce::RangedAudioParameter& param;
    juce::String label;
    juce::Colour accent, valueText;
    float r;
    bool bipolar;
    Format format;
    juce::ParameterAttachment attachment;
    float dragStart = 0.0f;
    bool active = true;
};

// Filled and coloured when on, an outline when off. The panel's toggle.
class Latch : public DsComponent
{
public:
    Latch(juce::RangedAudioParameter& p, const juce::String& text, juce::Colour c, float width = 84.0f)
        : DsComponent(width, 17.0f), param(p), label(text), colour(c), attachment(p, [this](float) { repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void mouseDown(const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture(param.getValue() > 0.5f ? 0.0f : 1.0f); }

    void draw(juce::Graphics& g) override
    {
        const bool on = param.getValue() > 0.5f;
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, dw, dh);
        if (on)
        {
            g.setColour(colour);
            g.fillRect(area);
        }
        g.setColour(on ? colour : ink(0.28f));
        g.drawRect(area.reduced(0.6f), 1.2f);
        g.setColour(on ? kPaper : ink(0.62f));
        g.setFont(mono(8.0f, false, 0.02f));
        g.drawText(label, area, juce::Justification::centred, false);
    }

private:
    juce::RangedAudioParameter& param;
    juce::String label;
    juce::Colour colour;
    juce::ParameterAttachment attachment;
};

// N-position selector; every position stays labelled.
class Selector : public DsComponent
{
public:
    Selector(juce::AudioParameterChoice& p, juce::Colour c, float width = 192.0f)
        : DsComponent(width, 22.0f), param(p), colour(c), attachment(p, [this](float) { repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int n = param.choices.size();
        const int idx = juce::jlimit(0, n - 1, static_cast<int>(e.position.x / scale() / (dw / static_cast<float>(n))));
        attachment.setValueAsCompleteGesture(static_cast<float>(idx));
    }

    void draw(juce::Graphics& g) override
    {
        const int n = param.choices.size();
        const int sel = param.getIndex();
        const float segW = dw / static_cast<float>(n);
        for (int i = 0; i < n; ++i)
        {
            const auto seg = juce::Rectangle<float>(segW * static_cast<float>(i), 0.0f, segW, dh);
            if (i == sel)
            {
                g.setColour(colour);
                g.fillRect(seg);
            }
            else if (i < n - 1)
            {
                g.setColour(ink(0.18f));
                g.drawLine(seg.getRight(), 0.0f, seg.getRight(), dh, 1.0f);
            }
            g.setColour(i == sel ? kPaper : ink(0.62f));
            g.setFont(mono(9.0f, false, 0.02f));
            g.drawText(param.choices[i].toUpperCase(), seg, juce::Justification::centred, false);
        }
        g.setColour(ink(0.28f));
        g.drawRect(juce::Rectangle<float>(0.0f, 0.0f, dw, dh).reduced(0.5f), 1.0f);
    }

private:
    juce::AudioParameterChoice& param;
    juce::Colour colour;
    juce::ParameterAttachment attachment;
};

// A min-max range on one bar: two handles, the span between them filled. Drag the nearer handle;
// the handles cannot cross. Double-click resets both. (Built from the system's Meter and Fader
// handle: a bar with outline and fill, ink handles.)
class RangeBar : public DsComponent
{
public:
    RangeBar(juce::RangedAudioParameter& lowParam, juce::RangedAudioParameter& highParam, const juce::String& labelText,
             juce::Colour c, float width = 200.0f)
        : DsComponent(width, 40.0f), lo(lowParam), hi(highParam), label(labelText), colour(c),
          loAttach(lowParam, [this](float) { repaint(); }), hiAttach(highParam, [this](float) { repaint(); })
    {
        loAttach.sendInitialUpdate();
        hiAttach.sendInitialUpdate();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const float x = e.position.x / scale();
        const float loX = xFor(lo.getValue()), hiX = xFor(hi.getValue());
        // Nearest handle; when they sit together, the side you grab decides.
        draggingLow = std::abs(x - loX) < std::abs(x - hiX) || (juce::approximatelyEqual(loX, hiX) && x < loX);
        (draggingLow ? loAttach : hiAttach).beginGesture();
        mouseDrag(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float norm = juce::jlimit(0.0f, 1.0f, (e.position.x / scale() - kPad) / (dw - 2.0f * kPad));
        if (draggingLow)
        {
            const float v = juce::jmin(lo.convertFrom0to1(norm), hi.convertFrom0to1(hi.getValue()));
            loAttach.setValueAsPartOfGesture(v);
        }
        else
        {
            const float v = juce::jmax(hi.convertFrom0to1(norm), lo.convertFrom0to1(lo.getValue()));
            hiAttach.setValueAsPartOfGesture(v);
        }
    }

    void mouseUp(const juce::MouseEvent&) override { (draggingLow ? loAttach : hiAttach).endGesture(); }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        loAttach.setValueAsCompleteGesture(lo.convertFrom0to1(lo.getDefaultValue()));
        hiAttach.setValueAsCompleteGesture(hi.convertFrom0to1(hi.getDefaultValue()));
    }

    void draw(juce::Graphics& g) override
    {
        const int a = juce::roundToInt(lo.convertFrom0to1(lo.getValue()));
        const int b = juce::roundToInt(hi.convertFrom0to1(hi.getValue()));

        g.setColour(ink(0.62f));
        g.setFont(mono(8.5f, false, 0.02f));
        g.drawText(label.toUpperCase(), juce::Rectangle<float>(0.0f, 0.0f, dw * 0.5f, 12.0f), juce::Justification::left, false);
        g.setColour(colour);
        g.setFont(mono(10.5f, true));
        g.drawText(juce::String(a) + " - " + juce::String(b), juce::Rectangle<float>(dw * 0.4f, -1.0f, dw * 0.6f, 14.0f),
                   juce::Justification::right, false);

        const juce::Rectangle<float> bar(kPad, 22.0f, dw - 2.0f * kPad, 10.0f);
        g.setColour(kPaper);
        g.fillRect(bar);
        const float x0 = xFor(lo.getValue()), x1 = xFor(hi.getValue());
        g.setColour(colour.withAlpha(0.6f));
        g.fillRect(juce::Rectangle<float>(x0, bar.getY() + 1.0f, juce::jmax(0.0f, x1 - x0), bar.getHeight() - 2.0f));
        g.setColour(ink(0.30f));
        g.drawRect(bar, 1.0f);

        // Handles: ink, like a fader's.
        g.setColour(ink(0.92f));
        g.fillRect(juce::Rectangle<float>(x0 - 2.0f, bar.getY() - 3.0f, 4.0f, bar.getHeight() + 6.0f));
        g.fillRect(juce::Rectangle<float>(x1 - 2.0f, bar.getY() - 3.0f, 4.0f, bar.getHeight() + 6.0f));
    }

private:
    static constexpr float kPad = 6.0f;
    float xFor(float norm) const { return kPad + norm * (dw - 2.0f * kPad); }

    juce::RangedAudioParameter& lo;
    juce::RangedAudioParameter& hi;
    juce::String label;
    juce::Colour colour;
    juce::ParameterAttachment loAttach, hiAttach;
    bool draggingLow = true;
};

// ---- drawing helpers for the parts of the panel that are not controls -----------------------------

// Bar meter: unipolar fills from the left, bipolar from a centre tick.
inline void drawMeter(juce::Graphics& g, juce::Rectangle<float> r, float value, juce::Colour c, bool bipolar = false)
{
    g.setColour(kPaper);
    g.fillRect(r);
    g.setColour(ink(0.30f));
    g.drawRect(r, 1.0f);
    if (bipolar)
    {
        const float v = juce::jlimit(-1.0f, 1.0f, value);
        const float mid = r.getCentreX();
        g.setColour(c.withAlpha(0.55f));
        g.fillRect(juce::Rectangle<float>(v >= 0.0f ? mid : mid + v * r.getWidth() * 0.5f, r.getY() + 1.0f,
                                          std::abs(v) * r.getWidth() * 0.5f, r.getHeight() - 2.0f));
        g.setColour(c.withAlpha(0.5f));
        g.fillRect(mid - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
    }
    else if (value > 0.02f)
    {
        g.setColour(c.withAlpha(0.75f));
        g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f, r.getY() + 1.0f, juce::jmax(0.0f, juce::jlimit(0.0f, 1.0f, value) * r.getWidth() - 2.0f), r.getHeight() - 2.0f));
    }
}

// Lit when something is happening, outlined when not; always a small square.
inline void drawLamp(juce::Graphics& g, juce::Point<float> topLeft, bool on, juce::Colour c, float size = 9.0f)
{
    const auto r = juce::Rectangle<float>(topLeft.x, topLeft.y, size, size);
    if (on)
    {
        g.setColour(c);
        g.fillRect(r);
    }
    else
    {
        g.setColour(c.withAlpha(0.45f));
        g.drawRect(r, 1.0f);
    }
}

} // namespace sungam
