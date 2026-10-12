#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

// WHALE look: deep cosmic water, neon violet / magenta / cyan light, sober professional chrome.
namespace Theme
{
    const juce::Colour bg       { 0xff07040f };
    const juce::Colour sky      { 0xff0a0618 };
    const juce::Colour water    { 0xff180c34 };
    const juce::Colour panel    { 0xf6090614 };
    const juce::Colour field    { 0xff120c26 };
    const juce::Colour line     { 0xff2a2046 };
    const juce::Colour lineHi   { 0xff4a3c7c };
    const juce::Colour text     { 0xffe8e1f8 };
    const juce::Colour dim      { 0xff8c80af };
    const juce::Colour faint    { 0xff5a4f80 };
    const juce::Colour magenta  { 0xffd746ff };
    const juce::Colour violet   { 0xff965aff };
    const juce::Colour cyan     { 0xff6ed7ff };
    const juce::Colour green    { 0xff5affbe };
    const juce::Colour orange   { 0xffff8c3c };
    const juce::Colour red      { 0xffff3c50 };

    struct Typefaces
    {
        juce::Typeface::Ptr display, displayRegular, text, bold, mono;
        Typefaces()
        {
            display        = juce::Typeface::createSystemTypefaceFor (BinaryData::UnboundedSemiBold_ttf, BinaryData::UnboundedSemiBold_ttfSize);
            displayRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::UnboundedRegular_ttf, BinaryData::UnboundedRegular_ttfSize);
            text           = juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeMedium_ttf, BinaryData::ManropeMedium_ttfSize);
            bold           = juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeBold_ttf, BinaryData::ManropeBold_ttfSize);
            mono           = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
        }
    };
    inline const Typefaces& typefaces() { static Typefaces t; return t; }
    inline juce::Font make (const juce::Typeface::Ptr& t, float h, float k = 0.0f) { return juce::Font (juce::FontOptions (t).withHeight (h)).withExtraKerningFactor (k); }
    inline juce::Font display (float h, float k = 0.02f) { return make (typefaces().display, h, k); }
    inline juce::Font body (float h)                     { return make (typefaces().text, h); }
    inline juce::Font bold (float h, float k = 0.02f)    { return make (typefaces().bold, h, k); }
    inline juce::Font mono (float h, float k = 0.0f)     { return make (typefaces().mono, h, k); }

    inline juce::String fr (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }

    // Neon line: a wide faint halo + a thin bright core (cheap glow, no blur).
    inline void neon (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w, float glow = 1.0f)
    {
        if (glow > 0.01f)
        {
            g.setColour (c.withMultipliedAlpha (0.10f * glow));
            g.strokePath (p, juce::PathStrokeType (w + 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c.withMultipliedAlpha (0.22f * glow));
            g.strokePath (p, juce::PathStrokeType (w + 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Whale fluke logo glyph centred on (cx, cy), half-width s.
    inline juce::Path flukeGlyph (float cx, float cy, float s)
    {
        juce::Path p;
        for (int side : { -1, 1 })
        {
            juce::Path lobe;
            for (int i = 0; i <= 20; ++i)
            {
                const float t = (float) i / 20.0f;
                const float x = cx + (float) side * s * t, y = cy - s * 0.5f * std::pow (t, 1.3f) - s * 0.1f * std::sin (juce::MathConstants<float>::pi * t);
                if (i == 0) lobe.startNewSubPath (x, y); else lobe.lineTo (x, y);
            }
            for (int i = 20; i >= 0; --i)
            {
                const float t = (float) i / 20.0f;
                lobe.lineTo (cx + (float) side * s * (0.12f + 0.88f * t), cy - s * 0.5f * std::pow (t, 1.3f) + s * 0.26f * std::pow (1.0f - t, 0.9f) - s * 0.02f);
            }
            lobe.closeSubPath();
            p.addPath (lobe);
        }
        p.addQuadrilateral (cx - s * 0.1f, cy + s * 0.1f, cx + s * 0.1f, cy + s * 0.1f, cx + s * 0.05f, cy + s * 0.75f, cx - s * 0.05f, cy + s * 0.75f);
        return p;
    }
}
