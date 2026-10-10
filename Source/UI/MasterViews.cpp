#include "MasterViews.h"

using namespace Night;

MasterView::MasterView (FINISHAudioProcessor& p) : proc (p)
{
    shortTerm.fill (-70.0f); momentary.fill (-70.0f); reduction.fill (0.0f);
    reportButton.setButtonText (utf8 ("ANALYSE"));
    reportButton.getProperties().set ("style", "chip");
    reportButton.setClickingTogglesState (true);
    reportButton.setTooltip (utf8 ("Ce que FINISH mesure dans ton mix, ce qu'il corrige et pourquoi, avant / après."));
    reportButton.onClick = [this] { showReport (reportButton.getToggleState()); };
    addAndMakeVisible (reportButton);
    reportText.setMultiLine (true, true);
    reportText.setReadOnly (true);
    reportText.setCaretVisible (false);
    reportText.setScrollbarsShown (true);
    reportText.setFont (Night::mono (10.0f));
    reportText.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    reportText.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    reportText.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    reportText.setColour (juce::TextEditor::textColourId, Night::mist);
    addChildComponent (reportText);
}

void MasterView::resized()
{
    reportButton.setBounds (getWidth() - 96, 10, 80, 20);
    reportText.setBounds (getLocalBounds().reduced (16, 12).withTrimmedTop (24));
}

void MasterView::showReport (bool on)
{
    reportMode = on;
    reportButton.setToggleState (on, juce::dontSendNotification);
    reportText.setVisible (on);
    reportTicks = 0;
    if (on) reportText.setText (proc.getMasterReport(), false);
    repaint();
}

void MasterView::tick()
{
    const auto& d = proc.getDSP();
    shortTerm[(size_t) writePos] = d.getOutputLoudness();
    momentary[(size_t) writePos] = d.getOutputMomentary();
    reduction[(size_t) writePos] = d.getGainReductionDb();
    writePos = (writePos + 1) % historySize;
    const float tp = d.getTruePeakDb();
    if (tp > tpHold || --tpHoldTicks <= 0) { tpHold = tp; tpHoldTicks = 60; }
    grHold = std::max (d.getGainReductionDb(), grHold * 0.9f);
    if (reportMode && ++reportTicks >= 60)
    {
        reportTicks = 0;
        const auto t = proc.getMasterReport();
        if (t != reportText.getText()) { const auto pos = reportText.getCaretPosition(); reportText.setText (t, false); reportText.setCaretPosition (pos); }
    }
    repaint();
}

void MasterView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r.reduced (2.0f), 14.0f);
    auto inner = r.reduced (18.0f, 14.0f);
    if (reportMode)
    {
        g.setFont (mono (9.5f));
        g.setColour (cyan.withAlpha (0.9f));
        g.drawText (utf8 ("MASTER  ·  ANALYSE ET COMPTE RENDU"), inner.withHeight (14.0f), juce::Justification::centredLeft);
        drawScreenGlass (g, r.reduced (2.0f), 14.0f, 0.2f);
        return;
    }

    const int t = proc.getTargetIndex();
    const float target = t > 0 ? proc.getTargetLufs() : -14.0f;

    // Header
    g.setFont (mono (9.5f));
    g.setColour (cyan.withAlpha (0.9f));
    g.drawText ("MASTER", inner.withHeight (14.0f), juce::Justification::centredLeft);
    g.setColour (dusk);
    g.drawText (t == sameLevelTarget ? utf8 ("MÊME NIVEAU ") + juce::String (target, 1) + " LUFS"
                : (t > 0 ? juce::String ("TARGET ") + loudnessTargets[t].name + " LUFS" : juce::String ("MANUAL (LOUDNESS KNOB)")),
                inner.withHeight (14.0f).withTrimmedLeft (70.0f), juce::Justification::centredLeft);
    g.drawText ("20 S", inner.withHeight (14.0f).withTrimmedRight (340.0f), juce::Justification::centredRight);

    // Graph
    auto graph = inner.withTrimmedTop (22.0f).withTrimmedRight (250.0f).withTrimmedBottom (4.0f);
    const float lo = -36.0f, hi = -3.0f;
    auto yOf = [&] (float lufs) { return graph.getBottom() - graph.getHeight() * (juce::jlimit (lo, hi, lufs) - lo) / (hi - lo); };
    g.setFont (mono (7.5f));
    for (float db = -33.0f; db <= -6.0f; db += 3.0f)
    {
        const float y = yOf (db);
        g.setColour (line.withAlpha (0.45f));
        g.drawHorizontalLine ((int) y, graph.getX() + 24.0f, graph.getRight());
        g.setColour (dusk);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (graph.getX(), y - 6.0f, 22.0f, 12.0f), juce::Justification::centredRight);
    }
    auto plot = graph.withTrimmedLeft (26.0f);
    // target band
    if (t > 0)
    {
        g.setColour (mint.withAlpha (0.08f));
        g.fillRect (plot.getX(), yOf (target + 1.0f), plot.getWidth(), yOf (target - 1.0f) - yOf (target + 1.0f));
        g.setColour (mint.withAlpha (0.7f));
        const float ty = yOf (target);
        for (float x = plot.getX(); x < plot.getRight(); x += 8.0f) g.drawLine (x, ty, x + 4.0f, ty, 1.0f);
    }
    auto path = [&] (const std::array<float, historySize>& data)
    {
        juce::Path p;
        for (int i = 0; i < historySize; ++i)
        {
            const float v = data[(size_t) ((writePos + i) % historySize)];
            const float x = plot.getX() + plot.getWidth() * (float) i / (historySize - 1);
            const float y = yOf (v);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (indigo.withAlpha (0.35f));
    g.strokePath (path (momentary), juce::PathStrokeType (1.0f));
    glowStrokeGradient (g, path (shortTerm), aurora (plot.getX(), 0, plot.getRight(), 0, 1.0f), 2.0f);
    // limiter activity as a curtain from the top
    {
        juce::Path gr;
        gr.startNewSubPath (plot.getX(), plot.getY());
        for (int i = 0; i < historySize; ++i)
        {
            const float v = reduction[(size_t) ((writePos + i) % historySize)];
            gr.lineTo (plot.getX() + plot.getWidth() * (float) i / (historySize - 1), plot.getY() + juce::jlimit (0.0f, 1.0f, v / 12.0f) * plot.getHeight() * 0.35f);
        }
        gr.lineTo (plot.getRight(), plot.getY());
        gr.closeSubPath();
        g.setColour (rose.withAlpha (0.18f));
        g.fillPath (gr);
    }

    // Readouts
    const auto& d = proc.getDSP();
    auto right = inner.withTrimmedTop (18.0f).withTrimmedLeft (inner.getWidth() - 236.0f);
    auto readout = [&] (juce::Rectangle<float> box, const juce::String& name, float value, juce::Colour c, const juce::String& unit)
    {
        drawScreen (g, box, 6.0f, true);
        g.setColour (dusk);
        g.setFont (mono (7.5f));
        g.drawText (name, box.reduced (8.0f, 4.0f).withHeight (10.0f), juce::Justification::centredLeft);
        const auto txt = value <= -60.0f ? juce::String ("--.-") : juce::String (value, 1);
        g.setColour (c);
        g.setFont (mono (24.0f));
        g.drawText (txt, box.reduced (10.0f, 2.0f).withTrimmedTop (10.0f), juce::Justification::centredLeft);
        g.setColour (dusk);
        g.setFont (mono (8.0f));
        g.drawText (unit, box.reduced (8.0f, 6.0f), juce::Justification::bottomRight);
    };
    const float st = d.getOutputLoudness();
    const auto stCol = t == 0 ? cyan : (std::abs (st - target) <= 1.0f ? mint : (st > target ? amber : cyan));
    readout (right.withHeight (66.0f), "SHORT-TERM", st, stCol, "LUFS");
    readout (right.withTrimmedTop (74.0f).withHeight (66.0f), "PEAK", tpHold, tpHold > proc.params.getRawParameterValue (ParamIDs::ceiling)->load() + 0.05f ? rose : moon, "dBFS");
    readout (right.withTrimmedTop (148.0f).withHeight (66.0f), "LIMITER", -grHold, grHold > 6.0f ? amber : moon, "dB");
    {
        auto drive = right.withTrimmedTop (222.0f).withHeight (36.0f);
        g.setColour (dusk);
        g.setFont (mono (8.0f));
        g.drawText ("DRIVE " + juce::String (proc.getAutoDriveDb() + d.getLoudnessCorrectionDb(), 1) + " dB   IN " + juce::String (d.getInputLoudness(), 1) + " LUFS",
                    drive, juce::Justification::centredLeft);
    }
    drawScreenGlass (g, r.reduced (2.0f), 14.0f, 0.35f);
}

// ======================================================================================
MasterColumn::MasterColumn (FINISHAudioProcessor& p) : proc (p)
{
    for (int t = 0; t < numLoudnessTargets; ++t)
    {
        auto& b = targets[(size_t) t];
        b.setButtonText (t == 0 ? juce::String ("MANUAL") : (t == sameLevelTarget ? utf8 ("MÊME NIV.") : juce::String (loudnessTargets[t].shortName) + " " + juce::String ((int) loudnessTargets[t].lufs)));
        b.onClick = [this, t]
        {
            if (auto* prm = proc.params.getParameter (ParamIDs::target))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) t));
                prm->endChangeGesture();
            }
            refresh();
        };
        addAndMakeVisible (b);
    }
    targets[1].setTooltip (utf8 ("Spotify, Apple Music, YouTube : -14 LUFS, le plus dynamique."));
    targets[2].setTooltip (utf8 ("Équilibré : -11 LUFS, fort mais encore vivant."));
    targets[3].setTooltip (utf8 ("Club : -9 LUFS, pour les DJ sets."));
    targets[4].setTooltip (utf8 ("Très fort : -7 LUFS, au prix de la dynamique."));
    targets[0].setTooltip (utf8 ("Manuel : corrections coupées, le bouton LOUDNESS règle le gain dans le limiteur."));
    targets[sameLevelTarget].setTooltip (utf8 ("Même niveau : FINISH corrige le son sans monter le volume (recommandé quand ton mix est déjà fort)."));
    for (int i = 0; i < numMasterProfiles; ++i)
    {
        auto& b = profiles[(size_t) i];
        b.setButtonText (MasterDoctor::profileLabel ((MasterDoctor::Profile) i));
        b.getProperties().set ("style", "chip");
        b.setTooltip (MasterDoctor::profileHelp ((MasterDoctor::Profile) i));
        b.onClick = [this, i]
        {
            if (auto* prm = proc.params.getParameter (ParamIDs::masterProfile))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
            proc.setMasterCharacter (i == (int) MasterDoctor::Profile::Warm ? proc.getKeepDark() : false, false);
            refresh();
        };
        addAndMakeVisible (b);
    }
    for (auto* k : { &ceiling, &intensity })
    {
        k->getProperties().set ("lunar", true);
        k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
        addAndMakeVisible (k);
    }
    ceiling.setTooltip (utf8 ("Plafond true-peak : -1 dBTP pour le streaming, -0.3 pour le club."));
    intensity.setTooltip (utf8 ("Intensité des corrections (EQ, largeur, leveller, clipper)."));
    ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, ParamIDs::ceiling, ceiling);
    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, ParamIDs::masterIntensity, intensity);
    ceiling.setNumDecimalPlacesToDisplay (1);
}

void MasterColumn::resized()
{
    const int w = getWidth();
    const int half = (w - 16) / 2;
    const int order[] = { 0, sameLevelTarget, 1, 2, 3, 4 };
    for (int k = 0; k < numLoudnessTargets; ++k)
        targets[(size_t) order[k]].setBounds (8 + (k % 2) * half, 140 + (k / 2) * 30, half - 4, 26);
    const int pw = (w - 16) / numMasterProfiles;
    for (int i = 0; i < numMasterProfiles; ++i) profiles[(size_t) i].setBounds (8 + i * pw, 246, pw - 4, 22);
    intensity.setBounds (w / 4 - 36, 280, 72, 64);
    ceiling.setBounds (3 * w / 4 - 36, 280, 72, 64);
}

void MasterColumn::refresh()
{
    const int t = proc.getTargetIndex();
    for (int k = 0; k < numLoudnessTargets; ++k) targets[(size_t) k].setToggleState (k == t, juce::dontSendNotification);
    const int pr = (int) proc.params.getRawParameterValue (ParamIDs::masterProfile)->load();
    for (int i = 0; i < numMasterProfiles; ++i) profiles[(size_t) i].setToggleState (i == pr, juce::dontSendNotification);
    repaint();
}

void MasterColumn::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    engraved (g, "LOUDNESS TARGET", { 0.0f, 126.0f, w, 10.0f }, 6.5f);
    engraved (g, "PROFIL", { 0.0f, 234.0f, w, 10.0f }, 6.5f);
    engraved (g, utf8 ("INTENSITÉ"), { 0.0f, 272.0f, w / 2.0f, 10.0f }, 6.5f);
    engraved (g, "CEILING", { w / 2.0f, 272.0f, w / 2.0f, 10.0f }, 6.5f);
}

// ======================================================================================
MasterConsole::MasterConsole (FINISHAudioProcessor& p) : proc (p) {}

void MasterConsole::paint (juce::Graphics& g)
{
    const auto& d = proc.getDSP();
    const int t = proc.getTargetIndex();
    const bool on = t > 0;
    auto r = getLocalBounds().toFloat().reduced (2.0f, 10.0f);
    struct Module { juce::String name, value, sub; float meter; bool active; };
    auto sgn = [] (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1); };
    const float eqL = proc.getAutoEqLow(), eqH = proc.getAutoEqHigh(), mud = proc.getAutoMud(), pres = proc.getAutoPres();
    const float side = proc.getAutoSide(), monoAmt = proc.getAutoMono();
    const Module mods[] = {
        { "TONE", on ? sgn (eqL) + " " + sgn (mud) + " " + sgn (pres) + " " + sgn (eqH) : "OFF", "110  300  3.2k  9k dB",
          std::min (1.0f, (std::abs (eqL) + std::abs (eqH) + std::abs (mud) + std::abs (pres)) / 6.0f), on },
        { "WIDTH", on ? sgn (side) + (monoAmt > 0.5f ? " M" : "") : "OFF", monoAmt > 0.5f ? "SIDES dB  MONO <160" : "SIDES dB", std::min (1.0f, std::abs (side) / 3.0f), on },
        { "LEVELLER", on ? juce::String (d.getBandGrDb (0), 1) + " " + juce::String (d.getBandGrDb (1), 1) + " " + juce::String (d.getBandGrDb (2), 1) : "OFF",
          proc.getAutoPunchKeep() > 0.5f ? "L M H dB  PUNCH" : "LOW MID HIGH dB", std::min (1.0f, (d.getBandGrDb (0) + d.getBandGrDb (1) + d.getBandGrDb (2)) / 6.0f), on },
        { "DRIVE", juce::String (on ? proc.getAutoDriveDb() + d.getLoudnessCorrectionDb() : 0.0f, 1), on ? "AUTO TO TARGET dB" : "LOUDNESS KNOB", 0.5f, true },
        { "CLIPPER", on ? juce::String (d.getClipDb(), 1) : "OFF", "SHAVED dB", std::min (1.0f, d.getClipDb() / 2.0f), on },
        { "LIMITER", juce::String (-d.getGainReductionDb(), 1), "TRUE PEAK", std::min (1.0f, d.getGainReductionDb() / 8.0f), true },
        { "OUT", juce::String (d.getOutputLoudness(), 1), "LUFS SHORT", 0.0f, true },
    };
    const int n = (int) (sizeof (mods) / sizeof (mods[0]));
    const float mw = (r.getWidth() - (n - 1) * 12.0f) / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto box = juce::Rectangle<float> (r.getX() + (float) i * (mw + 12.0f), r.getY() + 12.0f, mw, r.getHeight() - 12.0f);
        drawScreen (g, box, 8.0f, true);
        drawLed (g, { box.getX() + 12.0f, box.getY() + 14.0f }, 3.0f, mods[i].active ? mint : dusk, mods[i].active);
        g.setFont (label (8.0f));
        g.setColour (mods[i].active ? moon : dusk);
        g.drawText (mods[i].name, box.reduced (22.0f, 6.0f).withHeight (16.0f), juce::Justification::centredLeft);
        g.setFont (mono (mods[i].value.length() > 14 ? 9.5f : (mods[i].value.length() > 8 ? 12.0f : 20.0f)));
        g.setColour (mods[i].active ? cyan : dusk);
        g.drawText (mods[i].value, box.reduced (12.0f, 0).withTrimmedTop (30.0f).withHeight (36.0f), juce::Justification::centredLeft);
        g.setFont (mono (7.5f));
        g.setColour (dusk);
        g.drawText (mods[i].sub, box.reduced (12.0f, 8.0f), juce::Justification::bottomLeft);
        // activity bar
        auto bar = juce::Rectangle<float> (box.getX() + 12.0f, box.getBottom() - 30.0f, box.getWidth() - 24.0f, 4.0f);
        g.setColour (line);
        g.fillRoundedRectangle (bar, 2.0f);
        if (mods[i].active && mods[i].meter > 0.0f)
        {
            g.setGradientFill (aurora (bar.getX(), 0, bar.getRight(), 0, 1.0f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * mods[i].meter), 2.0f);
        }
        if (i < n - 1)
        {
            const float ax = box.getRight() + 3.0f, ay = box.getCentreY();
            juce::Path arrow;
            arrow.addTriangle (ax, ay - 4.0f, ax, ay + 4.0f, ax + 8.0f, ay);
            g.setColour (lineHi);
            g.fillPath (arrow);
        }
    }
}
