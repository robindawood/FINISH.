#include "Widgets.h"
#include "../Params.h"

using namespace Theme;

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    juce::Path arc (float cx, float cy, float r, float a0, float a1)
    {
        juce::Path p;
        p.addCentredArc (cx, cy, r, r, 0.0f, a0, a1, true);
        return p;
    }
}

// ------------------------------------------------------------------------------------------- LookAndFeel

WhaleLookAndFeel::WhaleLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff120c26));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a1c70));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff120c26));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, lineHi);
    setColour (juce::TextEditor::backgroundColourId, field);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, magenta);
    setColour (juce::Slider::textBoxTextColourId, dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, lineHi);
    setDefaultSansSerifTypeface (typefaces().text);
}

void WhaleLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const float r = (float) juce::jmin (w, h) * 0.5f - 10.0f;
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
    // ticks
    g.setColour (lineHi);
    for (int k = 0; k <= 10; ++k)
    {
        const float a = start + (end - start) * (float) k / 10.0f - pi * 0.5f;
        g.drawLine (cx + (r + 7) * std::cos (a), cy + (r + 7) * std::sin (a), cx + (r + 10) * std::cos (a), cy + (r + 10) * std::sin (a), 1.2f);
    }
    // track + value arc (from the centre for bipolar-neutral macros at 50 %)
    g.setColour (line);
    g.strokePath (arc (cx, cy, r + 3, start, end), juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float mid = (start + end) * 0.5f, ang = start + (end - start) * pos;
    const bool bipolar = s.getProperties()["bipolar"];
    neon (g, arc (cx, cy, r + 3, bipolar ? juce::jmin (mid, ang) : start, bipolar ? juce::jmax (mid, ang) : ang), colour, 3.0f, 0.7f);
    // body
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff302652), cx, cy - r, juce::Colour (0xff120d22), cx, cy + r, false));
    g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    g.setColour (juce::Colour (0xff4a3c7c));
    g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.2f);
    // pointer
    const float a = ang - pi * 0.5f;
    g.setColour (juce::Colour (0xfff5f0ff));
    g.drawLine (cx + r * 0.32f * std::cos (a), cy + r * 0.32f * std::sin (a), cx + r * 0.82f * std::cos (a), cy + r * 0.82f * std::sin (a), 2.6f);
}

void WhaleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const auto colour = s.findColour (juce::Slider::trackColourId);
    g.setColour (line);
    g.fillRoundedRectangle ((float) x, cy - 2.5f, (float) w, 5.0f, 2.5f);
    const bool bipolar = s.getProperties()["bipolar"];
    const float zero = bipolar ? (float) x + (float) w * (float) s.valueToProportionOfLength (0.0) : (float) x;
    g.setColour (colour);
    g.fillRoundedRectangle (juce::jmin (zero, pos), cy - 2.5f, std::abs (pos - zero), 5.0f, 2.5f);
    g.setColour (juce::Colour (0xfff5f0ff));
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void WhaleLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (field);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (false) ? magenta : lineHi);
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    juce::Path tri;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    tri.addTriangle (ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (dim);
    g.fillPath (tri);
}

void WhaleLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 30, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

void WhaleLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (lineHi);
    g.drawRect (0, 0, w, h);
}

void WhaleLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const juce::Rectangle<float> dot (r.getX() + 2, r.getCentreY() - 6, 12, 12);
    g.setColour (on ? green : faint);
    g.fillEllipse (dot);
    g.setColour (hl ? text : dim);
    g.setFont (bold (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (22), juce::Justification::centredLeft);
}

juce::Label* WhaleLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (11.5f));
    l->setColour (juce::Label::textColourId, dim);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

// ------------------------------------------------------------------------------------------- MacroKnob

MacroKnob::MacroKnob (juce::AudioProcessorValueTreeState& st, const char* id, const juce::String& n, juce::Colour c) : name (n), colour (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.getProperties().set ("bipolar", true);
    slider.setDoubleClickReturnValue (true, 0.5);
    slider.setMouseDragSensitivity (220);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, slider);
    slider.onValueChange = [this] { repaint(); };
}

void MacroKnob::resized()
{
    slider.setBounds (getLocalBounds().withHeight (getWidth()));
}

void MacroKnob::paint (juce::Graphics& g)
{
    const int top = getWidth() + 2;
    g.setColour (juce::Colour (0xffd6ccf0));
    g.setFont (bold (13.0f, 0.04f));
    g.drawText (name, 0, top, getWidth(), 16, juce::Justification::centred);
    g.setColour (dim);
    g.setFont (mono (11.0f));
    const int v = juce::roundToInt ((slider.getValue() - 0.5) * 200.0);
    g.drawText (v == 0 ? juce::String ("0") : (v > 0 ? "+" : "") + juce::String (v), 0, top + 17, getWidth(), 14, juce::Justification::centred);
}

// ------------------------------------------------------------------------------------------- AutoButton

void AutoButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 14.0f;
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const juce::Colour c0 = state == 2 ? green : magenta, c1 = cyan;

    // halo
    const float breathe = state == 0 ? 0.5f + 0.5f * std::sin (phase * 2.0f) : 1.0f;
    for (int k = 4; k >= 1; --k)
    {
        g.setColour (c0.withAlpha ((state == 0 ? 0.035f : 0.06f) * breathe * (hover ? 1.6f : 1.0f)));
        g.fillEllipse (cx - R - (float) k * 4.0f, cy - R - (float) k * 4.0f, (R + (float) k * 4.0f) * 2, (R + (float) k * 4.0f) * 2);
    }
    // disc
    g.setGradientFill (juce::ColourGradient (state == 2 ? juce::Colour (0xff145a50) : juce::Colour (0xff4a1a8a), cx, cy - R * 0.3f,
                                             state == 2 ? juce::Colour (0xff0c1622) : juce::Colour (0xff140a2a), cx, cy + R, true));
    g.fillEllipse (cx - R + 10, cy - R + 10, (R - 10) * 2, (R - 10) * 2);
    // ticks
    g.setColour (lineHi.withAlpha (0.8f));
    for (int k = 0; k < 60; ++k)
    {
        const float a = -pi * 0.5f + (float) k * pi / 30.0f, r0 = R + 6, r1 = R + (k % 5 == 0 ? 11.0f : 8.5f);
        g.drawLine (cx + r0 * std::cos (a), cy + r0 * std::sin (a), cx + r1 * std::cos (a), cy + r1 * std::sin (a), 1.0f);
    }
    // ring
    g.setColour (line);
    g.strokePath (arc (cx, cy, R, 0, 2 * pi), juce::PathStrokeType (5.0f));
    const float amount = state == 1 ? juce::jlimit (0.0f, 1.0f, prog) : 1.0f;
    if (amount > 0.002f)
    {
        constexpr int segs = 48;
        for (int i = 0; i < segs; ++i)
        {
            const float f0 = (float) i / segs, f1 = (float) (i + 1) / segs;
            if (f0 >= amount) break;
            const juce::Colour c = c0.interpolatedWith (c1, f0).withAlpha (state == 0 ? 0.45f + 0.25f * breathe : 1.0f);
            g.setColour (c);
            g.strokePath (arc (cx, cy, R, f0 * 2 * pi, juce::jmin (f1, amount) * 2 * pi + 0.01f), juce::PathStrokeType (5.0f));
        }
        if (state == 1)
        {
            const float a = amount * 2 * pi - pi * 0.5f;
            g.setColour (cyan.withAlpha (0.35f));
            g.fillEllipse (cx + R * std::cos (a) - 9, cy + R * std::sin (a) - 9, 18, 18);
            g.setColour (juce::Colours::white);
            g.fillEllipse (cx + R * std::cos (a) - 4, cy + R * std::sin (a) - 4, 8, 8);
        }
    }
    // content
    g.setColour (state == 2 ? green : juce::Colour (0xffece4ff));
    g.fillPath (flukeGlyph (cx, cy - R * 0.45f, R * 0.2f));
    g.setColour (juce::Colours::white);
    g.setFont (display (R * 0.36f));
    g.drawText ("AUTO", juce::Rectangle<float> (cx - R, cy - R * 0.22f, R * 2, R * 0.42f), juce::Justification::centred);
    g.setColour (state == 2 ? green : juce::Colour (0xffd2c4f2));
    g.setFont (mono (R * 0.16f));
    g.drawText (subText, juce::Rectangle<float> (cx - R, cy + R * 0.25f, R * 2, R * 0.2f), juce::Justification::centred);
}

// ------------------------------------------------------------------------------------------- ChainStrip

const juce::StringArray& ChainStrip::names()
{
    static const juce::StringArray n { "SPECTRAL", "COMPRESSION", "SATURATION", "LARGEUR", "BASSES / AIR", "CLIPPER", "LIMITEUR" };
    return n;
}

ChainStrip::ChainStrip (juce::AudioProcessorValueTreeState& s) : state (s) {}

juce::Rectangle<float> ChainStrip::chipBounds (int i) const
{
    const float labelW = 64.0f, gap = 26.0f;
    const float w = ((float) getWidth() - labelW - gap * 6.0f) / 7.0f;
    return { labelW + (float) i * (w + gap), 4.0f, w, (float) getHeight() - 8.0f };
}

void ChainStrip::paint (juce::Graphics& g)
{
    const char* ids[7] = { PID::specOn, PID::compOn, PID::satOn, PID::widthOn, PID::toneOn, PID::clipOn, nullptr };
    g.setColour (dim);
    g.setFont (mono (11.0f, 0.08f));
    g.drawText (fr ("CHAÎNE"), 0, 0, 64, getHeight(), juce::Justification::centredLeft);
    for (int i = 0; i < 7; ++i)
    {
        const auto r = chipBounds (i);
        const bool on = ids[i] == nullptr || state.getRawParameterValue (ids[i])->load() > 0.5f;
        g.setColour (i == selected ? juce::Colour (0xff2a1450) : field);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (i == selected ? magenta : line.brighter (0.2f));
        g.drawRoundedRectangle (r, 8.0f, 1.2f);
        g.setColour (on ? green : faint);
        g.fillEllipse (r.getX() + 11, r.getCentreY() - 5, 10, 10);
        g.setColour (on ? text : dim);
        g.setFont (bold (12.5f, 0.03f));
        g.drawText (names()[i], r.withTrimmedLeft (28).withTrimmedRight (20), juce::Justification::centredLeft);
        // activity bar
        const juce::Rectangle<float> bar (r.getRight() - 15, r.getY() + 7, 5, r.getHeight() - 14);
        g.setColour (line);
        g.fillRoundedRectangle (bar, 2.0f);
        const float a = on ? juce::jlimit (0.0f, 1.0f, activity[(size_t) i]) : 0.0f;
        g.setColour (magenta);
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * a), 2.0f);
        if (i < 6)
        {
            const float ax = r.getRight() + 5, ay = r.getCentreY();
            g.setColour (lineHi);
            g.drawLine (ax, ay, ax + 12, ay, 1.2f);
            juce::Path tri; tri.addTriangle (ax + 12, ay - 4, ax + 18, ay, ax + 12, ay + 4);
            g.fillPath (tri);
        }
    }
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    const char* ids[7] = { PID::specOn, PID::compOn, PID::satOn, PID::widthOn, PID::toneOn, PID::clipOn, nullptr };
    for (int i = 0; i < 7; ++i)
    {
        const auto r = chipBounds (i);
        if (! r.contains (e.position)) continue;
        if (e.position.x < r.getX() + 26 && ids[i] != nullptr)
        {
            auto* p = state.getParameter (ids[i]);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
            repaint();
        }
        else if (onOpenModule) onOpenModule (i);
        return;
    }
}

// ------------------------------------------------------------------------------------------- MeterPanel

void MeterPanel::paint (juce::Graphics& g)
{
    const float rowH = (float) getHeight() / (float) numRows;
    for (int i = 0; i < numRows; ++i)
    {
        const auto& r = rows[(size_t) i];
        const float y = (float) i * rowH;
        g.setColour (juce::Colour (0xffb2a6d6));
        g.setFont (bold (12.0f));
        g.drawText (r.label, juce::Rectangle<float> (0, y, (float) getWidth() * 0.62f, rowH - 8), juce::Justification::bottomLeft);
        g.setColour (r.colour);
        g.setFont (mono (13.0f));
        g.drawText (r.value, juce::Rectangle<float> (0, y, (float) getWidth(), rowH - 8), juce::Justification::bottomRight);
        const juce::Rectangle<float> bar (0, y + rowH - 6, (float) getWidth(), 3);
        g.setColour (line);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (r.colour.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, r.bar)), 2.0f);
    }
}

// ------------------------------------------------------------------------------------------- PresetBar

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xee100a22));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (isOpen ? magenta : lineHi);
    g.drawRoundedRectangle (r, 9.0f, 1.2f);
    const float h = r.getHeight(), cy = r.getCentreY();
    auto triangle = [&] (float x, bool left)
    {
        juce::Path p;
        if (left) p.addTriangle (x + 4, cy - 6, x - 4, cy, x + 4, cy + 6); else p.addTriangle (x - 4, cy - 6, x + 4, cy, x - 4, cy + 6);
        g.setColour (juce::Colour (0xffbeb2e6));
        g.fillPath (p);
    };
    triangle (20, true);
    triangle (r.getRight() - 20, false);
    g.setColour (line);
    g.drawVerticalLine (40, 6, h - 6);
    g.drawVerticalLine ((int) r.getRight() - 40, 6, h - 6);
    g.drawVerticalLine ((int) r.getRight() - 160, 6, h - 6);
    // star
    juce::Path star;
    star.addStar ({ 60, cy }, 5, 4.0f, 9.0f, 0.0f);
    if (favourite) { g.setColour (magenta); g.fillPath (star); }
    else { g.setColour (dim); g.strokePath (star, juce::PathStrokeType (1.2f)); }
    g.setColour (text);
    g.setFont (bold (16.0f));
    g.drawText (name, juce::Rectangle<float> (78, 0, r.getRight() - 300, h), juce::Justification::centredLeft);
    const float nw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
    g.setColour (dim);
    g.setFont (mono (11.5f, 0.06f));
    g.drawText (category, juce::Rectangle<float> (88 + nw, 0, 200, h), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffd6ccf0));
    g.setFont (bold (13.0f, 0.06f));
    g.drawText ("PRESETS", juce::Rectangle<float> (r.getRight() - 150, 0, 90, h), juce::Justification::centredLeft);
    juce::Path tri;
    tri.addTriangle (r.getRight() - 60, cy - 2, r.getRight() - 50, cy - 2, r.getRight() - 55, cy + 4);
    g.fillPath (tri);
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    const float x = e.position.x, w = (float) getWidth();
    if (x < 40) { if (onStep) onStep (-1); }
    else if (x > w - 40) { if (onStep) onStep (1); }
    else if (x < 74) { if (onFavourite) onFavourite(); }
    else if (onOpen) onOpen();
}

// ------------------------------------------------------------------------------------------- ABSwitch

ABSwitch::ABSwitch (juce::AudioProcessorValueTreeState& s) : state (s) { state.addParameterListener (PID::bypass, this); }
ABSwitch::~ABSwitch() { state.removeParameterListener (PID::bypass, this); }

void ABSwitch::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    const auto capArea = all.removeFromBottom (13.0f);
    g.setColour (dim);
    g.setFont (mono (9.5f));
    g.drawText (caption, capArea, juce::Justification::centred);
    const auto r = all.reduced (0.5f);
    const bool bypass = state.getRawParameterValue (PID::bypass)->load() > 0.5f;
    g.setColour (juce::Colour (0xee0e0920));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (lineHi);
    g.drawRoundedRectangle (r, 8.0f, 1.2f);
    const auto half = r.reduced (3).withWidth (r.getWidth() * 0.5f - 3);
    const auto sel = bypass ? half : half.translated (r.getWidth() * 0.5f - 0.0f, 0);
    g.setColour (juce::Colour (0xff38206e));
    g.fillRoundedRectangle (sel, 6.0f);
    g.setFont (bold (13.5f, 0.05f));
    g.setColour (bypass ? text : dim);
    g.drawText ("SANS", r.withWidth (r.getWidth() * 0.5f), juce::Justification::centred);
    g.setColour (bypass ? dim : text);
    g.drawText ("AVEC", r.withTrimmedLeft (r.getWidth() * 0.5f), juce::Justification::centred);
}

void ABSwitch::mouseUp (const juce::MouseEvent& e)
{
    auto* p = state.getParameter (PID::bypass);
    if (e.position.y > (float) getHeight() - 13.0f) return;   // caption
    const bool wantBypass = e.position.x < (float) getWidth() * 0.5f;
    p->beginChangeGesture();
    p->setValueNotifyingHost (wantBypass ? 1.0f : 0.0f);
    p->endChangeGesture();
    repaint();
}
