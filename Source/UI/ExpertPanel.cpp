#include "ExpertPanel.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

using namespace Theme;

namespace
{
    juce::StringArray tabNames()
    {
        auto n = ChainStrip::names();
        n.add ("RAPPORT AUTO");
        return n;
    }
    const char* moduleIntro (int m)
    {
        switch (m)
        {
            case 0: return "EQ dynamique : chaque bande a un réglage fixe et peut baisser seule quand sa zone déborde (pics de sub, bas-médium boueux, aigus agressifs).";
            case 1: return "Compression 3 bandes (glue) + punch des transitoires. Détail = attaque plus lente : les attaques passent, le corps est tenu.";
            case 2: return "Saturation douce suréchantillonnée x4 : densité et chaleur (harmoniques paires), sans repli d'aliasing.";
            case 3: return "Largeur des côtés au-dessus des graves, et graves remis au centre sous la fréquence choisie (club, vinyle, mono).";
            case 4: return "Étagères larges : basses à 110 Hz, air à 10 kHz.";
            case 5: return "Clipper doux suréchantillonné x4 devant le limiteur : il rase les crêtes les plus fines pour gagner du volume sans pomper.";
            case 6: return "Limiteur true-peak avec anticipation 1,5 ms. Cible de volume : style (références), streaming, club, ou à la main.";
            default: return "Ce qu'AUTO a mesuré dans ton morceau, ce qu'il a réglé, et pourquoi.";
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
                addRow (PID::specGain[b], juce::String (specBands[b].name) + " " + (specBands[b].hz >= 1000 ? juce::String (specBands[b].hz / 1000.0f, 1) + " kHz" : juce::String ((int) specBands[b].hz) + " Hz"),
                        fr ("réglage fixe"), b < 2 ? magenta : cyan, true);
                addRow (PID::specDyn[b], fr ("   dynamique"), fr ("baisse auto sur les pics"), violet);
            }
            break;
        case 1:
            addRow (PID::compGlue, "GLUE", fr ("tient les pics de chaque bande"), magenta);
            addRow (PID::compPunch, "PUNCH", fr ("renforce les attaques"), magenta);
            addRow (PID::compDetail, fr ("DÉTAIL"), fr ("attaque plus lente = plus de transitoires"), cyan);
            break;
        case 2:
            addRow (PID::satDrive, "DRIVE", fr ("densité + chaleur"), violet);
            break;
        case 3:
            addRow (PID::widthSide, fr ("CÔTÉS"), fr ("largeur au-dessus des graves"), cyan, true);
            addRow (PID::widthMono, "MONO SOUS", fr ("graves au centre (0 = off)"), violet);
            break;
        case 4:
            addRow (PID::toneLow, "BASSES 110 Hz", fr ("étagère grave"), magenta, true);
            addRow (PID::toneAir, "AIR 10 kHz", fr ("étagère aiguë"), cyan, true);
            break;
        case 5:
            addRow (PID::clipAmt, "CLIPPER", fr ("part du travail avant le limiteur"), magenta);
            break;
        case 6:
            addRow (PID::ceiling, "PLAFOND", fr ("true peak maximum"), cyan);
            addRow (PID::manualDrive, fr ("GAIN MANUEL"), fr ("utilisé en cible « Manual »"), violet, true);
            addRow (PID::dosage, "DOSAGE", fr ("intensité des corrections"), magenta);
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
    float y = 96.0f;
    const float rowH = rows.size() > 6 ? 34.0f : 48.0f;
    for (auto& r : rows)
    {
        r->slider->setBounds (juce::Rectangle<float> (x0 + 190, y, w - 190, 26).toNearestInt());
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
            const float x = std::log2 (hz / specBands[b].hz);
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
        const float x = fx (specBands[b].hz), y = fy (dos * proc.state.getRawParameterValue (PID::specGain[b])->load() - meterSpec[b]);
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
    g.drawText (fr ("COURBE APPLIQUÉE (dosage inclus)  ·  trait cyan = baisse dynamique en direct"), r.reduced (10).removeFromTop (16), juce::Justification::centredLeft);
}

void ExpertPanel::drawReport (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& res = proc.getAutoResult();
    g.setFont (body (14.0f));
    float y = r.getY();
    if (! res.valid || res.report.isEmpty())
    {
        g.setColour (dim);
        g.drawText (fr ("AUTO n'a pas encore tourné : lance la lecture sur la partie la plus forte du morceau et clique sur AUTO."), r.withHeight (24), juce::Justification::centredLeft);
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
        g.drawText (fr ("À RÉGLER DANS LE MIX (le master ne peut pas tout réparer)"), juce::Rectangle<float> (r.getX(), y, r.getWidth(), 18), juce::Justification::centredLeft);
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
        const char* names[3] = { "BAS < 120 Hz", "MILIEU", "HAUT > 4 kHz" };
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
        g.drawText ("CIBLE", juce::Rectangle<float> (x0, tb.getY() + 6, 185, 18), juce::Justification::centredLeft);
    }
}

void ExpertPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto tabs = tabNames();
    for (int i = 0; i < tabs.size(); ++i)
        if (tabBounds (i).contains (e.position)) { showModule (i); return; }
    if (e.position.x > (float) getWidth() - 40 && e.position.y > 48 && e.position.y < 74 && onClose) onClose();
}
