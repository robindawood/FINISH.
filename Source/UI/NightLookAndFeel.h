#pragma once
#include "NightTheme.h"

// Hardware look: LED-ring knobs with knurled caps, rubber push buttons with LEDs, neon "primary" key,
// short LED fader, LCD text fields. Button style via the "style" property: (default) hw, "chip", "primary", "link".
class NightLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NightLookAndFeel()
    {
        using namespace Night;
        setColour (juce::ResizableWindow::backgroundColourId, bg0);
        setColour (juce::Slider::textBoxTextColourId, cyan);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, violet.withAlpha (0.4f));
        setColour (juce::TextButton::buttonColourId, panelHi);
        setColour (juce::TextButton::buttonOnColourId, panelHi);
        setColour (juce::TextButton::textColourOffId, mist);
        setColour (juce::TextButton::textColourOnId, moon);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff07040e));
        setColour (juce::TextEditor::textColourId, cyan);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, cyan.withAlpha (0.6f));
        setColour (juce::TextEditor::highlightColourId, violet.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, cyan);
        setColour (juce::Label::textColourId, moon);
        setColour (juce::PopupMenu::backgroundColourId, bg1);
        setColour (juce::PopupMenu::textColourId, moon);
        setColour (juce::PopupMenu::headerTextColourId, dusk);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, panelHi.brighter (0.15f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, bg1);
        setColour (juce::TooltipWindow::textColourId, moon);
        setColour (juce::TooltipWindow::outlineColourId, lineHi);
        setDefaultSansSerifTypeface (Night::typefaces().text);
    }

    // ---- Knobs: LED ring + knurled cap ------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace Night;
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const bool bipolar = s.getProperties()["bipolar"];
        const bool lunar = s.getProperties()["lunar"];
        const bool enabled = s.isEnabled();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // LED ring
        const int dots = radius > 30.0f ? 25 : 19;
        const float ringR = radius - 3.0f;
        const float centre = 0.5f;
        for (int i = 0; i < dots; ++i)
        {
            const float t = (float) i / (float) (dots - 1);
            const float a = startAngle + t * (endAngle - startAngle);
            const bool lit = enabled && (bipolar ? ((t >= std::min (centre, pos) - 0.001f && t <= std::max (centre, pos) + 0.001f))
                                                 : (t <= pos + 0.001f && pos > 0.002f));
            const auto col = lunar ? moon.interpolatedWith (cyan, t * 0.6f) : auroraAt (t);
            const juce::Point<float> p (c.x + ringR * std::sin (a), c.y - ringR * std::cos (a));
            const float dr = radius > 30.0f ? 2.1f : 1.7f;
            if (lit)
            {
                g.setColour (col.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (dr * 5.0f, dr * 5.0f).withCentre (p));
                g.setColour (col.interpolatedWith (juce::Colours::white, 0.3f));
            }
            else g.setColour (juce::Colour (0xff2a2140));
            g.fillEllipse (juce::Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (p));
        }

        // Skirt + shadow
        const float skirtR = ringR - 8.0f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (skirtR * 2.0f + 6.0f, skirtR * 2.0f + 6.0f).withCentre (c.translated (0, 3.0f)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3150), c.x, c.y - skirtR, juce::Colour (0xff0b0712), c.x, c.y + skirtR, false));
        g.fillEllipse (juce::Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c));

        // Knurling
        const float capR = skirtR * 0.82f;
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        const int ridges = 36;
        for (int i = 0; i < ridges; ++i)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) ridges;
            g.drawLine (c.x + capR * std::sin (a), c.y - capR * std::cos (a), c.x + skirtR * 0.97f * std::sin (a), c.y - skirtR * 0.97f * std::cos (a), 1.0f);
        }
        // Cap: brushed metal top with a soft highlight
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a4e74), c.x - capR, c.y - capR, juce::Colour (0xff15101f), c.x + capR, c.y + capR, false));
        g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
        juce::ColourGradient spec (juce::Colours::white.withAlpha (0.18f), c.x - capR * 0.4f, c.y - capR * 0.5f, juce::Colours::white.withAlpha (0.0f), c.x, c.y + capR * 0.2f, true);
        g.setGradientFill (spec);
        g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c), 1.0f);

        // Pointer (luminous)
        const auto col = lunar ? moon : auroraAt (pos);
        const juce::Point<float> a0 (c.x + capR * 0.25f * std::sin (angle), c.y - capR * 0.25f * std::cos (angle));
        const juce::Point<float> a1 (c.x + capR * 0.9f * std::sin (angle), c.y - capR * 0.9f * std::cos (angle));
        juce::Path ptr;
        ptr.startNewSubPath (a0);
        ptr.lineTo (a1);
        glowStroke (g, ptr, enabled ? col : dusk, 2.2f, 0.9f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        using namespace Night;
        const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float cy = r.getCentreY();
        auto groove = juce::Rectangle<float> (r.getX(), cy - 3.0f, r.getWidth(), 6.0f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (groove, 3.0f);
        auto fill = groove.reduced (1.0f).withRight (sliderPos);
        g.setGradientFill (aurora (groove.getX(), cy, groove.getRight(), cy, s.isEnabled() ? 1.0f : 0.3f));
        g.fillRoundedRectangle (fill, 2.0f);
        g.setColour (violet.withAlpha (0.2f));
        g.fillRoundedRectangle (fill.expanded (0, 3.0f), 4.0f);
        auto cap = juce::Rectangle<float> (12.0f, 20.0f).withCentre ({ sliderPos, cy });
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (cap.translated (0, 2), 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5d86), 0, cap.getY(), juce::Colour (0xff221a31), 0, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (moon);
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 4.0f, 1.5f));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (Night::mono (10.5f));
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::textColourId, Night::cyan.withAlpha (0.85f));
        l->setColour (juce::Label::outlineWhenEditingColourId, Night::cyan);
        return l;
    }

    // ---- Buttons ----------------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        using namespace Night;
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const auto style = b.getProperties()["style"].toString();
        if (style == "link")
        {
            if (hover) { g.setColour (cyan.withAlpha (0.08f)); g.fillRoundedRectangle (r, 4.0f); }
            return;
        }
        const bool on = b.getToggleState();
        const float alpha = b.isEnabled() ? 1.0f : 0.4f;
        const float radius = style == "chip" ? 5.0f : 6.0f;

        if (style == "primary")
        {
            // Illuminated key: dark glass with a neon tube border.
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (r.translated (0, 2.0f), radius);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e1430), 0, r.getY(), juce::Colour (0xff0b0714), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            if (b.isEnabled())
            {
                for (int i = 3; i >= 1; --i)
                {
                    g.setGradientFill (aurora (r.getX(), 0, r.getRight(), 0, (hover ? 0.12f : 0.07f)));
                    g.drawRoundedRectangle (r.expanded ((float) i * 1.6f), radius + (float) i, 2.5f);
                }
                g.setGradientFill (aurora (r.getX(), 0, r.getRight(), 0, down ? 0.25f : (hover ? 0.18f : 0.1f)));
                g.fillRoundedRectangle (r, radius);
            }
            g.setGradientFill (aurora (r.getX(), 0, r.getRight(), 0, alpha));
            g.drawRoundedRectangle (r, radius, 1.6f);
            return;
        }

        // Rubber hardware key
        const float press = down ? 1.0f : 0.0f;
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (r.translated (0, 2.5f - press), radius);
        auto key = r.translated (0, press);
        g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xff3a2e52 : 0xff302643).withMultipliedAlpha (alpha), 0, key.getY(),
                                                 juce::Colour (0xff15101f).withMultipliedAlpha (alpha), 0, key.getBottom(), false));
        g.fillRoundedRectangle (key, radius);
        g.setColour (juce::Colours::white.withAlpha (0.09f * alpha));
        g.drawHorizontalLine ((int) key.getY() + 1, key.getX() + radius, key.getRight() - radius);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (key, radius, 1.0f);
        if (style != "chip")
        {
            // LED window on the key
            const auto led = juce::Point<float> (key.getX() + 12.0f, key.getCentreY());
            drawLed (g, led, 2.6f, b.getProperties()["led"].toString() == "amber" ? amber : cyan, on);
        }
        else if (on)
        {
            g.setColour (cyan.withAlpha (0.5f));
            g.drawRoundedRectangle (key, radius, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        using namespace Night;
        const auto style = b.getProperties()["style"].toString();
        auto r = b.getLocalBounds().toFloat();
        if (style == "link")
        {
            g.setColour (b.isOver() ? moon : cyan);
            g.setFont (bold (12.0f));
            g.drawText (b.getButtonText(), r.withTrimmedLeft (2), juce::Justification::centredLeft, true);
            return;
        }
        const float alpha = b.isEnabled() ? 1.0f : 0.4f;
        if (style == "primary")
        {
            neonText (g, b.getButtonText(), display (10.5f, 0.12f), r, b.isEnabled() ? cyan : dusk, juce::Justification::centred, alpha * 0.8f);
            return;
        }
        if (down) r.translate (0, 1.0f);
        if (style != "chip") r.removeFromLeft (18.0f);
        g.setFont (label (style == "chip" ? 7.5f : 8.0f));
        g.setColour ((b.getToggleState() || b.isOver() ? moon : mist).withMultipliedAlpha (alpha));
        g.drawText (b.getButtonText(), r, juce::Justification::centred, false);
    }

    // ---- LCD fields / popups / tooltips -----------------------------------------------------
    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&) override
    {
        Night::drawScreen (g, juce::Rectangle<int> (w, h).toFloat().reduced (3.0f), 4.0f, true);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        if (e.hasKeyboardFocus (true))
        {
            g.setColour (Night::cyan.withAlpha (0.45f));
            g.drawRoundedRectangle (juce::Rectangle<int> (w, h).toFloat().reduced (3.0f), 4.0f, 1.0f);
        }
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (Night::bg1);
        g.setColour (Night::lineHi);
        g.drawRect (0, 0, w, h, 1);
    }

    juce::Font getPopupMenuFont() override { return Night::text (13.5f); }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.setColour (Night::bg1);
        g.fillRoundedRectangle (juce::Rectangle<int> (w, h).toFloat(), 5.0f);
        g.setColour (Night::lineHi);
        g.drawRoundedRectangle (juce::Rectangle<int> (w, h).toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (Night::moon);
        g.setFont (Night::text (12.5f));
        g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (10, 6), juce::Justification::centredLeft, 4);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const float tw = juce::GlyphArrangement::getStringWidth (Night::text (12.5f), text);
        const int w = std::min (320, (int) tw + 24);
        const int lines = std::max (1, (int) std::ceil (tw / 296.0f));
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - w - 8 : pos.x + 14, pos.y + 18, w, 14 + lines * 17).constrainedWithin (parent);
    }
};
