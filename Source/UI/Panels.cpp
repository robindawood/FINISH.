#include "Panels.h"

using namespace Night;

// ======================================================================================
TrackPanel::TrackPanel (FINISHAudioProcessor& p) : proc (p)
{
    intentEditor.setFont (mono (11.5f));
    intentEditor.setTextToShowWhenEmpty (u8 ("TYPE YOUR INTENT\xe2\x80\xa6"), dusk);
    intentEditor.setIndents (10, 10);
    intentEditor.setText (proc.getIntentText(), false);
    intentEditor.onReturnKey = [this] { submit(); };
    intentEditor.setTooltip (u8 ("Tell FINISH what you want, in English or French. Ex: \"plus large mais pas plus fort\", \"make the drop hit harder\"."));
    go.getProperties().set ("style", "chip");
    go.onClick = [this] { submit(); };
    addAndMakeVisible (intentEditor);
    addAndMakeVisible (go);
}

void TrackPanel::submit()
{
    proc.setIntent (intentEditor.getText());
    intentEditor.unfocusAllComponents();
    if (onIntent) onIntent();
}

void TrackPanel::refresh()
{
    if (! intentEditor.hasKeyboardFocus (true) && intentEditor.getText() != proc.getIntentText())
        intentEditor.setText (proc.getIntentText(), false);
    repaint();
}

void TrackPanel::resized()
{
    intentEditor.setBounds (4, 298, 222, 40);
    go.setBounds (230, 304, 42, 28);
}

void TrackPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.position.y > 140.0f && e.position.y < 280.0f && onShowTrack) onShowTrack();
}

void TrackPanel::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    engraved (g, u8 ("PRESS  \xc2\xb7  PLAY  \xc2\xb7  STOP"), { 0.0f, 126.0f, w, 10.0f }, 6.5f);

    // Score: 7-segment display
    const auto f = proc.getFeatures();
    const int score = f.valid ? proc.getScore() : -1;
    auto lcd = juce::Rectangle<float> (8.0f, 146.0f, w - 16.0f, 60.0f);
    drawScreen (g, lcd, 6.0f, true);
    const auto segCol = score < 0 ? dusk : (score >= 85 ? mint : (score >= 65 ? amber : violet));
    drawSevenSeg (g, juce::Rectangle<float> (lcd.getX() + 10.0f, lcd.getY() + 9.0f, 96.0f, 42.0f),
                  score < 0 ? juce::String ("---") : juce::String (score).paddedLeft ('0', 3), segCol);
    g.setColour (dusk);
    g.setFont (mono (7.5f));
    g.drawText ("FINISH SCORE", juce::Rectangle<float> (lcd.getX() + 118.0f, lcd.getY() + 7.0f, 120.0f, 10.0f), juce::Justification::centredLeft);
    g.setColour (segCol);
    g.setFont (mono (10.0f));
    g.drawText (score < 0 ? "NOT ANALYSED" : (score >= 85 ? "READY" : (score >= 65 ? "ALMOST THERE" : "NEEDS WORK")),
                juce::Rectangle<float> (lcd.getX() + 118.0f, lcd.getY() + 19.0f, 130.0f, 13.0f), juce::Justification::centredLeft);
    if (f.valid)
    {
        g.setColour (cyan.withAlpha (0.85f));
        g.setFont (mono (9.0f));
        g.drawText (juce::String (f.integratedLufs, 1) + " LUFS  PLR " + juce::String (f.plr, 1),
                    juce::Rectangle<float> (lcd.getX() + 118.0f, lcd.getY() + 36.0f, 136.0f, 12.0f), juce::Justification::centredLeft);
    }
    drawScreenGlass (g, lcd, 6.0f, 0.5f);

    // Opportunities LCD
    auto opp = juce::Rectangle<float> (8.0f, 216.0f, w - 16.0f, 62.0f);
    drawScreen (g, opp, 6.0f, true);
    g.setColour (dusk);
    g.setFont (mono (7.5f));
    g.drawText ("TOP OPPORTUNITIES", opp.reduced (10.0f, 5.0f).withHeight (10.0f), juce::Justification::centredLeft);
    const auto& d = proc.getDiagnoses();
    float y = opp.getY() + 18.0f;
    if (! f.valid)
    {
        g.setColour (dusk);
        g.setFont (mono (9.5f));
        g.drawText ("-- WAITING FOR ANALYSIS --", juce::Rectangle<float> (opp.getX() + 10.0f, y + 6.0f, opp.getWidth() - 20.0f, 14.0f), juce::Justification::centredLeft);
    }
    else if (d.empty())
    {
        drawLed (g, { opp.getX() + 15.0f, y + 7.0f }, 2.5f, mint, true);
        g.setColour (mint);
        g.setFont (mono (9.5f));
        g.drawText ("NOTHING HOLDS IT BACK", juce::Rectangle<float> (opp.getX() + 24.0f, y, opp.getWidth() - 30.0f, 14.0f), juce::Justification::centredLeft);
    }
    for (size_t i = 0; i < d.size() && i < 3; ++i)
    {
        const auto col = d[i].severity > 0.66f ? rose : (d[i].severity > 0.33f ? amber : mist);
        drawLed (g, { opp.getX() + 15.0f, y + 7.0f }, 2.5f, col, true);
        g.setColour (moon.withAlpha (0.9f));
        g.setFont (mono (9.0f));
        g.drawText (d[i].title, juce::Rectangle<float> (opp.getX() + 24.0f, y, opp.getWidth() - 64.0f, 14.0f), juce::Justification::centredLeft, true);
        g.setColour (dusk);
        g.drawText (juce::String (juce::roundToInt (d[i].confidence * 100.0f)) + "%", juce::Rectangle<float> (opp.getRight() - 40.0f, y, 32.0f, 14.0f), juce::Justification::centredRight);
        y += 13.5f;
    }
    drawScreenGlass (g, opp, 6.0f, 0.5f);

    // Intent
    engraved (g, "INTENT  EN / FR", { 8.0f, 284.0f, 120.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
    const auto& u = proc.getIntentUnderstood();
    if (proc.getIntentText().isNotEmpty())
    {
        g.setColour (u.isEmpty() ? dusk : cyan);
        g.setFont (mono (8.5f));
        g.drawText (u.isEmpty() ? juce::String ("NOT UNDERSTOOD") : u.joinIntoString (" "), juce::Rectangle<float> (110.0f, 283.0f, w - 118.0f, 12.0f), juce::Justification::centredRight, true);
    }
}

// ======================================================================================
ControlDeck::ControlDeck (FINISHAudioProcessor& p) : proc (p)
{
    const std::array<std::tuple<const char*, const char*, const char*>, 7> defs { {
        { ParamIDs::amount,    "AMOUNT",    "How much of the FINISH processing is blended in (global)." },
        { ParamIDs::punch,     "PUNCH",     "Transient emphasis: sharper attacks on drums and plucks." },
        { ParamIDs::impact,    "IMPACT",    "Glue compression: density and cohesion." },
        { ParamIDs::width,     "WIDTH",     "Stereo width. Above 100 % the bass below 140 Hz is folded to mono." },
        { ParamIDs::warmth,    "WARMTH",    "Tone tilt: right = warmer, left = brighter." },
        { ParamIDs::loudness,  "LOUDNESS",  "Saturation and limiter drive. Peaks never exceed -1 dBFS." },
        { ParamIDs::coherence, "COHERENCE", "FINISH MY TRACK: pulls section edits towards one consistent sound. 0 % = every section on its own." } } };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        const auto* id = std::get<0> (defs[i]);
        k.name = std::get<1> (defs[i]);
        k.slider.setTooltip (std::get<2> (defs[i]));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 14);
        k.slider.getProperties().set ("bipolar", juce::String (id) == ParamIDs::width || juce::String (id) == ParamIDs::warmth);
        k.slider.getProperties().set ("lunar", juce::String (id) == ParamIDs::coherence);
        if (auto* param = proc.params.getParameter (id))
            k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, k.slider);
    }
}

void ControlDeck::resized()
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int x = 6 + (int) i * 104 + (i == 6 ? 18 : 0);
        knobs[i].slider.setBounds (x, 12, 96, 98);
    }
    meterX = 790;
}

void ControlDeck::paintVU (juce::Graphics& g, juce::Rectangle<float> r, float vu, const juce::String& name, const juce::String& extra)
{
    drawScreen (g, r, 6.0f, true);
    // Backlight
    juce::ColourGradient back (violet.withAlpha (0.22f), r.getCentreX(), r.getBottom(), violet.withAlpha (0.0f), r.getCentreX(), r.getY(), false);
    g.setGradientFill (back);
    g.fillRoundedRectangle (r, 6.0f);

    const auto pivot = juce::Point<float> (r.getCentreX(), r.getBottom() + 18.0f);
    const float radius = r.getHeight() * 0.92f;
    auto angleFor = [] (float v) { return juce::jmap (juce::jlimit (-20.0f, 3.0f, v), -20.0f, 3.0f, -0.85f, 0.85f); };

    // Scale
    juce::Path arc, red;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0, angleFor (-20.0f), angleFor (0.0f), true);
    red.addCentredArc (pivot.x, pivot.y, radius, radius, 0, angleFor (0.0f), angleFor (3.0f), true);
    g.setColour (mist.withAlpha (0.6f));
    g.strokePath (arc, juce::PathStrokeType (1.2f));
    glowStroke (g, red, violet, 2.5f, 0.7f);
    g.setFont (mono (7.0f));
    for (int iv : { -20, -10, -7, -5, -3, -1, 0, 1, 2, 3 })
    {
        const float v = (float) iv;
        const float a = angleFor (v);
        const juce::Point<float> p0 (pivot.x + radius * std::sin (a), pivot.y - radius * std::cos (a));
        const juce::Point<float> p1 (pivot.x + (radius - 6.0f) * std::sin (a), pivot.y - (radius - 6.0f) * std::cos (a));
        g.setColour (v > 0.0f ? violet : mist);
        g.drawLine ({ p0, p1 }, 1.2f);
        if (iv == -20 || iv == -10 || iv == -5 || iv == -3 || iv == 0 || iv == 3)
        {
            const juce::Point<float> pt (pivot.x + (radius + 9.0f) * std::sin (a), pivot.y - (radius + 9.0f) * std::cos (a));
            g.drawText (juce::String (std::abs (iv)), juce::Rectangle<float> (20.0f, 10.0f).withCentre (pt), juce::Justification::centred);
        }
    }
    neonText (g, "VU", display (10.0f, 0.1f), r.withTrimmedTop (r.getHeight() * 0.42f).withHeight (16.0f), cyan, juce::Justification::centred, 0.6f);
    g.setColour (mist);
    g.setFont (mono (8.0f));
    g.drawText (name, r.reduced (10.0f, 6.0f), juce::Justification::bottomLeft);
    g.setColour (cyan.withAlpha (0.8f));
    g.drawText (extra, r.reduced (10.0f, 6.0f), juce::Justification::bottomRight);

    // Needle
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    const float a = angleFor (vu);
    juce::Path needle;
    needle.startNewSubPath (pivot);
    needle.lineTo (pivot.x + (radius + 4.0f) * std::sin (a), pivot.y - (radius + 4.0f) * std::cos (a));
    glowStroke (g, needle, moon, 1.3f, 0.6f);
    g.restoreState();
    drawScreenGlass (g, r, 6.0f, 0.0f);
}

void ControlDeck::paint (juce::Graphics& g)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto b = knobs[i].slider.getBounds().toFloat();
        engraved (g, knobs[i].name, { b.getX(), 0.0f, b.getWidth(), 12.0f }, 7.0f, juce::Justification::centred, i == 6 ? moon.withAlpha (0.8f) : engrave);
    }
    const float cx = (float) knobs[6].slider.getX() - 10.0f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawVerticalLine ((int) cx, 8.0f, (float) getHeight() - 8.0f);
    g.drawVerticalLine (meterX - 14, 8.0f, (float) getHeight() - 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawVerticalLine ((int) cx + 1, 8.0f, (float) getHeight() - 8.0f);
    g.drawVerticalLine (meterX - 13, 8.0f, (float) getHeight() - 8.0f);

    // VU: 0 VU = -14 LUFS (short-term)
    const auto& dsp = proc.getDSP();
    const float in = dsp.getInputLoudness() + 14.0f, out = dsp.getOutputLoudness() + 14.0f;
    inNeedle += (juce::jlimit (-22.0f, 4.0f, in) - inNeedle) * 0.3f;
    outNeedle += (juce::jlimit (-22.0f, 4.0f, out) - outNeedle) * 0.3f;
    const float gr = dsp.getGainReductionDb();
    paintVU (g, { (float) meterX, 8.0f, 160.0f, 96.0f }, inNeedle, "IN", dsp.getInputLoudness() < -69.0f ? "--" : juce::String (dsp.getInputLoudness(), 1));
    paintVU (g, { (float) meterX + 170.0f, 8.0f, 160.0f, 96.0f }, outNeedle, "OUT", "GR " + juce::String (gr, 1));
}
