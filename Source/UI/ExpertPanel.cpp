#include "ExpertPanel.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

using namespace Theme;

namespace
{
    juce::StringArray tabNames()
    {
        auto n = ChainStrip::names();
        n.add ("AUTO REPORT");
        return n;
    }
    const char* moduleIntro (int m)
    {
        switch (m)
        {
            case 0: return "Dynamic EQ: each band has a fixed setting and dips on its own when its range spills over (sub peaks, muddy low-mids, harsh highs).";
            case 1: return "3-band compression (glue) + transient punch. Detail = slower attack: the hits come through, the body is held.";
            case 2: return "Soft saturation, 4x oversampled: density and warmth (even harmonics), no aliasing.";
            case 3: return "Side width above the low end, and low end folded to the centre below the chosen frequency (club, vinyl, mono).";
            case 4: return "Broad shelves: low end at 110 Hz, air at 10 kHz.";
            case 5: return "Soft clipper, 4x oversampled, before the limiter: it shaves the thinnest peaks to gain loudness without pumping.";
            case 6: return "True-peak limiter with 1.5 ms look-ahead. Loudness target: genre (references), streaming, club, or manual.";
            default: return "What AUTO measured in your track, what it changed, and why.";
        }
    }
}

ExpertPanel::ExpertPanel (WhaleAudioProcessor& p) : proc (p)
{
    for (auto& t : loudnessTargets) targetBox.addItem (t.name, targetBox.getNumItems() + 1);
    addChildComponent (targetBox);
    targetAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, PID::target, targetBox);
    build();
}

void ExpertPanel::addRow (const char* id, const juce::String& label, const juce::String& hint, juce::Colour c, bool bipolar)
{
    auto r = std::make_unique<Row>();
    r->label = label; r->hint = hint;
    r->slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    r->slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 22);
    r->slider->setColour (juce::Slider::trackColourId, c);
    r->slider->getProperties().set ("bipolar", bipolar);
    r->slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    r->slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    r->slider->setColour (juce::Slider::textBoxTextColourId, text);
    addAndMakeVisible (*r->slider);
    r->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.state, id, *r->slider);
    rows.push_back (std::move (r));
}

void ExpertPanel::build()
{
    rows.clear();
    targetBox.setVisible (module == 6);
    switch (module)
    {
        case 0:
            for (int b = 0; b < 4; ++b)
            {
                addRow (PID::specGain[b], juce::String (specBands[b].name), fr ("fixed setting"), b < 2 ? magenta : cyan, true);
                addRow (PID::specFreq[b], fr ("   frequency"), {}, cyan);
                addRow (PID::specDyn[b], fr ("   dynamic"), fr ("automatic dip on peaks"), violet);
            }
            break;
        case 1:
            addRow (PID::compGlue, "GLUE", fr ("holds the peaks of each band"), magenta);
            addRow (PID::compPunch, "PUNCH", fr ("kick forward, tighter low-end tail"), magenta);
            addRow (PID::compDetail, fr ("DETAIL"), fr ("slower attack = more transients"), cyan);
            break;
        case 2:
            addRow (PID::satDrive, "DRIVE", fr ("warmth: harmonics on lows and low-mids"), violet);
            break;
        case 3:
            addRow (PID::widthSide, fr ("SIDES"), fr ("width above the low end"), cyan, true);
            addRow (PID::widthMono, "MONO BELOW", fr ("low end in the centre (0 = off)"), violet);
            break;
        case 4:
            addRow (PID::toneLow, "LOW 110 Hz", fr ("low shelf"), magenta, true);
            addRow (PID::toneAir, "AIR 10 kHz", fr ("high shelf"), cyan, true);
            addRow (PID::lowCut, "LOW CUT", fr ("removes rumble below the kick (0 = off)"), violet);
            break;
        case 5:
            addRow (PID::clipAmt, "CLIPPER", fr ("share of the work before the limiter"), magenta);
            break;
        case 6:
            addRow (PID::ceiling, "CEILING", fr ("maximum true peak"), cyan);
            addRow (PID::manualDrive, fr ("MANUAL GAIN"), fr ("used with the “Manual” target"), violet, true);
            addRow (PID::dosage, "AMOUNT", fr ("strength of the corrections"), magenta);
            break;
        default: break;
    }
    resized();
    repaint();
}

void ExpertPanel::showModule (int m)
{
    module = juce::jlimit (0, 7, m);
    build();
}

juce::Rectangle<float> ExpertPanel::tabBounds (int i) const
{
    const float w = ((float) getWidth() - 40.0f) / 8.0f;
    return { 20.0f + (float) i * w, 14.0f, w - 6.0f, 30.0f };
}

juce::Rectangle<float> ExpertPanel::graphBounds() const
{
    return { 24.0f, 92.0f, (float) getWidth() * 0.5f - 40.0f, (float) getHeight() - 112.0f };
}

void ExpertPanel::resized()
{
    const bool graph = module == 0 || module == 1;
    const float x0 = graph ? (float) getWidth() * 0.5f : 24.0f;
    const float w = (float) getWidth() - x0 - 24.0f;
    const bool dense = rows.size() > 9;
    float y = dense ? 84.0f : 96.0f;
    const float rowH = dense ? 29.0f : rows.size() > 6 ? 34.0f : 48.0f;
    for (auto& r : rows)
    {
        r->slider->setBounds (juce::Rectangle<float> (x0 + 190, y, w - 190, dense ? 22.0f : 26.0f).toNearestInt());
        y += rowH;
    }
    if (module == 6) targetBox.setBounds (juce::Rectangle<float> (x0 + 190, y + 6, 240, 30).toNearestInt());
}

void ExpertPanel::tick()
{
    auto& d = proc.getDSP();
    for (int k = 0; k < 3; ++k) meterGr[k] = 0.7f * meterGr[k] + 0.3f * d.getBandGrDb (k);
    for (int k = 0; k < 4; ++k) meterSpec[k] = 0.7f * meterSpec[k] + 0.3f * d.getSpecDynDb (k);
    repaint();
}

void ExpertPanel::drawSpectral (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff0b0719));
    g.fillRoundedRectangle (r, 10.0f);
    auto fx = [&] (float hz) { return r.getX() + r.getWidth() * std::log10 (hz / 20.0f) / 3.0f; };
    auto fy = [&] (float db) { return r.getCentreY() - db * r.getHeight() / 26.0f; };
    g.setFont (mono (10.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (line);
        g.drawVerticalLine ((int) fx (hz), r.getY(), r.getBottom());
        g.setColour (faint);
        g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz), (int) fx (hz) - 20, (int) r.getBottom() - 16, 40, 14, juce::Justification::centred);
    }
    for (float db : { -6.0f, 0.0f, 6.0f })
    {
        g.setColour (db == 0 ? lineHi : line);
        g.drawHorizontalLine ((int) fy (db), r.getX(), r.getRight());
    }
    // current effective curve (base x dosage, minus the live dynamic cut)
    const float dos = proc.state.getRawParameterValue (PID::dosage)->load();
    juce::Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
        float gdb = 0;
        for (int b = 0; b < 4; ++b)
        {
            const float gain = dos * proc.state.getRawParameterValue (PID::specGain[b])->load() - meterSpec[b];
            const float x = std::log2 (hz / proc.state.getRawParameterValue (PID::specFreq[b])->load());
            gdb += gain * std::exp (-x * x * 1.6f * specBands[b].q);
        }
        const float lo = dos * proc.state.getRawParameterValue (PID::toneLow)->load(), air = dos * proc.state.getRawParameterValue (PID::toneAir)->load();
        gdb += lo / (1.0f + std::pow (hz / 110.0f, 2.0f)) + air / (1.0f + std::pow (10000.0f / hz, 2.0f));
        const juce::Point<float> q (fx (hz), fy (gdb));
        if (i == 0) curve.startNewSubPath (q); else curve.lineTo (q);
    }
    neon (g, curve, magenta, 2.2f, 0.8f);
    for (int b = 0; b < 4; ++b)
    {
        const float x = fx (proc.state.getRawParameterValue (PID::specFreq[b])->load()), y = fy (dos * proc.state.getRawParameterValue (PID::specGain[b])->load() - meterSpec[b]);
        g.setColour (juce::Colour (0xff1e0e3c));
        g.fillEllipse (x - 7, y - 7, 14, 14);
        g.setColour (magenta);
        g.drawEllipse (x - 7, y - 7, 14, 14, 1.8f);
        if (meterSpec[b] > 0.05f)
        {
            g.setColour (cyan);
            g.drawLine (x, fy (dos * proc.state.getRawParameterValue (PID::specGain[b])->load()), x, y, 2.0f);
        }
    }
    g.setColour (dim);
    g.setFont (mono (10.5f));
    g.drawText (fr ("APPLIED CURVE (amount included)  ·  cyan line = live dynamic dip"), r.reduced (10).removeFromTop (16), juce::Justification::centredLeft);
}

void ExpertPanel::drawReport (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& res = proc.getAutoResult();
    g.setFont (body (14.0f));
    float y = r.getY();
    if (! res.valid || res.report.isEmpty())
    {
        g.setColour (dim);
        g.drawText (fr ("AUTO has not run yet: play the loudest part of the track and click AUTO."), r.withHeight (24), juce::Justification::centredLeft);
        return;
    }
    for (auto& l : res.report)
    {
        g.setColour (cyan);
        g.fillEllipse (r.getX(), y + 8, 6, 6);
        g.setColour (text);
        juce::AttributedString as;
        as.append (l, body (14.0f), text);
        juce::TextLayout tl; tl.createLayout (as, r.getWidth() - 20);
        tl.draw (g, juce::Rectangle<float> (r.getX() + 16, y, r.getWidth() - 20, tl.getHeight()));
        y += tl.getHeight() + 8;
    }
    if (! res.mixNotes.isEmpty())
    {
        y += 8;
        g.setColour (orange);
        g.setFont (bold (13.0f, 0.06f));
        g.drawText (fr ("TO FIX IN THE MIX (mastering cannot repair everything)"), juce::Rectangle<float> (r.getX(), y, r.getWidth(), 18), juce::Justification::centredLeft);
        y += 24;
        for (auto& l : res.mixNotes)
        {
            juce::AttributedString as;
            as.append (l, body (14.0f), juce::Colour (0xffffc8a0));
            juce::TextLayout tl; tl.createLayout (as, r.getWidth() - 20);
            tl.draw (g, juce::Rectangle<float> (r.getX() + 16, y, r.getWidth() - 20, tl.getHeight()));
            y += tl.getHeight() + 8;
        }
    }
}

void ExpertPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0a0618));
    g.fillRoundedRectangle (b, 14.0f);
    g.setColour (juce::Colour (0xff4f3c8c));
    g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.2f);

    const auto tabs = tabNames();
    for (int i = 0; i < tabs.size(); ++i)
    {
        const auto r = tabBounds (i);
        if (i == module) { g.setColour (juce::Colour (0xff2e1660)); g.fillRoundedRectangle (r, 7.0f); g.setColour (magenta); g.drawRoundedRectangle (r, 7.0f, 1.2f); }
        g.setColour (i == module ? juce::Colours::white : dim);
        g.setFont (bold (12.0f, 0.04f));
        g.drawText (tabs[i], r, juce::Justification::centred);
    }
    // close cross
    g.setColour (dim);
    g.drawLine (b.getRight() - 30, 56, b.getRight() - 20, 66, 1.6f);
    g.drawLine (b.getRight() - 30, 66, b.getRight() - 20, 56, 1.6f);
    g.setColour (juce::Colour (0xffb2a6d6));
    g.setFont (body (13.0f));
    g.drawText (fr (moduleIntro (module)), juce::Rectangle<float> (24, 54, b.getWidth() - 70, 24), juce::Justification::centredLeft);

    if (module == 7) { drawReport (g, { 24, 96, b.getWidth() - 48, b.getHeight() - 110 }); return; }
    if (module == 0) drawSpectral (g, graphBounds());
    if (module == 1)
    {
        const auto r = graphBounds();
        g.setColour (juce::Colour (0xff0b0719));
        g.fillRoundedRectangle (r, 10.0f);
        const char* names[3] = { "LOW < 120 Hz", "MID", "HIGH > 4 kHz" };
        for (int k = 0; k < 3; ++k)
        {
            const float x = r.getX() + 30 + (float) k * (r.getWidth() - 60) / 3.0f, w = (r.getWidth() - 60) / 3.0f - 30;
            const juce::Rectangle<float> bar (x, r.getY() + 30, w, r.getHeight() - 70);
            g.setColour (line);
            g.fillRoundedRectangle (bar, 4.0f);
            g.setColour (magenta);
            g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * juce::jlimit (0.0f, 1.0f, meterGr[k] / 6.0f)), 4.0f);
            g.setColour (dim);
            g.setFont (mono (10.5f));
            g.drawText (names[k], (int) x - 10, (int) bar.getBottom() + 6, (int) w + 20, 14, juce::Justification::centred);
            g.setColour (text);
            g.drawText ("-" + juce::String (meterGr[k], 1) + " dB", (int) x - 10, (int) r.getY() + 10, (int) w + 20, 14, juce::Justification::centred);
        }
    }
    // row labels
    const bool graph = module == 0 || module == 1;
    const float x0 = graph ? b.getWidth() * 0.5f : 24.0f;
    for (auto& r : rows)
    {
        const auto sb = r->slider->getBounds().toFloat();
        g.setColour (text);
        g.setFont (bold (13.0f, 0.03f));
        g.drawText (r->label, juce::Rectangle<float> (x0, sb.getY() - 4, 185, 18), juce::Justification::centredLeft);
        g.setColour (faint);
        g.setFont (body (11.0f));
        g.drawText (r->hint, juce::Rectangle<float> (x0, sb.getY() + 13, 185, 14), juce::Justification::centredLeft);
    }
    if (module == 6)
    {
        const auto tb = targetBox.getBounds().toFloat();
        g.setColour (text);
        g.setFont (bold (13.0f, 0.03f));
        g.drawText ("TARGET", juce::Rectangle<float> (x0, tb.getY() + 6, 185, 18), juce::Justification::centredLeft);
    }
}

void ExpertPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto tabs = tabNames();
    for (int i = 0; i < tabs.size(); ++i)
        if (tabBounds (i).contains (e.position)) { showModule (i); return; }
    if (e.position.x > (float) getWidth() - 40 && e.position.y > 48 && e.position.y < 74 && onClose) onClose();
}
