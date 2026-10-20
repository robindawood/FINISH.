#include "PluginEditor.h"
#include "UI/Brand.h"

using namespace Theme;

WhaleEditor::WhaleEditor (WhaleAudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), presetBar(), ab (p.state), chain (p.state), expert (p), browser (p), licencePanel (p.licence)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (scene);
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (ab);
    content.addAndMakeVisible (expertLink);
    content.addAndMakeVisible (menuLink);
    content.addChildComponent (undoAutoLink);
    content.addAndMakeVisible (chain);
    content.addAndMakeVisible (autoButton);
    content.addAndMakeVisible (styleBox);
    content.addAndMakeVisible (targetBox);
    content.addAndMakeVisible (dosage);
    content.addAndMakeVisible (meters);

    for (auto& n : styleNames()) styleBox.addItem (n, styleBox.getNumItems() + 1);
    for (auto& t : loudnessTargets) targetBox.addItem (t.name, targetBox.getNumItems() + 1);
    styleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, PID::style, styleBox);
    targetAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, PID::target, targetBox);
    dosage.setSliderStyle (juce::Slider::LinearHorizontal);
    dosage.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dosage.setColour (juce::Slider::trackColourId, magenta);
    dosageAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.state, PID::dosage, dosage);
    dosage.onValueChange = [this] { content.repaint (dosage.getBounds().expanded (0, 22)); };
    styleBox.setTooltip (fr ("Reference style. “Auto”: WHALE detects it (tempo + tonal balance)."));
    targetBox.setTooltip (fr ("Loudness target. “Style” = the loudness measured on the style's reference masters."));
    dosage.setTooltip (fr ("Strength of AUTO's and the preset's corrections (the loudness target is not affected)."));

    struct K { const char* id; const char* name; juce::Colour c; };
    const K ks[8] = { { PID::mGlue, "GLUE", magenta }, { PID::mPunch, "PUNCH", magenta }, { PID::mWarmth, "WARMTH", violet }, { PID::mAir, "AIR", cyan },
                      { PID::mWidth, "WIDTH", cyan }, { PID::mBass, "BASS", violet }, { PID::mDetail, "DETAIL", cyan }, { PID::mLoud, "LOUDNESS", magenta } };
    const char* tips[8] = { "Glues the mix: holds the peaks of each band", "Boosts the hits (kick, snare)", "Soft saturation: density and warmth",
                            "Brightness above 10 kHz", "Stereo width (the low end stays in the centre)", "Low end around 110 Hz",
                            "Presence at 3.5 kHz + transients", "Loudness: +/- 3 dB around the target" };
    for (int i = 0; i < 8; ++i)
    {
        knobs.push_back (std::make_unique<MacroKnob> (proc.state, ks[i].id, fr (ks[i].name), ks[i].c));
        knobs.back()->slider.setTooltip (fr (tips[i]));
        content.addAndMakeVisible (*knobs.back());
    }

    content.addChildComponent (expert);
    content.addChildComponent (browser);
    content.addChildComponent (licencePanel);
    content.addChildComponent (welcome);
    welcome.onClose = [this] { showWelcome (false); };
    content.addChildComponent (licenceLink);
    licenceLink.onClick = [this] { showLicence (true); };
    licencePanel.onClose = [this] { showLicence (false); };
    proc.licence.update();
    proc.licence.recheckIfDue();
    updateLicenceLink();
    if (proc.licence.isLocked()) showLicence (true);
    else if (! WelcomeOverlay::wasSeen()) showWelcome (true);

    autoButton.onClick = [this] { proc.toggleAuto(); };
    autoButton.setTooltip (fr ("Start playback (ideally at the start of the drop), then click: WHALE listens for 30 s, sets the chain, then measures the processed sound and fine-tunes what is left. Click again during the analysis to cancel; afterwards, “Undo AUTO” restores your previous settings."));
    expertLink.onClick = [this] { openExpert (expert.isVisible() ? -1 : 0); };
    menuLink.onClick = [this] { showMenu(); };
    undoAutoLink.onClick = [this] { proc.undoAuto(); };
    undoAutoLink.setTooltip (fr ("Go back to exactly the settings you had before AUTO (preset, knobs, target)"));
    chain.onOpenModule = [this] (int m) { openExpert (m); };
    presetBar.onOpen = [this] { browser.refresh(); browser.setVisible (! browser.isVisible()); expert.setVisible (false); };
    presetBar.onStep = [this] (int d)
    {
        const int n = proc.presets.getAll().size();
        const int cur = proc.presets.getCurrent();
        proc.applyPreset (((cur < 0 ? (d > 0 ? -1 : 0) : cur) + d + n) % n);
    };
    presetBar.onFavourite = [this]
    {
        const auto name = proc.presets.getCurrentName();
        if (proc.presets.getCurrent() >= 0) proc.presets.setFavourite (name, ! proc.presets.isFavourite (name));
    };
    browser.onClose = [this] { browser.setVisible (false); };
    expert.onClose = [this] { openExpert (-1); };

    setResizable (true, true);
    setResizeLimits (designW * 7 / 10, designH * 7 / 10, designW * 3 / 2, designH * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / designH);
    setWantsKeyboardFocus (true);
#if JUCE_MODULE_AVAILABLE_juce_opengl
    gl.setComponentPaintingEnabled (true);
    gl.attachTo (*this);
#endif
    setSize (designW, designH);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

WhaleEditor::~WhaleEditor()
{
    stopTimer();
   #if JUCE_MODULE_AVAILABLE_juce_opengl
    gl.detach();
   #endif
    setLookAndFeel (nullptr);
}

void WhaleEditor::paint (juce::Graphics& g) { g.fillAll (bg); }

void WhaleEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content.setBounds (0, 0, designW, designH);
    content.setTransform (juce::AffineTransform::scale (s));
}

bool WhaleEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { showLicence (false); browser.setVisible (false); openExpert (-1); return true; }
    return false;
}

void WhaleEditor::Content::resized() { owner.layout(); }

void WhaleEditor::layout()
{
    scene.setBounds (0, 56, designW, 452);
    presetBar.setBounds (390, 11, 420, 36);
    ab.setBounds (900, 12, 150, 47);
    expertLink.setBounds (1062, 12, 80, 34);
    menuLink.setBounds (1148, 12, 40, 34);
    chain.setBounds (20, 509, 1160, 51);
    autoButton.setBounds (14, 566, 184, 184);
    undoAutoLink.setBounds (10, 108, 150, 26);
    styleBox.setBounds (210, 600, 150, 30);
    targetBox.setBounds (210, 660, 150, 30);
    dosage.setBounds (206, 716, 158, 20);
    for (int i = 0; i < 8; ++i)
        knobs[(size_t) i]->setBounds (396 + i * 72 + (i >= 4 ? 30 : 0), 594, 72, 104);
    meters.setBounds (1000, 584, 180, 158);
    expert.setBounds (14, 62, designW - 28, 440);
    browser.setBounds (0, 50, designW, designH - 50);
    licenceLink.setBounds (272, 15, 108, 26);
    licencePanel.setBounds (0, 0, designW, designH);
    welcome.setBounds (0, 0, designW, designH);
}

void WhaleEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // header
    g.setColour (juce::Colour (0xff09061a));
    g.fillRect (0, 0, designW, 56);
    g.setColour (line);
    g.drawHorizontalLine (55, 0.0f, (float) designW);
    g.setColour (juce::Colour (0xffece4ff));
    g.fillPath (flukeGlyph (40, 26, 18));
    g.setColour (juce::Colours::white);
    g.setFont (display (27.0f, 0.04f));
    g.drawText ("WHALE", 66, 10, 140, 34, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (mono (11.0f, 0.12f));
    g.drawText ("MASTERING", 180, 12, 120, 34, juce::Justification::centredLeft);
    Brand::drawLockup (g, 821.0f, 29.0f, 15.0f, 0.85f);     // Rawdio Labs, between the preset bar and BEFORE / AFTER

    // bottom panel
    g.setColour (juce::Colour (0xff08051a));
    g.fillRect (0, 508, designW, designH - 508);
    g.setColour (lineHi.withAlpha (0.7f));
    g.drawHorizontalLine (508, 0.0f, (float) designW);
    g.setColour (line);
    g.drawHorizontalLine (562, 20.0f, (float) designW - 20.0f);
    for (int x : { 380, 980 }) g.drawVerticalLine (x, 576.0f, 740.0f);

    g.setFont (mono (10.5f, 0.1f));
    g.setColour (dim);
    g.drawText ("STYLE", 210, 582, 150, 14, juce::Justification::centredLeft);
    g.drawText ("TARGET", 210, 642, 150, 14, juce::Justification::centredLeft);
    g.drawText ("AMOUNT", 210, 700, 100, 14, juce::Justification::centredLeft);
    g.setColour (text);
    g.drawText (juce::String (juce::roundToInt (owner.dosage.getValue() * 100)) + " %", 300, 700, 60, 14, juce::Justification::centredRight);
    g.setColour (dim);
    g.drawText (fr ("CHARACTER"), 400, 580, 200, 14, juce::Justification::centredLeft);
    g.drawText (fr ("SPACE & LEVEL"), 718, 580, 200, 14, juce::Justification::centredLeft);
    g.setColour (line);
    g.drawHorizontalLine (587, 480.0f, 680.0f);
    g.drawHorizontalLine (587, 840.0f, 966.0f);
}

void WhaleEditor::openExpert (int module)
{
    if (module < 0) { expert.setVisible (false); expertLink.active = false; chain.setSelected (-1); }
    else
    {
        browser.setVisible (false);
        expert.showModule (module);
        expert.setVisible (true);
        expertLink.active = true;
        chain.setSelected (module < 7 ? module : -1);
    }
    expertLink.repaint();
}

bool WhaleEditor::reducedAnimations() const { return (bool) proc.state.state.getProperty ("reducedAnim", false); }
void WhaleEditor::setReducedAnimations (bool r) { proc.state.state.setProperty ("reducedAnim", r, nullptr); startTimerHz (r ? 30 : 60); }

void WhaleEditor::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, fr ("AUTO report (what was done)"));
    m.addItem (2, fr ("Reduced animations (slower computers)"), true, reducedAnimations());
    m.addSeparator();
    m.addItem (6, fr ("Undo AUTO (back to the previous settings)"), proc.canUndoAuto());
    m.addItem (3, fr ("Reset everything to neutral"));
    m.addItem (4, fr ("Open my presets folder"));
    m.addItem (8, fr ("Quick tour (first steps)"));
    m.addItem (7, proc.licence.getStatus() == LicenseManager::Status::activated ? fr ("License (activated)") : fr ("License: activate WHALE…"));
    m.addSeparator();
    m.addItem (5, juce::String ("WHALE ") + WHALE_VERSION_STRING + fr (" by Rawdio Labs — mastering by reference styles"), false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuLink), [this] (int r)
    {
        if (r == 1) openExpert (7);
        else if (r == 2) setReducedAnimations (! reducedAnimations());
        else if (r == 3) proc.resetToNeutral();
        else if (r == 6) proc.undoAuto();
        else if (r == 7) showLicence (true);
        else if (r == 8) showWelcome (true);
        else if (r == 4) { auto d = PresetManager::userFolder(); d.createDirectory(); d.startAsProcess(); }
    });
}

void WhaleEditor::showWelcome (bool show)
{
    if (show) { browser.setVisible (false); openExpert (-1); welcome.toFront (true); }
    welcome.setVisible (show);
}

void WhaleEditor::showLicence (bool show)
{
    if (show) { browser.setVisible (false); openExpert (-1); licencePanel.refresh(); licencePanel.toFront (false); }
    licencePanel.setVisible (show);
    updateLicenceLink();
}

void WhaleEditor::updateLicenceLink()
{
    const auto st = proc.licence.getStatus();
    licenceLink.setVisible (st != LicenseManager::Status::activated);
    if (st == LicenseManager::Status::trial)
    {
        licenceLink.setText (fr ("TRIAL · ") + juce::String (proc.licence.trialDaysLeft()) + "D");
        licenceLink.setTooltip (fr ("Full trial version. Click to activate your license."));
    }
    else
    {
        licenceLink.setText (fr ("ACTIVATE"));
        licenceLink.setTooltip (fr ("Trial over: WHALE mutes the sound for 2 s every 40 s. Click to activate your license."));
    }
    licenceLink.active = st == LicenseManager::Status::expired;
}

void WhaleEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = forcedDt > 0.0f ? forcedDt : (float) juce::jlimit (0.001, 0.1, (now - lastTime) / 1000.0);
    lastTime = now;
    auto& d = proc.getDSP();

    // ---- meters
    const float pk = d.takeMaxPeakDb();                 // 4x true peak of what is heard (after SANS / AVEC)
    tpHoldAge += dt; peak10Age += dt;
    if (pk > tpHold || tpHoldAge > 2.0f) { tpHold = pk; tpHoldAge = 0; }
    if (pk > peak10 || peak10Age > 10.0f) { peak10 = pk; peak10Age = 0; }
    const float heard = d.getHeardLoudness();
    lufsSmooth = heard < -60 ? heard : lufsSmooth + (heard - lufsSmooth) * 0.08f;
    const bool sound = d.getInputLoudness() > -50.0f;
    const float ceil = proc.state.getRawParameterValue (PID::ceiling)->load();
    const bool dry = proc.state.getRawParameterValue (PID::bypass)->load() > 0.5f;
    const float plr = peak10 - heard;
    const float corr = d.getCorrelation();
    const float monoPct = 100.0f * d.getBassMono();
    const float tone = proc.getToneDistance();
    const float expCrest = proc.getExpectedCrest();
    std::array<MeterPanel::Row, MeterPanel::numRows> rows;
    rows[0] = { "TRUE PEAK", sound ? juce::String (tpHold, 1) + " dBTP" : "--", juce::jlimit (0.0f, 1.0f, (tpHold + 24.0f) / 24.0f),
                (! dry && tpHold > ceil + 0.1f) || tpHold > 0.0f ? red : green };
    // distance of the heard 1/3-octave balance to the style's references (the preset's colour is not counted)
    rows[1] = { fr ("TONE"), (sound && tone >= 0) ? juce::String (tone, 1) + " dB" : "--", tone < 0 ? 0.0f : juce::jlimit (0.0f, 1.0f, 1.0f - tone / 3.0f),
                tone < 0 ? dim : tone < 1.0f ? green : tone < 2.0f ? orange : red };
    // peak-to-loudness vs what the loudness target leaves: lower = squashed
    rows[2] = { "DYNAMICS", sound ? juce::String (plr, 1) + " dB" : "--", juce::jlimit (0.0f, 1.0f, plr / 14.0f),
                dry ? cyan : plr >= expCrest - 1.0f ? green : plr >= expCrest - 2.5f ? orange : red };
    rows[3] = { fr ("STEREO"), sound ? (corr >= 0 ? "+" : "") + juce::String (corr, 2) : "--", juce::jlimit (0.0f, 1.0f, (corr + 1.0f) * 0.5f),
                corr < 0.0f ? red : corr < 0.25f ? orange : green };
    rows[4] = { "MONO BASS", sound ? juce::String (juce::roundToInt (monoPct)) + " %" : "--", juce::jlimit (0.0f, 1.0f, monoPct / 100.0f),
                monoPct >= 90.0f ? green : monoPct >= 75.0f ? orange : red };
    if (! sound)
        for (auto& r : rows) { r.bar = 0.0f; r.colour = dim; }        // nothing heard: no verdict on the meters
    meters.setRows (rows);
    {
        const bool match = proc.state.getRawParameterValue (PID::match)->load() > 0.5f;
        const float g = d.getMatchGainDb();
        ab.setCaption (! match ? fr ("BEFORE at its original level")
                               : (proc.isMatchMeasured() ? fr ("BEFORE at matched level (") + (g >= 0 ? "+" : "") + juce::String (g, 1) + " dB)"
                                                         : fr ("BEFORE at matched level (measuring…)")));
    }

    // ---- chain activity
    float spec = 0; for (int b = 0; b < 4; ++b) spec = std::max (spec, d.getSpecDynDb (b));
    float comp = 0; for (int b = 0; b < 3; ++b) comp = std::max (comp, d.getBandGrDb (b));
    const float dos = proc.state.getRawParameterValue (PID::dosage)->load();
    chain.setActivity (0, sound ? spec / 3.0f + 0.15f : 0);
    chain.setActivity (1, sound ? comp / 3.0f : 0);
    chain.setActivity (2, sound ? dos * proc.state.getRawParameterValue (PID::satDrive)->load() + (knobs[2]->slider.getValue() - 0.5f) : 0);
    chain.setActivity (3, sound ? 0.3f + std::abs (proc.state.getRawParameterValue (PID::widthSide)->load()) / 4.0f : 0);
    chain.setActivity (4, sound ? (std::abs (proc.state.getRawParameterValue (PID::toneLow)->load()) + std::abs (proc.state.getRawParameterValue (PID::toneAir)->load())) / 5.0f + 0.1f : 0);
    chain.setActivity (5, sound ? d.getClipDb() / 2.5f : 0);
    chain.setActivity (6, sound ? d.getGainReductionDb() / 6.0f : 0);
    {
        auto db = [] (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
        auto raw = [this] (const char* id) { return proc.state.getRawParameterValue (id)->load(); };
        chain.setValueText (0, sound ? (spec > 0.1f ? db (-spec) : juce::String ("EQ")) : juce::String());
        chain.setValueText (1, sound ? db (-comp) : juce::String());
        chain.setValueText (2, sound ? juce::String (juce::roundToInt (100.0f * juce::jlimit (0.0f, 1.0f, dos * raw (PID::satDrive) + 0.8f * (float) (knobs[2]->slider.getValue() - 0.5)))) + " %" : juce::String());
        chain.setValueText (3, sound ? db (dos * raw (PID::widthSide) + 6.0f * (float) (knobs[4]->slider.getValue() - 0.5)) : juce::String());
        chain.setValueText (4, sound ? "L" + db (dos * raw (PID::toneLow) + 6.0f * (float) (knobs[5]->slider.getValue() - 0.5)).upToFirstOccurrenceOf (" ", false, false)
                                      + "  A" + db (dos * raw (PID::toneAir) + 6.0f * (float) (knobs[3]->slider.getValue() - 0.5)) : juce::String());
        chain.setValueText (5, sound ? db (-d.getClipDb()) : juce::String());
        chain.setValueText (6, sound ? db (-d.getGainReductionDb()) : juce::String());

        // knobs: the diamond = what AUTO / the preset set for the module, the value = what is heard with the nudge
        const bool compOn = raw (PID::compOn) > 0.5f, satOn = raw (PID::satOn) > 0.5f, widthOn = raw (PID::widthOn) > 0.5f, toneOn = raw (PID::toneOn) > 0.5f;
        auto m = [this] (int i) { return (float) knobs[(size_t) i]->slider.getValue() - 0.5f; };
        auto pct = [] (float v) { return juce::String (juce::roundToInt (100.0f * v)); };
        auto sdb = [] (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1); };
        auto unit = [&] (int i, bool on, float base, float k, auto fmt, const juce::String& suffix, bool dbScale)
        {
            if (! on) { knobs[(size_t) i]->setAutoInfo (-1.0f, -1.0f, "off", false); return; }
            const float eff = dbScale ? base + k * m (i) : juce::jlimit (0.0f, 1.0f, base + k * m (i));
            const bool moved = std::abs (m (i)) > 0.005f;
            auto pos = [&] (float v) { return dbScale ? juce::jlimit (0.0f, 1.0f, (v + 6.0f) / 12.0f) : v; };
            knobs[(size_t) i]->setAutoInfo (pos (base), pos (eff), moved ? fmt (base) + juce::String::fromUTF8 ("\xe2\x86\x92") + fmt (eff) + suffix : fmt (base) + suffix, moved);
        };
        unit (0, compOn, dos * raw (PID::compGlue), 1.0f, pct, " %", false);
        unit (1, compOn, dos * raw (PID::compPunch), 1.0f, pct, " %", false);
        unit (2, satOn, dos * raw (PID::satDrive), 0.8f, pct, " %", false);
        unit (3, toneOn, dos * raw (PID::toneAir), 6.0f, sdb, " dB", true);
        unit (4, widthOn, dos * raw (PID::widthSide), 6.0f, sdb, " dB", true);
        unit (5, toneOn, dos * raw (PID::toneLow), 6.0f, sdb, " dB", true);
        unit (6, compOn, raw (PID::compDetail), 0.8f, pct, " %", false);
        {
            const float tgt0 = proc.getEffectiveTarget(), eff = tgt0 + 6.0f * m (7);
            const bool moved = std::abs (m (7)) > 0.005f;
            auto f1 = [] (float v) { return juce::String (v, 1); };
            knobs[7]->setAutoInfo (0.5f, juce::jlimit (0.0f, 1.0f, 0.5f + m (7)), moved ? f1 (tgt0) + juce::String::fromUTF8 ("\xe2\x86\x92") + f1 (eff) : f1 (tgt0) + " LUFS", moved);
        }
    }
    if (++frameCounter % 2 == 0) chain.repaint();

    // ---- AUTO button
    const auto st = proc.getAutoState();
    if (st == WhaleAudioProcessor::AutoState::listening || st == WhaleAudioProcessor::AutoState::verifying)
        autoButton.setState (1, proc.getAutoProgress(), proc.isWaitingForAudio() ? fr ("WAITING") : juce::String (juce::roundToInt (proc.getAutoProgress() * 100)) + " %");
    else if (st == WhaleAudioProcessor::AutoState::done)
        autoButton.setState (2, 1.0f, fr ("DONE"));
    else
        autoButton.setState (0, 0.0f, "MASTER");
    autoButton.tick (dt);
    {
        const bool showUndo = st == WhaleAudioProcessor::AutoState::done && proc.canUndoAuto();
        if (undoAutoLink.isVisible() != showUndo) undoAutoLink.setVisible (showUndo);
    }

    // ---- preset bar
    {
        const int cur = proc.presets.getCurrent();
        const auto& all = proc.presets.getAll();
        juce::String name = "Init", cat;
        if (juce::isPositiveAndBelow (cur, all.size())) { name = all[cur].name; cat = all[cur].factory ? styleShortNames()[all[cur].style] : fr ("MY PRESETS"); }
        else if (proc.getAutoResult().valid && st != WhaleAudioProcessor::AutoState::idle) { name = fr ("Master AUTO"); cat = styleShortNames()[proc.getAutoResult().style]; }
        presetBar.set (name, cat, cur >= 0 && proc.presets.isFavourite (name), browser.isVisible());
    }

    // ---- whale
    WhaleScene::Input in;
    for (int b = 0; b < 8; ++b) in.bands[b] = d.getVisBand (b);
    in.kick = d.getKick();
    {
        const auto& prof = AutoMaster::profile (proc.getActiveStyle());
        const float* cc = proc.getCharacterCurve();
        for (int b = 0; b < 29; ++b)
        {
            const float c = cc != nullptr ? cc[b] : 0.0f;          // the preset's colour is intentional: the references move with it
            in.refMed[b] = prof.thirdOctMedian[b] + c; in.refLo[b] = prof.thirdOctP25[b] + c; in.refHi[b] = prof.thirdOctP75[b] + c;
        }
        in.hasSpectrum = sound && proc.getLiveSpectrum (in.spectrum);
        in.styleName = styleShortNames()[proc.getActiveStyle()];
    }
    in.width = d.getWidth();
    in.loudErr = sound ? proc.getLoudnessError() : -8.0f;
    in.reduced = reducedAnimations();
    in.souffleCount = proc.getSouffleCount();
    in.autoProgress = proc.getAutoProgress();
    in.autoRunning = st == WhaleAudioProcessor::AutoState::listening || st == WhaleAudioProcessor::AutoState::verifying;
    using V = WhaleAudioProcessor::Verdict;
    using M = WhaleScene::Mood;
    switch (proc.getVerdict())
    {
        case V::idle:      in.mood = M::idle;      in.verdictColour = dim; break;
        case V::ok:        in.mood = M::ok;        in.verdictColour = cyan; break;
        case V::working:   in.mood = M::working;   in.verdictColour = cyan; break;
        case V::validated: in.mood = M::validated; in.verdictColour = green; break;
        case V::tooLoud:   in.mood = M::tooLoud;   in.verdictColour = orange; break;
        case V::tooSoft:   in.mood = M::tooSoft;   in.verdictColour = violet; break;
        case V::clipping:  in.mood = M::clipping;  in.verdictColour = red; break;
        case V::phase:     in.mood = M::phase;     in.verdictColour = red; break;
    }
    in.verdictText = proc.getVerdictText();
    in.lufsText = lufsSmooth > -60 ? juce::String (lufsSmooth, 1) : "--";
    const int tIdx = (int) proc.state.getRawParameterValue (PID::target)->load();
    const float tgt = proc.getEffectiveTarget() + 6.0f * (knobs[7]->slider.getValue() - 0.5f);
    in.targetText = proc.isTargetLoudnessMode() ? fr ("DROP TARGET ") + juce::String (tgt, 1) + " LUFS (" + (tIdx == styleTarget ? styleShortNames()[proc.getActiveStyle()] : juce::String (loudnessTargets[tIdx].shortName)) + ")"
                                                : juce::String (loudnessTargets[tIdx].shortName);
    in.meter = juce::jlimit (0.0f, 1.0f, (lufsSmooth + 30.0f) / 26.0f);
    if (st == WhaleAudioProcessor::AutoState::listening)
        in.caption = proc.isWaitingForAudio() ? fr ("AUTO: play the loudest part (drop / chorus)") : fr ("AUTO is listening to your track… the whale rises to the target");
    else if (st == WhaleAudioProcessor::AutoState::verifying) in.caption = fr ("AUTO has set the chain and is checking the result");
    else if (st == WhaleAudioProcessor::AutoState::done)
        {
            const auto& ar = proc.getAutoResult();
            in.caption = fr ("Style ") + styleShortNames()[ar.style] + (ar.styleAlt > 0 ? fr (" (close to ") + styleShortNames()[ar.styleAlt] + ")" : juce::String())
                       + fr (" — full report: menu ≡ or EXPERT › AUTO REPORT");
        }
    else if (! sound) in.caption = fr ("Start playback, pick a preset or click AUTO");
    scene.advance (in, dt);
    scene.repaint();
    if (expert.isVisible() && frameCounter % 3 == 0) expert.tick();
    if (frameCounter % 60 == 0)
    {
        const auto before = proc.licence.getStatus();
        proc.licence.update();
        updateLicenceLink();
        if (licencePanel.isVisible() || before != proc.licence.getStatus()) licencePanel.refresh();
    }
}

juce::AudioProcessorEditor* WhaleAudioProcessor::createEditor() { return new WhaleEditor (*this); }
