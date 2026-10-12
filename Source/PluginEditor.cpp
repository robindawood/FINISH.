#include "PluginEditor.h"

using namespace Theme;

WhaleEditor::WhaleEditor (WhaleAudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), presetBar(), ab (p.state), chain (p.state), expert (p), browser (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (scene);
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (ab);
    content.addAndMakeVisible (expertLink);
    content.addAndMakeVisible (menuLink);
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
    styleBox.setTooltip (fr ("Style de référence. « Auto » : WHALE le détecte (tempo + équilibre du son)."));
    targetBox.setTooltip (fr ("Cible de volume. « Style » = le volume mesuré sur les masters de référence du style."));
    dosage.setTooltip (fr ("Intensité des corrections d'AUTO et du preset (le volume cible n'est pas concerné)."));

    struct K { const char* id; const char* name; juce::Colour c; };
    const K ks[8] = { { PID::mGlue, "GLUE", magenta }, { PID::mPunch, "PUNCH", magenta }, { PID::mWarmth, "CHALEUR", violet }, { PID::mAir, "AIR", cyan },
                      { PID::mWidth, "LARGEUR", cyan }, { PID::mBass, "BASSES", violet }, { PID::mDetail, fr ("DÉTAIL").toRawUTF8(), cyan }, { PID::mLoud, "LOUDNESS", magenta } };
    const char* tips[8] = { "Colle le mix : tient les pics de chaque bande", "Renforce les attaques (kick, caisse claire)", "Saturation douce : densité et chaleur",
                            "Brillance au-dessus de 10 kHz", "Largeur stéréo (les graves restent au centre)", "Grave autour de 110 Hz",
                            "Présence 3,5 kHz + transitoires", "Volume : +/- 3 dB autour de la cible" };
    for (int i = 0; i < 8; ++i)
    {
        knobs.push_back (std::make_unique<MacroKnob> (proc.state, ks[i].id, fr (i == 6 ? "D\xc3\x89TAIL" : ks[i].name), ks[i].c));
        knobs.back()->slider.setTooltip (fr (tips[i]));
        content.addAndMakeVisible (*knobs.back());
    }

    content.addChildComponent (expert);
    content.addChildComponent (browser);

    autoButton.onClick = [this] { proc.toggleAuto(); };
    autoButton.setTooltip (fr ("Lance la lecture sur la partie la plus forte, puis clique : WHALE écoute 12 s, règle tout, puis vérifie. Re-clique pour annuler."));
    expertLink.onClick = [this] { openExpert (expert.isVisible() ? -1 : 0); };
    menuLink.onClick = [this] { showMenu(); };
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
    if (k == juce::KeyPress::escapeKey) { browser.setVisible (false); openExpert (-1); return true; }
    return false;
}

void WhaleEditor::Content::resized() { owner.layout(); }

void WhaleEditor::layout()
{
    scene.setBounds (0, 56, designW, 452);
    presetBar.setBounds (390, 11, 420, 36);
    ab.setBounds (900, 12, 150, 34);
    expertLink.setBounds (1062, 12, 80, 34);
    menuLink.setBounds (1148, 12, 40, 34);
    chain.setBounds (20, 516, 1160, 40);
    autoButton.setBounds (14, 566, 184, 184);
    styleBox.setBounds (210, 600, 150, 30);
    targetBox.setBounds (210, 660, 150, 30);
    dosage.setBounds (206, 716, 158, 20);
    for (int i = 0; i < 8; ++i)
        knobs[(size_t) i]->setBounds (400 + i * 72 + (i >= 4 ? 30 : 0), 600, 64, 98);
    meters.setBounds (1000, 590, 180, 150);
    expert.setBounds (14, 62, designW - 28, 440);
    browser.setBounds (0, 50, designW, designH - 50);
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
    g.drawText ("CIBLE", 210, 642, 150, 14, juce::Justification::centredLeft);
    g.drawText ("DOSAGE", 210, 700, 100, 14, juce::Justification::centredLeft);
    g.setColour (text);
    g.drawText (juce::String (juce::roundToInt (owner.dosage.getValue() * 100)) + " %", 300, 700, 60, 14, juce::Justification::centredRight);
    g.setColour (dim);
    g.drawText (fr ("CARACTÈRE"), 400, 580, 200, 14, juce::Justification::centredLeft);
    g.drawText (fr ("ESPACE & NIVEAU"), 718, 580, 200, 14, juce::Justification::centredLeft);
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
    m.addItem (1, fr ("Rapport AUTO (ce qui a été fait)"));
    m.addItem (2, fr ("Animations réduites (ordinateurs moins puissants)"), true, reducedAnimations());
    m.addSeparator();
    m.addItem (3, fr ("Tout remettre à neutre"));
    m.addItem (4, fr ("Ouvrir le dossier de mes presets"));
    m.addSeparator();
    m.addItem (5, juce::String ("WHALE ") + WHALE_VERSION_STRING + fr (" — mastering par styles de référence"), false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuLink), [this] (int r)
    {
        if (r == 1) openExpert (7);
        else if (r == 2) setReducedAnimations (! reducedAnimations());
        else if (r == 3) proc.resetToNeutral();
        else if (r == 4) { auto d = PresetManager::userFolder(); d.createDirectory(); d.startAsProcess(); }
    });
}

void WhaleEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = forcedDt > 0.0f ? forcedDt : (float) juce::jlimit (0.001, 0.1, (now - lastTime) / 1000.0);
    lastTime = now;
    auto& d = proc.getDSP();

    // ---- meters
    const float pk = d.takeMaxPeakDb();
    tpHoldAge += dt; peak10Age += dt;
    if (pk > tpHold || tpHoldAge > 2.0f) { tpHold = pk; tpHoldAge = 0; }
    if (pk > peak10 || peak10Age > 10.0f) { peak10 = pk; peak10Age = 0; }
    const float outSt = d.getOutputLoudness();
    lufsSmooth = outSt < -60 ? outSt : lufsSmooth + (outSt - lufsSmooth) * 0.08f;
    const bool sound = d.getInputLoudness() > -50.0f;
    const float ceil = proc.state.getRawParameterValue (PID::ceiling)->load();
    const float plr = peak10 - outSt;
    const float corr = d.getCorrelation();
    const bool monoOn = proc.state.getRawParameterValue (PID::widthOn)->load() > 0.5f && proc.state.getRawParameterValue (PID::widthMono)->load() >= 20.0f;
    std::array<MeterPanel::Row, 4> rows;
    rows[0] = { "TRUE PEAK", sound ? juce::String (tpHold, 1) + " dBTP" : "--", juce::jlimit (0.0f, 1.0f, (tpHold + 24.0f) / 24.0f), tpHold > ceil + 0.15f ? red : green };
    rows[1] = { "DYNAMIQUE", sound ? juce::String (plr, 1) + " dB" : "--", juce::jlimit (0.0f, 1.0f, plr / 14.0f), plr < 5.0f ? red : plr < 7.0f ? orange : green };
    rows[2] = { fr ("STÉRÉO"), sound ? (corr >= 0 ? "+" : "") + juce::String (corr, 2) : "--", juce::jlimit (0.0f, 1.0f, (corr + 1.0f) * 0.5f), corr < 0.0f ? red : corr < 0.25f ? orange : green };
    rows[3] = { "MONO < " + juce::String (monoOn ? juce::roundToInt (proc.state.getRawParameterValue (PID::widthMono)->load()) : 120) + " Hz",
                monoOn ? juce::String ("OK") : fr ("LIBRE"), monoOn ? 1.0f : 0.4f, monoOn ? green : orange };
    meters.setRows (rows);

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
    if (++frameCounter % 2 == 0) chain.repaint();

    // ---- AUTO button
    const auto st = proc.getAutoState();
    if (st == WhaleAudioProcessor::AutoState::listening || st == WhaleAudioProcessor::AutoState::verifying)
        autoButton.setState (1, proc.getAutoProgress(), proc.isWaitingForAudio() ? fr ("EN ATTENTE") : juce::String (juce::roundToInt (proc.getAutoProgress() * 100)) + " %");
    else if (st == WhaleAudioProcessor::AutoState::done)
        autoButton.setState (2, 1.0f, fr ("TERMINÉ"));
    else
        autoButton.setState (0, 0.0f, "MASTER");
    autoButton.tick (dt);

    // ---- preset bar
    {
        const int cur = proc.presets.getCurrent();
        const auto& all = proc.presets.getAll();
        juce::String name = "Init", cat;
        if (juce::isPositiveAndBelow (cur, all.size())) { name = all[cur].name; cat = all[cur].factory ? styleShortNames()[all[cur].style] : fr ("MES PRESETS"); }
        else if (proc.getAutoResult().valid && st != WhaleAudioProcessor::AutoState::idle) { name = fr ("Master AUTO"); cat = styleShortNames()[proc.getAutoResult().style]; }
        presetBar.set (name, cat, cur >= 0 && proc.presets.isFavourite (name), browser.isVisible());
    }

    // ---- whale
    WhaleScene::Input in;
    for (int b = 0; b < 8; ++b) in.bands[b] = d.getVisBand (b);
    in.kick = d.getKick();
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
    in.targetText = proc.isTargetLoudnessMode() ? fr ("CIBLE ") + juce::String (tgt, 1) + " LUFS (" + (tIdx == styleTarget ? styleShortNames()[proc.getActiveStyle()] : juce::String (loudnessTargets[tIdx].shortName)) + ")"
                                                : juce::String (loudnessTargets[tIdx].shortName);
    in.meter = juce::jlimit (0.0f, 1.0f, (lufsSmooth + 30.0f) / 26.0f);
    if (st == WhaleAudioProcessor::AutoState::listening)
        in.caption = proc.isWaitingForAudio() ? fr ("AUTO : lance la lecture sur la partie la plus forte (drop / refrain)") : fr ("AUTO écoute ton morceau… la baleine remonte vers la cible");
    else if (st == WhaleAudioProcessor::AutoState::verifying) in.caption = fr ("AUTO a réglé la chaîne et vérifie le résultat");
    else if (st == WhaleAudioProcessor::AutoState::done)
        in.caption = fr ("Style ") + styleShortNames()[proc.getAutoResult().style] + fr (" — rapport complet : menu ≡ ou EXPERT › RAPPORT AUTO");
    else if (! sound) in.caption = fr ("Lance la lecture, choisis un preset ou clique sur AUTO");
    scene.advance (in, dt);
    scene.repaint();
    if (expert.isVisible() && frameCounter % 3 == 0) expert.tick();
}

juce::AudioProcessorEditor* WhaleAudioProcessor::createEditor() { return new WhaleEditor (*this); }
