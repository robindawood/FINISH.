#include "PluginEditor.h"

using namespace Night;

// ======================================================================================
void ArcadeButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    const float t = (float) (juce::Time::getMillisecondCounter() % 100000) / 1000.0f;

    // Outer halo
    const auto haloCol = mode == Mode::Listening ? cyan : violet;
    const float pulse = mode == Mode::Listening ? 0.6f + 0.4f * std::sin (t * 5.0f) : (highlighted ? 1.0f : 0.75f);
    juce::ColourGradient halo (haloCol.withAlpha (0.28f * pulse), c.x, c.y, haloCol.withAlpha (0.0f), c.x + R * 1.25f, c.y, true);
    g.setGradientFill (halo);
    g.fillEllipse (juce::Rectangle<float> (R * 2.5f, R * 2.5f).withCentre (c));

    // Chrome bezel
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c.translated (0, 3.0f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a8fb5), c.x, c.y - R, juce::Colour (0xff1a1326), c.x, c.y + R, false));
    g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
    const float domeR = R * 0.84f;
    g.setColour (juce::Colours::black);
    g.fillEllipse (juce::Rectangle<float> (domeR * 2.0f + 3.0f, domeR * 2.0f + 3.0f).withCentre (c));

    // Dark glass dome, pressed = slightly lower
    const auto dc = c.translated (0, down ? 1.5f : 0.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a1d40), dc.x, dc.y - domeR, juce::Colour (0xff07040d), dc.x, dc.y + domeR, false));
    g.fillEllipse (juce::Rectangle<float> (domeR * 2.0f, domeR * 2.0f).withCentre (dc));

    // Neon ring inside the dome
    const float ringR = domeR * 0.8f;
    const int segs = 64;
    const float spin = mode == Mode::Listening ? t * 2.2f : 0.0f;
    for (int i = 0; i < segs; ++i)
    {
        const float a0 = juce::MathConstants<float>::twoPi * (float) i / segs, a1 = a0 + juce::MathConstants<float>::twoPi / segs + 0.01f;
        const float u = std::fmod ((float) i / segs + spin / juce::MathConstants<float>::twoPi, 1.0f);
        const auto col = mode == Mode::Listening ? cyan.withAlpha (0.25f + 0.75f * u) : auroraAt (std::abs (u * 2.0f - 1.0f));
        juce::Path seg;
        seg.addCentredArc (dc.x, dc.y, ringR, ringR, 0, a0, a1, true);
        g.setColour (col.withMultipliedAlpha (0.22f * pulse));
        g.strokePath (seg, juce::PathStrokeType (9.0f));
        g.setColour (col.interpolatedWith (juce::Colours::white, 0.2f).withMultipliedAlpha (0.6f + 0.4f * pulse));
        g.strokePath (seg, juce::PathStrokeType (2.2f));
    }

    // Dome highlight
    juce::ColourGradient spec (juce::Colours::white.withAlpha (0.16f), dc.x, dc.y - domeR * 0.8f, juce::Colours::white.withAlpha (0.0f), dc.x, dc.y, false);
    g.setGradientFill (spec);
    g.fillEllipse (juce::Rectangle<float> (domeR * 1.4f, domeR * 0.8f).withCentre (dc.translated (0, -domeR * 0.45f)));

    // Legend
    auto txt = juce::Rectangle<float> (domeR * 1.5f, domeR * 1.2f).withCentre (dc);
    if (mode == Mode::Listening)
    {
        neonText (g, "STOP", display (12.0f, 0.12f), txt.withTrimmedBottom (txt.getHeight() * 0.45f), cyan, juce::Justification::centredBottom);
        g.setColour (cyan.interpolatedWith (juce::Colours::white, 0.4f));
        g.setFont (mono (13.0f));
        g.drawText (mmss (listenSeconds), txt.withTrimmedTop (txt.getHeight() * 0.55f), juce::Justification::centredTop);
    }
    else
    {
        const auto col = mode == Mode::Ready ? cyan : violet;
        neonText (g, "FINISH", display (13.0f, 0.1f), txt.withTrimmedBottom (txt.getHeight() * 0.5f), col, juce::Justification::centredBottom);
        g.setColour (mist);
        g.setFont (label (6.5f));
        g.drawText (mode == Mode::Ready ? "ANALYSE AGAIN" : "MY TRACK", txt.withTrimmedTop (txt.getHeight() * 0.55f), juce::Justification::centredTop);
    }
}

// ======================================================================================
FINISHAudioProcessorEditor::FINISHAudioProcessorEditor (FINISHAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), trackMap (p), inspector (p), trackPanel (p), deck (p)
{
    setLookAndFeel (&lnf);
    faceplate = makeFaceplate (baseWidth, baseHeight, 2.0f);
    addAndMakeVisible (content);

    for (auto* c : std::initializer_list<juce::Component*> { &refButton, &refClear, &matchButton, &bypassButton,
                                                             &trackMap, &inspector, &trackPanel, &deck, &finishButton })
        content.addAndMakeVisible (c);

    finishButton.setTooltip ("FINISH MY TRACK: press, play the whole song in your DAW, then stop playback.");
    finishButton.onClick = [this] { proc.toggleAnalysis(); refreshAll(); };

    refButton.setTooltip ("Load a finished commercial track to compare against (WAV, AIFF, FLAC, MP3, OGG).");
    refButton.getProperties().set ("led", "amber");
    refButton.onClick = [this] { chooseReference(); };
    refClear.getProperties().set ("style", "chip");
    refClear.setTooltip ("Remove reference");
    refClear.onClick = [this] { proc.clearReference(); refreshAll(); };

    matchButton.setClickingTogglesState (true);
    bypassButton.setClickingTogglesState (true);
    bypassButton.getProperties().set ("led", "amber");
    matchButton.setTooltip ("Keeps the output as loud as the input, so A/B compares quality, not volume.");
    bypassButton.setTooltip ("Hear the original track (click-free, level matched).");
    matchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, ParamIDs::match, matchButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, ParamIDs::bypass, bypassButton);

    trackMap.onSelect = [this] (TrackEngine::Selection s) { select (s); };
    inspector.onSelect = [this] (TrackEngine::Selection s) { select (s); };
    trackPanel.onShowTrack = [this] { select (TrackEngine::Selection::track()); };
    trackPanel.onIntent = [this] { refreshAll(); };

    sendLookAndFeelChange();

    setResizable (true, true);
    setResizeLimits ((int) (baseWidth * 0.6), (int) (baseHeight * 0.6), (int) (baseWidth * 1.6), (int) (baseHeight * 1.6));
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    lastMapStart = proc.getMusic().startTime;
    lastSectionCount = proc.getMusic().sections.size();
    if (proc.getTrack().hasMap()) autoSelectBiggestOpportunity();
    refreshAll();
    startTimerHz (30);
}

FINISHAudioProcessorEditor::~FINISHAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void FINISHAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));
}

namespace Layout
{
    const juce::Rectangle<int> screen { 36, 104, 836, 344 };
    const juce::Rectangle<int> column { 888, 104, 276, 344 };
    const juce::Rectangle<int> console { 36, 474, 1128, 168 };
    const juce::Rectangle<int> deck { 36, 664, 1128, 112 };
}

void FINISHAudioProcessorEditor::Content::resized()
{
    auto& e = editor;
    e.refButton.setBounds (572, 28, 140, 34);
    e.refClear.setBounds (716, 33, 24, 24);
    e.matchButton.setBounds (748, 28, 132, 34);
    e.bypassButton.setBounds (888, 28, 118, 34);
    e.trackMap.setBounds (Layout::screen);
    e.trackPanel.setBounds (Layout::column);
    e.finishButton.setBounds (Layout::column.getCentreX() - 60, Layout::column.getY() + 4, 120, 120);
    e.inspector.setBounds (Layout::console);
    e.deck.setBounds (Layout::deck);
}

void FINISHAudioProcessorEditor::Content::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.fillAll (void_);

    // Chassis: anodised faceplate, bevelled rim, neon trim.
    auto chassis = r.reduced (4.0f);
    g.saveState();
    juce::Path chassisPath;
    chassisPath.addRoundedRectangle (chassis, 18.0f);
    g.reduceClipRegion (chassisPath);
    g.drawImage (editor.faceplate, chassis, juce::RectanglePlacement::stretchToFit);
    g.restoreState();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), 0, chassis.getY(), juce::Colours::black.withAlpha (0.5f), 0, chassis.getBottom(), false));
    g.drawRoundedRectangle (chassis, 18.0f, 2.0f);
    g.setGradientFill (aurora (chassis.getX() + 60.0f, 0, chassis.getRight() - 60.0f, 0, 0.9f));
    g.fillRect (juce::Rectangle<float> (chassis.getX() + 60.0f, chassis.getBottom() - 3.0f, chassis.getWidth() - 120.0f, 1.5f));
    g.setGradientFill (aurora (chassis.getX() + 60.0f, 0, chassis.getRight() - 60.0f, 0, 0.12f));
    g.fillRect (juce::Rectangle<float> (chassis.getX() + 60.0f, chassis.getBottom() - 10.0f, chassis.getWidth() - 120.0f, 8.0f));

    for (auto p : { juce::Point<float> (20, 20), juce::Point<float> (r.getRight() - 20, 20),
                    juce::Point<float> (20, r.getBottom() - 20), juce::Point<float> (r.getRight() - 20, r.getBottom() - 20) })
        drawScrew (g, p, 6.0f, p.x * 0.01f + p.y * 0.02f);

    // Header: neon logo + model plate
    neonText (g, "FINISH", display (34.0f, 0.14f), { 44.0f, 20.0f, 260.0f, 44.0f }, violet, juce::Justification::centredLeft);
    const float ww = juce::GlyphArrangement::getStringWidth (display (34.0f, 0.14f), "FINISH");
    drawMoonMark (g, { 44.0f + ww + 20.0f, 41.0f }, 10.0f, 1.0f);
    engraved (g, u8 ("TRACK FINISHER  \xc2\xb7  MODEL FN-1"), { 46.0f, 66.0f, 300.0f, 12.0f }, 7.5f, juce::Justification::centredLeft);

    engraved (g, "INPUT", { 572.0f, 12.0f, 140.0f, 12.0f }, 6.5f);
    engraved (g, "COMPARE", { 748.0f, 12.0f, 258.0f, 12.0f }, 6.5f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawVerticalLine (1026, 22.0f, 70.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawVerticalLine (1027, 22.0f, 70.0f);
    drawLed (g, { 1048.0f, 45.0f }, 3.5f, mint, true);
    engraved (g, "POWER", { 1060.0f, 39.0f, 60.0f, 12.0f }, 7.0f, juce::Justification::centredLeft);
    engraved (g, "v" FINISH_VERSION_STRING, { 1100.0f, 39.0f, 64.0f, 12.0f }, 7.0f, juce::Justification::centredRight, dusk);

    // Status LCD strip
    auto strip = juce::Rectangle<float> (572.0f, 74.0f, 592.0f, 18.0f);
    drawScreen (g, strip, 3.0f, true);
    g.setColour (cyan.withAlpha (0.85f));
    g.setFont (mono (9.5f));
    const auto msg = editor.proc.getStatusMessage();
    g.drawText (msg.isEmpty() ? juce::String ("READY. PLACE FINISH LAST ON THE MASTER.") : msg.toUpperCase(), strip.reduced (8.0f, 0), juce::Justification::centredLeft, true);

    // Section engravings + grooves
    auto groove = [&] (float y)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (30.0f, y, r.getWidth() - 60.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (30.0f, y + 1.0f, r.getWidth() - 60.0f, 1.0f);
    };
    groove (98.0f);
    groove (460.0f);
    groove (652.0f);
    engraved (g, "SECTION CONSOLE", { 36.0f, 462.0f, 300.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
    engraved (g, "MASTER CONTROLS", { 36.0f, 654.0f, 300.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
    engraved (g, "METERS", { 824.0f, 654.0f, 300.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
}

// ======================================================================================
void FINISHAudioProcessorEditor::select (TrackEngine::Selection s)
{
    trackMap.setSelection (s);
    inspector.setSelection (s);
}

void FINISHAudioProcessorEditor::autoSelectBiggestOpportunity()
{
    const auto& engine = proc.getTrack();
    float best = 0.24f;
    auto sel = TrackEngine::Selection::track();
    for (size_t i = 0; i < engine.getVerdicts().size(); ++i)
        if (engine.getVerdicts()[i].potential > best) { best = engine.getVerdicts()[i].potential; sel = TrackEngine::Selection::section ((int) i); }
    for (const auto& t : engine.getTransitions())
        if (! t.works && t.severity * 0.9f > best) { best = t.severity * 0.9f; sel = TrackEngine::Selection::transition (t.index); }
    select (sel);
}

void FINISHAudioProcessorEditor::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<FINISHAudioProcessorEditor> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto file = fc.getResult();
                              if (file.existsAsFile()) safe->proc.loadReference (file);
                              safe->refreshAll();
                          });
}

void FINISHAudioProcessorEditor::updateFinishButton()
{
    using S = FINISHAudioProcessor::AnalysisState;
    const auto st = proc.getAnalysisState();
    finishButton.setMode (st == S::Listening ? ArcadeButton::Mode::Listening : (st == S::Ready ? ArcadeButton::Mode::Ready : ArcadeButton::Mode::Idle),
                          proc.getListeningSeconds());
}

void FINISHAudioProcessorEditor::refreshAll()
{
    lastVersion = proc.getResultVersion();
    updateFinishButton();

    const auto& m = proc.getMusic();
    if (proc.getTrack().hasMap() && (std::abs (m.startTime - lastMapStart) > 1.0e-6 || m.sections.size() != lastSectionCount || wasListening)
        && proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Ready)
    {
        lastMapStart = m.startTime;
        lastSectionCount = m.sections.size();
        wasListening = false;
        autoSelectBiggestOpportunity();
    }
    if (proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Listening) wasListening = true;

    const auto& ref = proc.getReference();
    refButton.setButtonText (proc.isReferenceLoading() ? "LOADING..." : (ref.valid ? "REF " + ref.name.toUpperCase().substring (0, 10) : "REFERENCE"));
    refButton.setToggleState (ref.valid, juce::dontSendNotification);
    refClear.setVisible (ref.valid);

    inspector.refresh();
    trackPanel.refresh();
    trackMap.repaint();
    deck.repaint();
    content.repaint (570, 72, 600, 22);
}

void FINISHAudioProcessorEditor::timerCallback()
{
    trackMap.repaint();
    deck.repaintLive();
    if (proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Listening || finishButton.isOver())
        updateFinishButton();
    if (proc.getResultVersion() != lastVersion || proc.isReferenceLoading())
        refreshAll();
}
