#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

// FINISH hardware identity: a synthwave night machine.
// Dark anodised chassis, neon tubes (magenta -> purple -> cyan), CRT screen with scanlines,
// LED rings, 7-segment display, backlit VU meters.
namespace Night
{
    // ---- Palette --------------------------------------------------------------------------
    const juce::Colour void_   { 0xff06030b };
    const juce::Colour bg0     { 0xff0b0714 };
    const juce::Colour bg1     { 0xff160e24 };
    const juce::Colour panel   { 0xff1b1329 };
    const juce::Colour panelHi { 0xff2b2040 };
    const juce::Colour glass   { 0xff07040e };   // screen glass
    const juce::Colour line    { 0xff2c2142 };
    const juce::Colour lineHi  { 0xff4b3a6d };
    const juce::Colour moon    { 0xfff4eeff };   // main text
    const juce::Colour mist    { 0xffb9addb };
    const juce::Colour dusk    { 0xff6f6394 };
    const juce::Colour violet  { 0xffff2bd6 };   // neon magenta (start of the neon gradient)
    const juce::Colour indigo  { 0xff8f5bff };   // neon purple
    const juce::Colour cyan    { 0xff2de2ff };   // neon cyan
    const juce::Colour amber   { 0xffffb340 };
    const juce::Colour rose    { 0xffff4d6d };
    const juce::Colour mint    { 0xff39ffb0 };
    const juce::Colour sun     { 0xffff7a3d };
    const juce::Colour gold    { 0xffffd84d };
    const juce::Colour metalHi { 0xff3a2f50 };
    const juce::Colour metalLo { 0xff120b1d };
    const juce::Colour engrave { 0xff8d80b0 };

    // ---- Fonts (embedded, OFL) -----------------------------------------------------------
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

    inline juce::Font make (const juce::Typeface::Ptr& t, float h, float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions (t).withHeight (h)).withExtraKerningFactor (kerning);
    }
    inline juce::Font display (float h, float kerning = 0.08f)  { return make (typefaces().display, h, kerning); }
    inline juce::Font displayLight (float h, float k = 0.08f)   { return make (typefaces().displayRegular, h, k); }
    inline juce::Font text (float h)                            { return make (typefaces().text, h); }
    inline juce::Font bold (float h)                            { return make (typefaces().bold, h); }
    inline juce::Font mono (float h, float kerning = 0.0f)      { return make (typefaces().mono, h, kerning); }
    inline juce::Font label (float h = 9.5f)                    { return make (typefaces().display, h, 0.16f); }

    // ---- Neon ------------------------------------------------------------------------------
    inline juce::ColourGradient aurora (float x0, float y0, float x1, float y1, float alpha = 1.0f)
    {
        juce::ColourGradient g (violet.withMultipliedAlpha (alpha), x0, y0, cyan.withMultipliedAlpha (alpha), x1, y1, false);
        g.addColour (0.5, indigo.withMultipliedAlpha (alpha));
        return g;
    }

    inline juce::Colour auroraAt (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return t < 0.5f ? violet.interpolatedWith (indigo, t * 2.0f) : indigo.interpolatedWith (cyan, (t - 0.5f) * 2.0f);
    }

    inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity = 1.0f)
    {
        for (auto [w, a] : { std::pair { 12.0f, 0.05f }, { 7.0f, 0.1f }, { 3.5f, 0.2f } })
        {
            g.setColour (c.withMultipliedAlpha (a * intensity));
            g.strokePath (p, juce::PathStrokeType (width + w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.35f * intensity));
        g.strokePath (p, juce::PathStrokeType (std::max (0.6f, width * 0.35f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    inline void glowStrokeGradient (juce::Graphics& g, const juce::Path& p, const juce::ColourGradient& grad, float width)
    {
        for (auto [w, a] : { std::pair { 14.0f, 0.06f }, { 8.0f, 0.11f }, { 4.0f, 0.22f } })
        {
            auto gg = grad;
            gg.multiplyOpacity (a);
            g.setGradientFill (gg);
            g.strokePath (p, juce::PathStrokeType (width + w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setGradientFill (grad);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.strokePath (p, juce::PathStrokeType (std::max (0.6f, width * 0.35f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Neon tube lettering: coloured halo + hot white core.
    inline void neonText (juce::Graphics& g, const juce::String& t, const juce::Font& f, juce::Rectangle<float> r,
                          juce::Colour c, juce::Justification j = juce::Justification::centredLeft, float intensity = 1.0f)
    {
        g.setFont (f);
        for (auto [rad, a] : { std::pair { 4.0f, 0.05f }, { 2.5f, 0.08f }, { 1.2f, 0.14f } })
            for (int k = 0; k < 8; ++k)
            {
                const float ang = juce::MathConstants<float>::twoPi * (float) k / 8.0f;
                g.setColour (c.withMultipliedAlpha (a * intensity));
                g.drawText (t, r.translated (rad * std::cos (ang), rad * std::sin (ang)), j, false);
            }
        g.setColour (c.interpolatedWith (juce::Colours::white, 0.55f));
        g.drawText (t, r, j, false);
    }

    // ---- Hardware --------------------------------------------------------------------------
    inline void drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle = 0.6f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (c.translated (0, 1)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a80a3), c.x - r, c.y - r, juce::Colour (0xff2a2338), c.x + r, c.y + r, false));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        for (float a : { angle, angle + juce::MathConstants<float>::halfPi })
            g.drawLine (c.x - r * 0.65f * std::cos (a), c.y - r * 0.65f * std::sin (a), c.x + r * 0.65f * std::cos (a), c.y + r * 0.65f * std::sin (a), 1.4f);
    }

    inline void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on)
    {
        if (on)
        {
            juce::ColourGradient halo (col.withAlpha (0.45f), c.x, c.y, col.withAlpha (0.0f), c.x + r * 4.0f, c.y, true);
            g.setGradientFill (halo);
            g.fillEllipse (juce::Rectangle<float> (r * 8.0f, r * 8.0f).withCentre (c));
        }
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (c));
        g.setGradientFill (juce::ColourGradient (on ? col.interpolatedWith (juce::Colours::white, 0.5f) : col.withAlpha (0.25f).withMultipliedBrightness (0.4f),
                                                 c.x - r * 0.4f, c.y - r * 0.4f, on ? col : col.withAlpha (0.15f).withMultipliedBrightness (0.3f), c.x + r, c.y + r, true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    }

    // Engraved silk-screen label on the faceplate.
    inline void engraved (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h = 8.0f,
                          juce::Justification j = juce::Justification::centred, juce::Colour c = engrave)
    {
        g.setFont (label (h));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawText (t, r.translated (0, 1.0f), j, false);
        g.setColour (c);
        g.drawText (t, r, j, false);
    }

    // Recessed CRT/LCD screen: bezel, dark glass, inner shadow, reflection.
    inline void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.0f, bool lcd = false)
    {
        // bezel
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050309), r.getX(), r.getY(), metalHi.withAlpha (0.9f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r.expanded (5.0f), radius + 5.0f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (r.expanded (1.5f), radius + 1.5f);
        // glass
        const auto tint = lcd ? juce::Colour (0xff0a0818) : glass;
        juce::ColourGradient glassGrad (tint.brighter (0.25f), r.getCentreX(), r.getCentreY(), tint, r.getX(), r.getY(), true);
        g.setGradientFill (glassGrad);
        g.fillRoundedRectangle (r, radius);
    }

    // Applied on top of screen content: scanlines, vignette, glass reflection.
    inline void drawScreenGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.0f, float scan = 1.0f)
    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);
        if (scan > 0.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f * scan));
            for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        }
        juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                       juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(), true);
        g.setGradientFill (vignette);
        g.fillRect (r);
        juce::Path refl;
        refl.startNewSubPath (r.getX(), r.getY());
        refl.lineTo (r.getX() + r.getWidth() * 0.55f, r.getY());
        refl.lineTo (r.getX() + r.getWidth() * 0.35f, r.getBottom());
        refl.lineTo (r.getX(), r.getBottom());
        refl.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), r.getX(), r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.4f, r.getBottom(), false));
        g.fillPath (refl);
        g.restoreState();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    // Synthwave perspective grid + optional sun.
    inline void drawSynthGrid (juce::Graphics& g, juce::Rectangle<float> r, float horizonY, juce::Colour c, float alpha = 1.0f)
    {
        const float cx = r.getCentreX();
        g.setColour (c.withAlpha (0.35f * alpha));
        g.drawHorizontalLine ((int) horizonY, r.getX(), r.getRight());
        for (int i = 1; i < 12; ++i)
        {
            const float t = std::pow ((float) i / 12.0f, 2.0f);
            const float y = horizonY + (r.getBottom() - horizonY) * t;
            g.setColour (c.withAlpha ((0.08f + 0.3f * t) * alpha));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        }
        for (int i = -14; i <= 14; ++i)
        {
            const float xb = cx + (float) i * r.getWidth() / 9.0f;
            g.setColour (c.withAlpha (0.22f * alpha));
            g.drawLine (cx + (float) i * 18.0f, horizonY, xb, r.getBottom(), 1.0f);
        }
    }

    inline void drawSun (juce::Graphics& g, juce::Point<float> c, float r, float alpha = 1.0f)
    {
        juce::ColourGradient halo (violet.withAlpha (0.25f * alpha), c.x, c.y, violet.withAlpha (0.0f), c.x + r * 2.2f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (r * 4.4f, r * 4.4f).withCentre (c));
        juce::Path disc;
        disc.addEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        // classic sliced sun: gaps widen towards the bottom
        juce::Path cut;
        for (int i = 0; i < 6; ++i)
        {
            const float y = c.y + r * (0.05f + 0.16f * (float) i);
            cut.addRectangle (c.x - r, y, r * 2.0f, 1.5f + (float) i * 1.3f);
        }
        g.saveState();
        g.reduceClipRegion (disc);
        g.setGradientFill (juce::ColourGradient (gold.withAlpha (alpha), c.x, c.y - r, violet.withAlpha (alpha), c.x, c.y + r, false));
        g.fillRect (disc.getBounds());
        g.setColour (glass);
        g.fillPath (cut);
        g.restoreState();
    }

    // 7-segment display. Characters: 0-9, '-', ' '.
    inline void drawSevenSeg (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, juce::Colour on)
    {
        static const int masks[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
        const int n = s.length();
        if (n == 0) return;
        const float cw = r.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const auto ch = s[i];
            int mask = 0;
            if (ch >= '0' && ch <= '9') mask = masks[ch - '0'];
            else if (ch == '-') mask = 0x40;
            auto cell = juce::Rectangle<float> (r.getX() + cw * (float) i, r.getY(), cw, r.getHeight()).reduced (cw * 0.14f, 0);
            const float t = cell.getWidth() * 0.17f;
            const float x0 = cell.getX(), x1 = cell.getRight(), y0 = cell.getY(), ym = cell.getCentreY(), y1 = cell.getBottom();
            const float sk = cell.getWidth() * 0.12f;   // italic slant
            auto seg = [&] (float ax, float ay, float bx, float by, bool horizontal)
            {
                juce::Path p;
                const float sa = sk * (1.0f - (ay - y0) / (y1 - y0)), sb = sk * (1.0f - (by - y0) / (y1 - y0));
                ax += sa; bx += sb;
                if (horizontal)
                {
                    p.startNewSubPath (ax + t * 0.5f, ay);
                    p.lineTo (ax + t, ay - t * 0.5f); p.lineTo (bx - t, by - t * 0.5f); p.lineTo (bx - t * 0.5f, by);
                    p.lineTo (bx - t, by + t * 0.5f); p.lineTo (ax + t, ay + t * 0.5f);
                }
                else
                {
                    p.startNewSubPath (ax, ay + t * 0.5f);
                    p.lineTo (ax + t * 0.5f, ay + t); p.lineTo (bx + t * 0.5f, by - t); p.lineTo (bx, by - t * 0.5f);
                    p.lineTo (bx - t * 0.5f, by - t); p.lineTo (ax - t * 0.5f, ay + t);
                }
                p.closeSubPath();
                return p;
            };
            const juce::Path segs[7] = {
                seg (x0, y0 + t * 0.5f, x1, y0 + t * 0.5f, true),        // a
                seg (x1 - t * 0.5f, y0, x1 - t * 0.5f, ym, false),       // b
                seg (x1 - t * 0.5f, ym, x1 - t * 0.5f, y1, false),       // c
                seg (x0, y1 - t * 0.5f, x1, y1 - t * 0.5f, true),        // d
                seg (x0 + t * 0.5f, ym, x0 + t * 0.5f, y1, false),       // e
                seg (x0 + t * 0.5f, y0, x0 + t * 0.5f, ym, false),       // f
                seg (x0, ym, x1, ym, true) };                            // g
            for (int k = 0; k < 7; ++k)
            {
                const bool lit = (mask >> k) & 1;
                if (lit)
                {
                    g.setColour (on.withAlpha (0.25f));
                    g.strokePath (segs[k], juce::PathStrokeType (4.0f));
                    g.setColour (on.interpolatedWith (juce::Colours::white, 0.25f));
                }
                else g.setColour (on.withAlpha (0.07f));
                g.fillPath (segs[k]);
            }
        }
    }

    // Brushed, anodised faceplate texture (rendered once and cached by the editor).
    inline juce::Image makeFaceplate (int w, int h, float scale)
    {
        juce::Image img (juce::Image::ARGB, (int) (w * scale), (int) (h * scale), true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff221832), 0, 0, juce::Colour (0xff0e0918), 0, (float) h, false));
        g.fillRect (r);
        juce::Random rng (77);
        for (int i = 0; i < h * 3; ++i)
        {
            const float y = rng.nextFloat() * (float) h;
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + 0.02f * rng.nextFloat()));
            g.fillRect (0.0f, y, (float) w, 0.6f);
        }
        return img;
    }

    // ---- Compatibility helpers --------------------------------------------------------------
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
    {
        drawScreen (g, r, radius, true);
    }

    inline void drawTitle (juce::Graphics& g, const juce::String& t, float x, float y, juce::Colour c = mist)
    {
        g.setColour (c);
        g.setFont (label (9.0f));
        g.drawText (t, juce::Rectangle<float> (x, y, 400.0f, 14.0f), juce::Justification::centredLeft);
    }

    inline juce::Path crescent (juce::Point<float> c, float r, float bite = 0.62f)
    {
        const float d = r * bite;
        const float h = std::sqrt (std::max (0.0f, r * r - d * d * 0.25f));
        auto angle = [] (float x, float y) { return std::atan2 (x, -y); };
        juce::Path p;
        const float t1 = angle (d * 0.5f, -h), t2 = angle (d * 0.5f, h);
        p.startNewSubPath (c.x + d * 0.5f, c.y - h);
        p.addCentredArc (c.x, c.y, r, r, 0.0f, t1, t2 - juce::MathConstants<float>::twoPi, false);
        const float p2 = angle (-d * 0.5f, h), p1 = angle (-d * 0.5f, -h);
        p.addCentredArc (c.x + d, c.y, r, r, 0.0f, p2 + juce::MathConstants<float>::twoPi, p1 + juce::MathConstants<float>::twoPi, false);
        p.closeSubPath();
        return p;
    }

    inline void drawMoonMark (juce::Graphics& g, juce::Point<float> c, float r, float glow = 1.0f)
    {
        const float halo = r * 3.2f;
        juce::ColourGradient rg (cyan.withAlpha (0.18f * juce::jmin (1.5f, glow)), c.x, c.y, cyan.withAlpha (0.0f), c.x + halo, c.y, true);
        g.setGradientFill (rg);
        g.fillEllipse (juce::Rectangle<float> (halo * 2.0f, halo * 2.0f).withCentre (c));
        auto p = crescent (c, r);
        g.setGradientFill (juce::ColourGradient (moon, c.x - r, c.y - r, cyan, c.x + r, c.y + r, false));
        g.fillPath (p);
    }

    struct Star { float x, y, size, alpha; };
    inline std::vector<Star> makeStars (int count, juce::uint32 seed)
    {
        juce::Random rng ((juce::int64) seed);
        std::vector<Star> s;
        for (int i = 0; i < count; ++i)
        {
            const float b = rng.nextFloat();
            s.push_back ({ rng.nextFloat(), rng.nextFloat(), b > 0.94f ? 1.6f : (b > 0.7f ? 1.1f : 0.7f), 0.15f + 0.6f * rng.nextFloat() * rng.nextFloat() });
        }
        return s;
    }

    inline void drawStars (juce::Graphics& g, const std::vector<Star>& stars, juce::Rectangle<float> area, float alpha = 1.0f)
    {
        for (auto& s : stars)
        {
            const float x = area.getX() + s.x * area.getWidth(), y = area.getY() + s.y * area.getHeight();
            g.setColour (moon.withAlpha (s.alpha * alpha));
            g.fillEllipse (x - s.size * 0.5f, y - s.size * 0.5f, s.size, s.size);
        }
    }

    inline juce::String mmss (double s)
    {
        const int t = (int) std::max (0.0, s);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }

    inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
}
