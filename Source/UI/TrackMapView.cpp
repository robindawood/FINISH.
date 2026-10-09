#include "TrackMapView.h"
#include "../Music/SectionDetector.h"

using namespace Night;

TrackMapView::TrackMapView (FINISHAudioProcessor& p) : proc (p)
{
    stars = makeStars (90, 0x5eed);
    wholeTrack.getProperties().set ("style", "chip");
    wholeTrack.setClickingTogglesState (false);
    wholeTrack.setTooltip ("Inspect the whole track: global diagnosis and PUNCH / WIDE / MASSIVE.");
    wholeTrack.onClick = [this] { if (onSelect) onSelect (Selection::track()); };
    addAndMakeVisible (wholeTrack);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TrackMapView::resized()
{
    wholeTrack.setBounds (getWidth() - 26 - 104, 16, 104, 22);
}

juce::Rectangle<float> TrackMapView::glassArea() const { return getLocalBounds().toFloat().reduced (8.0f); }

// ---- Geometry -----------------------------------------------------------------------------
juce::Rectangle<float> TrackMapView::plot() const      { return getLocalBounds().toFloat().reduced (30.0f, 0).withTrimmedTop (48.0f).withTrimmedBottom (30.0f); }
juce::Rectangle<float> TrackMapView::labelRow() const  { return plot().withHeight (22.0f); }
juce::Rectangle<float> TrackMapView::curveArea() const { auto p = plot(); return p.withTrimmedTop (24.0f).withTrimmedBottom (36.0f); }
juce::Rectangle<float> TrackMapView::rail() const      { auto p = plot(); return p.withTop (p.getBottom() - 30.0f).withHeight (26.0f); }

std::pair<double, double> TrackMapView::range() const
{
    if (proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Listening)
    {
        const auto& frames = proc.getSession().getFrames();
        double t0 = 1.0e9, t1 = -1.0e9;
        for (auto& f : frames) if (f.hostTime >= 0) { t0 = std::min (t0, f.hostTime); t1 = std::max (t1, f.hostTime); }
        if (t1 < t0) { t0 = 0; t1 = proc.getListeningSeconds(); }
        return { t0, std::max (t0 + 60.0, t1 + 8.0) };
    }
    const auto& m = proc.getMusic();
    return { m.startTime, std::max (m.startTime + 1.0, m.endTime()) };
}

float TrackMapView::xFor (double t) const
{
    const auto [a, b] = range();
    const auto p = plot();
    return p.getX() + (float) ((t - a) / (b - a)) * p.getWidth();
}

double TrackMapView::timeFor (float x) const
{
    const auto [a, b] = range();
    const auto p = plot();
    return a + (double) ((x - p.getX()) / p.getWidth()) * (b - a);
}

int TrackMapView::boundaryNear (juce::Point<float> pt) const
{
    const auto& S = proc.getMusic().sections;
    if (! plot().withTrimmedBottom (36.0f).contains (pt)) return -1;
    for (size_t i = 0; i + 1 < S.size(); ++i)
        if (std::abs (xFor (S[i].endTime) - pt.x) < 5.0f) return (int) i;
    return -1;
}

int TrackMapView::nodeAt (juce::Point<float> pt) const
{
    const auto& S = proc.getMusic().sections;
    if (! rail().expanded (0, 6).contains (pt)) return -1;
    for (size_t i = 0; i + 1 < S.size(); ++i)
        if (std::abs (xFor (S[i].endTime) - pt.x) < 10.0f) return (int) i;
    return -1;
}

int TrackMapView::sectionAt (juce::Point<float> pt) const
{
    if (! plot().contains (pt)) return -1;
    return proc.getMusic().sectionAt (timeFor (pt.x));
}

// ---- Mouse --------------------------------------------------------------------------------
void TrackMapView::mouseMove (const juce::MouseEvent& e)
{
    if (proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Listening || ! proc.getTrack().hasMap()) return;
    const auto pt = e.position;
    hoverNode = nodeAt (pt);
    hoverBoundary = hoverNode < 0 ? boundaryNear (pt) : -1;
    hoverSection = (hoverNode < 0 && hoverBoundary < 0) ? sectionAt (pt) : -1;
    setMouseCursor (hoverBoundary >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void TrackMapView::mouseExit (const juce::MouseEvent&)
{
    hoverNode = hoverBoundary = hoverSection = -1;
    repaint();
}

void TrackMapView::mouseDown (const juce::MouseEvent& e)
{
    if (proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Listening || ! proc.getTrack().hasMap()) return;
    const auto pt = e.position;
    if (const int n = nodeAt (pt); n >= 0)
    {
        if (onSelect) onSelect (Selection::transition (n));
        return;
    }
    if (const int b = boundaryNear (pt); b >= 0 && ! e.mods.isPopupMenu())
    {
        dragBoundary = b;
        dragTime = proc.getMusic().sections[(size_t) b].endTime;
        return;
    }
    if (const int s = sectionAt (pt); s >= 0)
    {
        if (onSelect) onSelect (Selection::section (s));
        if (e.mods.isPopupMenu()) showSectionMenu (s);
        else if (labelRow().contains (pt) && proc.getMusic().sections[(size_t) s].isUncertain())
            showSectionMenu (s);
    }
}

void TrackMapView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBoundary < 0) return;
    dragTime = timeFor (e.position.x);
    repaint();
}

void TrackMapView::mouseUp (const juce::MouseEvent&)
{
    if (dragBoundary < 0) return;
    const int b = dragBoundary;
    const double t = dragTime;
    dragBoundary = -1;
    proc.editTrack ([b, t] (TrackEngine& engine) { engine.moveBoundary (b, t); });
    if (onSelect) onSelect (Selection::transition (b));
}

void TrackMapView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! plot().contains (e.position) && onSelect) onSelect (Selection::track());
}

void TrackMapView::showSectionMenu (int s)
{
    const auto& sec = proc.getMusic().sections[(size_t) s];
    juce::PopupMenu m;
    m.addSectionHeader (sec.label + "  " + mmss (sec.startTime) + " - " + mmss (sec.endTime));
    if (sec.isUncertain())
    {
        m.addItem (100, "Confirm " + sectionTypeName (sec.type));
        m.addItem (100 + (int) sec.alternative, "It's a " + sectionTypeName (sec.alternative));
        m.addSeparator();
    }
    m.addSectionHeader ("This section is a...");
    for (auto t : { SectionType::Intro, SectionType::Verse, SectionType::Chorus, SectionType::Build,
                    SectionType::Drop, SectionType::Break, SectionType::Bridge, SectionType::Outro })
        m.addItem ((int) t, sectionTypeName (t), true, t == sec.type);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                       .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> ((int) xFor (sec.startTime), (int) labelRow().getBottom(), 1, 1))),
                     [this, s] (int r)
                     {
                         if (r == 0) return;
                         if (r == 100) proc.editTrack ([s] (TrackEngine& e) { e.confirmSection (s); });
                         else
                         {
                             const auto type = (SectionType) (r > 100 ? r - 100 : r);
                             proc.editTrack ([s, type] (TrackEngine& e) { e.setSectionType (s, type); });
                         }
                         if (onSelect) onSelect (Selection::section (s));
                     });
}

// ---- Paint --------------------------------------------------------------------------------
void TrackMapView::paint (juce::Graphics& g)
{
    const auto glassR = glassArea();
    drawScreen (g, glassR, 14.0f);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (glassR, 14.0f);
    g.reduceClipRegion (clip);

    // Synthwave floor glowing under the data.
    drawSynthGrid (g, glassR.withTrimmedTop (glassR.getHeight() * 0.55f), glassR.getY() + glassR.getHeight() * 0.55f, indigo, 0.45f);
    drawStars (g, stars, glassR.withHeight (glassR.getHeight() * 0.5f), 0.4f);

    neonText (g, "TRACK MAP", mono (11.0f, 0.12f), { 30.0f, 18.0f, 140.0f, 18.0f }, cyan, juce::Justification::centredLeft, 0.7f);

    const auto state = proc.getAnalysisState();
    wholeTrack.setVisible (state != FINISHAudioProcessor::AnalysisState::Listening && proc.getTrack().hasMap());
    wholeTrack.setToggleState (selection.kind == Selection::Kind::Track, juce::dontSendNotification);

    if (state == FINISHAudioProcessor::AnalysisState::Listening) paintListening (g);
    else if (proc.getTrack().hasMap()) paintMap (g);
    else paintIdle (g);

    g.restoreState();
    drawScreenGlass (g, glassR, 14.0f, 1.0f);
}

void TrackMapView::paintCurve (juce::Graphics& g, const std::vector<float>& curve, double t0, double step, bool live)
{
    if (curve.size() < 2) return;
    const auto area = curveArea();
    float maxV = -100.0f;
    for (auto v : curve) maxV = std::max (maxV, v);
    float minV = maxV;
    for (auto v : curve) if (v > maxV - 28.0f) minV = std::min (minV, v);
    minV -= 2.0f; maxV += 1.5f;

    const float x0 = xFor (t0), x1 = xFor (t0 + step * (double) curve.size());
    const int columns = std::max (2, (int) (x1 - x0));
    juce::Path p;
    for (int c = 0; c <= columns; ++c)
    {
        const size_t i0 = (size_t) ((double) c / (columns + 1) * (double) curve.size());
        const size_t i1 = std::max (i0 + 1, (size_t) ((double) (c + 1) / (columns + 1) * (double) curve.size()));
        float v = -100.0f;
        for (size_t i = i0; i < std::min (i1, curve.size()); ++i) v = std::max (v, curve[i]);
        const float norm = juce::jlimit (0.0f, 1.0f, (v - minV) / (maxV - minV));
        const float x = x0 + (x1 - x0) * (float) c / (float) columns;
        const float y = area.getBottom() - norm * area.getHeight() * 0.92f;
        if (c == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::Path fill (p);
    fill.lineTo (x1, area.getBottom());
    fill.lineTo (x0, area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (violet.withAlpha (0.30f), 0, area.getY(), indigo.withAlpha (0.0f), 0, area.getBottom(), false));
    g.fillPath (fill);
    glowStrokeGradient (g, p, aurora (x0, 0, std::max (x0 + 1.0f, x1), 0), live ? 1.6f : 1.8f);

    g.setColour (dusk);
    g.setFont (mono (9.0f));
    g.drawText (juce::String (juce::roundToInt (maxV - 1.5f)) + " LUFS", area.withHeight (12.0f).translated (0, -2), juce::Justification::topRight);
}

void TrackMapView::paintAxis (juce::Graphics& g)
{
    const auto [a, b] = range();
    const double span = b - a;
    const double step = span > 480 ? 60.0 : (span > 200 ? 30.0 : (span > 80 ? 15.0 : 5.0));
    const auto p = plot();
    g.setFont (mono (9.0f));
    for (double t = std::ceil (a / step) * step; t <= b; t += step)
    {
        const float x = xFor (t);
        g.setColour (line);
        g.drawVerticalLine ((int) x, p.getBottom() + 2.0f, p.getBottom() + 6.0f);
        g.setColour (dusk);
        g.drawText (mmss (t), juce::Rectangle<float> (x - 20.0f, p.getBottom() + 5.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
}

void TrackMapView::paintPlayhead (juce::Graphics& g)
{
    if (! proc.hasHostTime()) return;
    const double t = proc.getHostTime();
    const auto [a, b] = range();
    if (t < a || t > b) return;
    const float x = xFor (t);
    const auto p = plot();
    juce::Path line;
    line.startNewSubPath (x, p.getY() + 2.0f);
    line.lineTo (x, p.getBottom() - 4.0f);
    glowStroke (g, line, moon.withAlpha (proc.isHostPlaying() ? 0.9f : 0.45f), 1.0f, 0.8f);
    juce::Path tri;
    tri.addTriangle (x - 4.0f, p.getY() - 1.0f, x + 4.0f, p.getY() - 1.0f, x, p.getY() + 4.0f);
    g.setColour (moon);
    g.fillPath (tri);
}

void TrackMapView::paintIdle (juce::Graphics& g)
{
    const auto r = glassArea();
    const float horizon = r.getY() + r.getHeight() * 0.55f;
    drawSun (g, { r.getRight() - 170.0f, horizon - 4.0f }, 62.0f, 0.95f);
    // re-draw the horizon over the sun base
    g.setColour (glass);
    g.fillRect (juce::Rectangle<float> (r.getX(), horizon, r.getWidth(), 6.0f));
    drawSynthGrid (g, r.withTop (horizon), horizon, violet, 0.8f);

    neonText (g, "PRESS FINISH, PLAY YOUR SONG.", display (19.0f, 0.06f), { 46.0f, 66.0f, 640.0f, 32.0f }, violet, juce::Justification::centredLeft);
    g.setColour (mist);
    g.setFont (text (13.0f));
    g.drawText ("FINISH maps every section and transition,", juce::Rectangle<float> (48.0f, 104.0f, 600.0f, 18.0f), juce::Justification::centredLeft);
    g.drawText ("then proposes three directions for each. Without imposing one.", juce::Rectangle<float> (48.0f, 122.0f, 600.0f, 18.0f), juce::Justification::centredLeft);
    g.setColour (cyan.withAlpha (0.75f));
    g.setFont (mono (9.5f, 0.1f));
    g.drawText (u8 ("WHOLE TRACK \xc2\xb7 SECTION DNA \xc2\xb7 TRANSITIONS \xc2\xb7 A/B/C VARIANTS \xc2\xb7 COHERENCE"),
                juce::Rectangle<float> (48.0f, 150.0f, 640.0f, 14.0f), juce::Justification::centredLeft);
}

void TrackMapView::paintListening (juce::Graphics& g)
{
    const auto& frames = proc.getSession().getFrames();
    const double dur = proc.getListeningSeconds();

    // Live curve on song time (or listening time if the host gives no position).
    std::vector<AnalysisFrame> timed;
    double start = 0;
    auto grid = TrackEngine::buildGrid (frames, start);
    paintCurve (g, SectionDetector::contour (grid), start, MusicState::frameSec, true);
    paintAxis (g);
    paintPlayhead (g);

    const auto r = getLocalBounds().toFloat();
    const bool blink = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
    drawLed (g, { r.getRight() - 150.0f, 27.0f }, 3.5f, rose, blink);
    neonText (g, "REC  " + mmss (dur), mono (11.0f, 0.1f), { r.getRight() - 138.0f, 18.0f, 120.0f, 18.0f }, rose, juce::Justification::centredLeft, 0.8f);
    g.setColour (dusk);
    g.setFont (mono (10.0f));
    g.drawText (proc.hasHostTime() ? "MAPPING ON YOUR SONG TIMELINE" : "NO SONG POSITION FROM THE HOST: MAPPING BY LISTENING TIME",
                juce::Rectangle<float> (160.0f, 18.0f, 500.0f, 18.0f), juce::Justification::centredLeft);
    if (! proc.isHostPlaying() && dur < 1.0)
    {
        g.setColour (moon);
        g.setFont (display (15.0f, 0.04f));
        g.drawText ("Press play in your DAW.", curveArea(), juce::Justification::centred);
        g.setColour (mist);
        g.setFont (text (12.5f));
        g.drawText ("Play the whole song if you can. Stop playback to get the map.", curveArea().translated (0, 26.0f), juce::Justification::centred);
    }
}

void TrackMapView::paintMap (juce::Graphics& g)
{
    const auto& engine = proc.getTrack();
    const auto& m = engine.getMusic();
    const auto& S = m.sections;
    const auto p = plot(), lr = labelRow(), ca = curveArea(), rl = rail();

    g.setColour (dusk);
    g.setFont (mono (10.0f));
    g.drawText (juce::String ((int) S.size()) + " SECTIONS  " + u8 ("\xc2\xb7") + "  " + juce::String ((int) engine.getTransitions().size())
                + " TRANSITIONS  " + u8 ("\xc2\xb7") + "  " + mmss (m.durationSec) + "  " + u8 ("\xc2\xb7") + "  CONTRAST "
                + juce::String (m.contrastDb, 1) + " dB",
                juce::Rectangle<float> (160.0f, 18.0f, 560.0f, 18.0f), juce::Justification::centredLeft);

    // Sections
    for (size_t i = 0; i < S.size(); ++i)
    {
        const auto& s = S[i];
        const float x0 = xFor (s.startTime), x1 = xFor (s.endTime);
        const auto band = juce::Rectangle<float> (x0, ca.getY(), x1 - x0, ca.getHeight());
        const bool sel = selection.kind == Selection::Kind::Section && selection.index == (int) i;
        const bool hov = hoverSection == (int) i;
        if (isPeakType (s.type))
        {
            g.setGradientFill (juce::ColourGradient (violet.withAlpha (0.0f), 0, band.getY(), violet.withAlpha (0.16f), 0, band.getBottom(), false));
            g.fillRect (band);
        }
        if (sel || hov)
        {
            g.setColour (moon.withAlpha (sel ? 0.06f : 0.03f));
            g.fillRect (band.withTop (lr.getY()));
        }
        if (sel)
        {
            g.setGradientFill (aurora (x0, 0, x1, 0));
            g.fillRect (juce::Rectangle<float> (x0 + 1.0f, lr.getBottom() - 1.0f, x1 - x0 - 2.0f, 2.0f));
        }

        // Label + confidence
        const bool peak = isPeakType (s.type);
        auto labelArea = juce::Rectangle<float> (x0 + 8.0f, lr.getY(), std::max (0.0f, x1 - x0 - 12.0f), lr.getHeight());
        g.setColour (sel ? moon : (peak ? moon.withAlpha (0.9f) : mist));
        g.setFont (label (9.5f));
        const float lw = juce::GlyphArrangement::getStringWidth (label (9.5f), s.label);
        g.drawText (s.label, labelArea, juce::Justification::centredLeft, true);
        const float cx = labelArea.getX() + lw + 8.0f;
        if (cx + 40.0f < x1)
        {
            if (s.isUncertain())
            {
                auto pill = juce::Rectangle<float> (cx, lr.getCentreY() - 7.0f, 18.0f + juce::GlyphArrangement::getStringWidth (mono (9.0f), sectionTypeName (s.alternative)) + 20.0f, 14.0f);
                if (pill.getRight() > x1 - 4.0f) pill.setWidth (16.0f);
                g.setColour (amber.withAlpha (0.16f));
                g.fillRoundedRectangle (pill, 7.0f);
                g.setColour (amber);
                g.setFont (mono (9.0f));
                g.drawText (pill.getWidth() > 16.0f ? "? or " + sectionTypeName (s.alternative) : juce::String ("?"), pill, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (dusk);
                g.setFont (mono (9.0f));
                g.drawText (juce::String (juce::roundToInt (s.confidence * 100.0f)) + "%", juce::Rectangle<float> (cx, lr.getY(), 40.0f, lr.getHeight()), juce::Justification::centredLeft);
            }
        }

        // Boundary
        if (i > 0)
        {
            const bool hot = hoverBoundary == (int) i - 1 || dragBoundary == (int) i - 1;
            g.setColour (hot ? cyan : lineHi.withAlpha (0.8f));
            g.drawVerticalLine ((int) x0, lr.getY() + 2.0f, rl.getCentreY());
        }
    }

    paintCurve (g, m.energyCurve, m.startTime, MusicState::frameSec, false);

    // Rail: applied edits, preview, transition nodes
    g.setColour (line);
    g.fillRoundedRectangle (juce::Rectangle<float> (p.getX(), rl.getCentreY() - 1.0f, p.getWidth(), 2.0f), 1.0f);
    for (const auto& e : engine.getEdits())
    {
        const auto [a, b] = engine.rangeOf (e);
        const float x0 = xFor (a), x1 = std::max (xFor (b), x0 + 3.0f);
        const auto bar = juce::Rectangle<float> (x0, rl.getCentreY() - 3.5f, x1 - x0, 7.0f);
        if (e.keepOriginal)
        {
            g.setColour (mist.withAlpha (0.35f));
            g.fillRoundedRectangle (bar, 3.5f);
            g.setColour (moon.withAlpha (0.8f));
            g.setFont (mono (8.5f));
            g.drawText ("ORIGINAL", bar.withY (bar.getY() - 12.0f).withHeight (11.0f), juce::Justification::centredLeft, true);
            continue;
        }
        g.setGradientFill (aurora (x0, 0, x1, 0, 0.25f));
        g.fillRoundedRectangle (bar.expanded (0, 3.0f), 6.0f);
        g.setGradientFill (aurora (x0, 0, x1, 0));
        g.fillRoundedRectangle (bar, 3.5f);
        g.setColour (moon);
        g.setFont (mono (8.5f));
        g.drawText (juce::String::charToString ((juce::juce_wchar) e.variant.letter) + " " + e.variant.name.toUpperCase(),
                    juce::Rectangle<float> (x0, bar.getY() - 13.0f, std::max (60.0f, x1 - x0), 11.0f), juce::Justification::centredLeft, true);
    }
    if (const auto pv = engine.getPreview())
    {
        const auto [sel, v] = *pv;
        if (const auto* vs = engine.variantsFor (sel); vs != nullptr && v >= 0 && v < (int) vs->size())
        {
            const auto [a, b] = (*vs)[(size_t) v].target.resolve (S);
            const float x0 = xFor (a), x1 = std::max (xFor (b), x0 + 3.0f);
            const float pulse = 0.55f + 0.45f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f);
            juce::Path outline;
            outline.addRoundedRectangle (juce::Rectangle<float> (x0, rl.getCentreY() - 6.0f, x1 - x0, 12.0f), 6.0f);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (cyan.withAlpha (pulse));
            g.fillPath (dashed);
            // highlight the previewed region on the curve
            g.setColour (cyan.withAlpha (0.05f + 0.04f * pulse));
            g.fillRect (juce::Rectangle<float> (x0, ca.getY(), x1 - x0, ca.getHeight()));
        }
    }
    const auto& T = engine.getTransitions();
    for (size_t i = 0; i < T.size() && i + 1 < S.size(); ++i)
    {
        const float x = xFor (S[i].endTime);
        const auto c = juce::Point<float> (x, rl.getCentreY());
        const bool sel = selection.kind == Selection::Kind::Transition && selection.index == (int) i;
        const bool hov = hoverNode == (int) i;
        const auto col = T[i].works ? mint : amber;
        const float rad = (sel || hov) ? 6.5f : 5.0f;
        juce::Path diamond;
        diamond.addPolygon (c, 4, rad, 0.0f);
        if (! T[i].works)
        {
            g.setColour (col.withAlpha (0.22f));
            g.fillEllipse (juce::Rectangle<float> (rad * 4.0f, rad * 4.0f).withCentre (c));
        }
        g.setColour (bg0);
        g.fillPath (diamond);
        g.setColour (col);
        if (T[i].works) g.strokePath (diamond, juce::PathStrokeType (1.4f));
        else g.fillPath (diamond);
        if (sel)
        {
            g.setColour (moon);
            g.drawEllipse (juce::Rectangle<float> (rad * 3.2f, rad * 3.2f).withCentre (c), 1.2f);
        }
    }

    // Boundary being dragged
    if (dragBoundary >= 0)
    {
        const float x = xFor (dragTime);
        juce::Path l;
        l.startNewSubPath (x, lr.getY());
        l.lineTo (x, rl.getCentreY());
        glowStroke (g, l, cyan, 1.2f);
        g.setColour (cyan);
        g.setFont (mono (9.5f));
        g.drawText (mmss (dragTime), juce::Rectangle<float> (x + 4.0f, ca.getY(), 50.0f, 12.0f), juce::Justification::centredLeft);
    }

    paintAxis (g);
    paintPlayhead (g);
}
