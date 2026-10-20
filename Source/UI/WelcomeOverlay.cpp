#include "Brand.h"
#include "WelcomeOverlay.h"

using namespace Theme;

namespace
{
    juce::File flagFile()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        d = d.getChildFile ("Application Support");
       #endif
        return d.getChildFile ("Whale Audio").getChildFile ("WHALE").getChildFile ("welcome-seen");
    }
}

WelcomeOverlay::WelcomeOverlay() { setWantsKeyboardFocus (true); }

bool WelcomeOverlay::wasSeen() { return flagFile().existsAsFile(); }
void WelcomeOverlay::markSeen()
{
    auto f = flagFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText ("1");
}

juce::Rectangle<float> WelcomeOverlay::buttonBounds() const
{
    return { (float) getWidth() * 0.5f - 90.0f, 362.0f, 180.0f, 44.0f };
}

void WelcomeOverlay::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    // dim everything except the AUTO button
    juce::Path dimPath;
    dimPath.addRectangle (0, 0, w, h);
    dimPath.addEllipse (autoSpot.reduced (4.0f));
    dimPath.setUsingNonZeroWinding (false);
    g.setColour (juce::Colour (0xe0040209));
    g.fillPath (dimPath);
    {
        juce::Path ring; ring.addEllipse (autoSpot.reduced (6.0f));
        neon (g, ring, green, 2.5f, 1.0f);
    }

    g.setColour (text);
    g.setFont (bold (40.0f, -0.01f));
    g.drawText (fr ("Welcome to WHALE"), juce::Rectangle<float> (0, 66, w, 48), juce::Justification::centred);
    g.setColour (juce::Colour (0xffb2a6d6));
    g.setFont (body (17.0f));
    g.drawText (fr ("Your first master in three steps"), juce::Rectangle<float> (0, 114, w, 26), juce::Justification::centred);

    struct Step { const char* n; const char* a; const char* b; };
    const Step steps[3] = { { "1", "Put WHALE last", "on your master bus" }, { "2", "Play the loudest part", "the drop or the chorus" }, { "3", "Click AUTO", "it listens for about 30 s" } };
    const float cw = 280.0f, gap = 30.0f, x0 = (w - (3.0f * cw + 2.0f * gap)) * 0.5f, y = 168.0f, ch = 130.0f;
    for (int i = 0; i < 3; ++i)
    {
        const juce::Rectangle<float> r (x0 + (float) i * (cw + gap), y, cw, ch);
        const bool last = i == 2;
        g.setColour (juce::Colour (0xf5120c26));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (last ? green : violet);
        g.drawRoundedRectangle (r, 14.0f, last ? 2.0f : 1.2f);
        g.fillEllipse (r.getX() + 20, r.getY() + 20, 38, 38);
        g.setColour (bg);
        g.setFont (bold (20.0f));
        g.drawText (steps[i].n, juce::Rectangle<float> (r.getX() + 20, r.getY() + 20, 38, 38), juce::Justification::centred);
        g.setColour (text);
        g.setFont (bold (20.0f));
        g.drawText (fr (steps[i].a), juce::Rectangle<float> (r.getX() + 20, r.getY() + 72, cw - 30, 24), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffb2a6d6));
        g.setFont (body (15.0f));
        g.drawText (fr (steps[i].b), juce::Rectangle<float> (r.getX() + 20, r.getY() + 98, cw - 30, 20), juce::Justification::centredLeft);
    }
    g.setColour (juce::Colour (0xffb2a6d6));
    g.setFont (body (14.0f));
    g.drawText (fr ("Then compare BEFORE / AFTER at the same loudness, and nudge the knobs: the white diamond on each knob marks what AUTO chose."),
                juce::Rectangle<float> (0, 324, w, 20), juce::Justification::centred);

    const auto b = buttonBounds();
    g.setColour (green);
    g.fillRoundedRectangle (b, 22.0f);
    g.setColour (bg);
    g.setFont (bold (18.0f));
    g.drawText (fr ("Got it"), b, juce::Justification::centred);
    g.setColour (dim);
    g.setFont (body (12.5f));
    g.drawText (fr ("Shown on first launch only \xc2\xb7 menu \xe2\x89\xa1 > Quick tour to see it again"), juce::Rectangle<float> (0, 416, w, 18), juce::Justification::centred);
    g.setColour (faint);
    g.setFont (mono (11.0f, 0.12f));
    {
        const juce::String by = fr ("WHALE MASTERING  \xc2\xb7  BY");
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), by), lh = 18.0f, lw = Brand::lockupWidth (lh);
        const float x0 = w * 0.5f - (tw + 10.0f + lw) * 0.5f;
        g.drawText (by, juce::Rectangle<float> (x0, 446, tw + 4, 16), juce::Justification::centredLeft);
        Brand::drawLockup (g, x0 + tw + 10.0f, 454.0f, lh, 0.95f);
    }
}

void WelcomeOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (buttonBounds().expanded (6.0f).contains (e.position) || autoSpot.contains (e.position))
    {
        markSeen();
        if (onClose) onClose();
    }
}

bool WelcomeOverlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::returnKey)
    {
        markSeen();
        if (onClose) onClose();
        return true;
    }
    return false;
}
