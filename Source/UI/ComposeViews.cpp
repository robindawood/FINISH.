#include "ComposeViews.h"

using namespace Night;

namespace
{
    constexpr float laneLabelW = 70.0f, headerH = 30.0f, topBarH = 26.0f, rulerH = 14.0f;

    void drawPadlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool closed)
    {
        auto body = r.withTrimmedTop (r.getHeight() * 0.45f);
        juce::Path shackle;
        const float sx = r.getCentreX(), sw = r.getWidth() * 0.32f, top = r.getY() + 1.0f, base = body.getY();
        shackle.startNewSubPath (sx - sw, base);
        shackle.lineTo (sx - sw, top + sw);
        shackle.addCentredArc (sx, top + sw, sw, sw, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
        shackle.lineTo (sx + sw, closed ? base : base - 3.0f);
        g.setColour (c);
        g.strokePath (shackle, juce::PathStrokeType (1.4f));
        g.fillRoundedRectangle (body, 1.5f);
    }

    void drawDice (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        g.drawRoundedRectangle (r.reduced (0.7f), 2.5f, 1.3f);
        const float d = r.getWidth() * 0.13f;
        for (auto p : { juce::Point<float> (0.3f, 0.3f), juce::Point<float> (0.7f, 0.7f), juce::Point<float> (0.5f, 0.5f), juce::Point<float> (0.7f, 0.3f), juce::Point<float> (0.3f, 0.7f) })
            g.fillEllipse (juce::Rectangle<float> (d * 2, d * 2).withCentre ({ r.getX() + p.x * r.getWidth(), r.getY() + p.y * r.getHeight() }));
    }

    juce::String barsRange (const GenSection& s)
    {
        return juce::String (s.startBar + 1) + utf8 ("–") + juce::String (s.startBar + s.bars);
    }
}

// ======================================================================================
ArrangementView::ArrangementView (FINISHAudioProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ArrangementView::gridArea() const
{
    return getLocalBounds().toFloat().reduced (16.0f, 12.0f).withTrimmedTop (topBarH).withTrimmedLeft (laneLabelW).withTrimmedBottom (rulerH);
}

juce::Rectangle<float> ArrangementView::sectionColumn (int i) const
{
    const auto& arr = proc.getComposer().getArrangement();
    const auto g = gridArea();
    const float total = (float) std::max (1, arr.totalBars());
    const auto& s = arr.sections[(size_t) i];
    const float x0 = g.getX() + g.getWidth() * (float) s.startBar / total;
    const float x1 = g.getX() + g.getWidth() * (float) (s.startBar + s.bars) / total;
    return { x0, g.getY(), x1 - x0, g.getHeight() };
}

juce::Rectangle<float> ArrangementView::cellRect (int section, int lane) const
{
    auto col = sectionColumn (section).withTrimmedTop (headerH);
    const float lh = col.getHeight() / (float) numLanes;
    return juce::Rectangle<float> (col.getX(), col.getY() + lh * (float) lane, col.getWidth(), lh).reduced (1.5f, 2.0f);
}

ArrangementView::Hit ArrangementView::hitAt (juce::Point<float> p) const
{
    Hit h;
    if (! proc.getComposer().hasArrangement()) return h;
    const auto g = gridArea();
    const auto& arr = proc.getComposer().getArrangement();
    if (p.x < g.getX() && p.x > g.getX() - laneLabelW && p.y > g.getY() + headerH && p.y < g.getBottom())
    {
        h.laneLabel = true;
        h.lane = juce::jlimit (0, numLanes - 1, (int) ((p.y - g.getY() - headerH) / ((g.getHeight() - headerH) / numLanes)));
        return h;
    }
    for (int i = 0; i < (int) arr.sections.size(); ++i)
    {
        const auto col = sectionColumn (i);
        if (! col.contains (p)) continue;
        h.section = i;
        if (p.y < col.getY() + headerH)
        {
            h.header = true;
            const auto icons = juce::Rectangle<float> (col.getRight() - 32.0f, col.getY() + headerH - 17.0f, 30.0f, 12.0f);
            if (col.getWidth() > 56.0f && icons.contains (p))
            {
                h.lockIcon = p.x < icons.getCentreX();
                h.diceIcon = ! h.lockIcon;
            }
        }
        else
            h.lane = juce::jlimit (0, numLanes - 1, (int) ((p.y - col.getY() - headerH) / ((col.getHeight() - headerH) / numLanes)));
        return h;
    }
    return h;
}

void ArrangementView::drawClip (juce::Graphics& g, const GenSection& s, Lane lane, juce::Rectangle<float> r, bool sel) const
{
    const auto col = laneColour (lane);
    const auto& clip = s.isReference ? s.layers[(size_t) lane] : s.clips[(size_t) lane];
    const bool fxLane = lane == Lane::Fx;
    const bool empty = fxLane ? s.clips[(size_t) lane].fx.empty() : clip.notes.empty();
    if (s.isReference && ! fxLane)
    {
        // The user's own part: hatched, never replaced.
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (mist.withAlpha (0.06f));
        for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 7.0f)
            g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);
        g.restoreState();
        if (empty) return;
    }
    if (empty) return;
    const bool muted = proc.isPreviewLaneMuted (lane);
    const float a = muted ? 0.35f : 1.0f;
    g.setColour (col.withAlpha ((sel ? 0.24f : 0.15f) * a));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (col.withAlpha ((sel ? 0.8f : 0.45f) * a));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, 2.0f);   // DAW-style clip header strip

    auto body = r.reduced (2.5f, 4.0f).withTrimmedTop (1.0f);
    const double len = s.lengthBeats();
    if (fxLane)
    {
        for (auto& f : s.clips[(size_t) lane].fx)
        {
            const float x0 = body.getX() + body.getWidth() * (float) (f.beat / len);
            const float x1 = body.getX() + body.getWidth() * (float) std::min (1.0, (f.beat + f.length) / len);
            juce::Path p;
            switch (f.kind)
            {
                case FxKind::Riser: case FxKind::Sweep: case FxKind::ReverseCymbal:
                    p.startNewSubPath (x0, body.getBottom()); p.quadraticTo (x1 - (x1 - x0) * 0.2f, body.getBottom(), x1, body.getY()); p.lineTo (x1, body.getBottom()); p.closeSubPath(); break;
                case FxKind::Downlifter:
                    p.startNewSubPath (x0, body.getY()); p.quadraticTo (x0 + (x1 - x0) * 0.2f, body.getBottom(), x1, body.getBottom()); p.lineTo (x0, body.getBottom()); p.closeSubPath(); break;
                case FxKind::Impact:
                    p.startNewSubPath (x0, body.getBottom()); p.lineTo (x0, body.getY()); p.quadraticTo (x0 + 6.0f, body.getBottom(), std::min (x1, x0 + 30.0f), body.getBottom()); p.closeSubPath(); break;
                case FxKind::None: break;
            }
            g.setColour (col.withAlpha (0.55f * a));
            g.fillPath (p);
        }
        return;
    }
    int lo = 127, hi = 0;
    for (auto& n : clip.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    if (hi - lo < 6) { lo -= 3; hi += 3; }
    g.setColour (col.withAlpha (0.9f * a));
    const float noteH = std::max (1.2f, std::min (3.0f, body.getHeight() / (float) (hi - lo + 1)));
    for (auto& n : clip.notes)
    {
        const float x = body.getX() + body.getWidth() * (float) (n.beat / len);
        const float w = std::max (1.0f, body.getWidth() * (float) (n.length / len));
        const float y = body.getBottom() - noteH - (body.getHeight() - noteH) * (float) (n.pitch - lo) / (float) std::max (1, hi - lo);
        g.fillRect (x, y, w, noteH);
    }
}

void ArrangementView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r.reduced (2.0f), 14.0f);
    const auto& comp = proc.getComposer();
    const auto inner = r.reduced (16.0f, 12.0f);

    if (! comp.hasArrangement())
    {
        drawSynthGrid (g, r.reduced (2.0f), r.getCentreY() + 40.0f, indigo, 0.5f);
        drawSun (g, { r.getCentreX(), r.getCentreY() + 40.0f }, 60.0f, 0.6f);
        const bool busy = proc.isComposing() || proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Listening;
        neonText (g, busy ? utf8 ("J'ÉCOUTE…") : juce::String ("NO ARRANGEMENT YET"), display (20.0f, 0.12f),
                  r.withHeight (r.getHeight() * 0.42f), busy ? cyan : violet, juce::Justification::centredBottom);
        g.setColour (mist);
        g.setFont (mono (11.0f));
        g.drawText (busy ? utf8 ("TEMPO · TONALITÉ · GROOVE · BASSE · MÉLODIE")
                         : utf8 ("1. JOUE TON DROP, TA BOUCLE OU TA MÉLODIE    2. FINISH MY TRACK"),
                    r.withTrimmedTop (r.getHeight() * 0.44f).withHeight (20.0f), juce::Justification::centredTop);
        drawScreenGlass (g, r.reduced (2.0f), 14.0f, 0.6f);
        return;
    }

    const auto& arr = comp.getArrangement();
    const auto& dna = comp.getDNA();

    // Top bar
    {
        auto bar = inner.withHeight (topBarH - 6.0f);
        g.setFont (mono (9.5f));
        g.setColour (cyan.withAlpha (0.9f));
        g.drawText (juce::String ("ARRANGEMENT  ") + flavourName (arr.flavour), bar, juce::Justification::centredLeft);
        g.setColour (dusk);
        g.drawText (juce::String (arr.totalBars()) + utf8 (" MES.  ·  ") + dna.keyName() + utf8 ("  ·  ") + juce::String (dna.bpm, 1) + " BPM",
                    bar.withTrimmedLeft (150.0f), juce::Justification::centredLeft);
        g.drawText (proc.isPreviewing() ? utf8 ("▶ PREVIEW") : utf8 ("CLIC = ÉCOUTER   ·   GLISSER = DAW"), bar, juce::Justification::centredRight);
    }

    const auto grid = gridArea();
    // Lane labels
    {
        const float lh = (grid.getHeight() - headerH) / (float) numLanes;
        for (int l = 0; l < numLanes; ++l)
        {
            auto lr = juce::Rectangle<float> (grid.getX() - laneLabelW, grid.getY() + headerH + lh * (float) l, laneLabelW - 6.0f, lh);
            const bool muted = proc.isPreviewLaneMuted ((Lane) l);
            drawLed (g, { lr.getX() + 6.0f, lr.getCentreY() }, 2.4f, laneColour ((Lane) l), ! muted);
            g.setFont (label (7.5f));
            g.setColour ((hover.laneLabel && hover.lane == l) ? moon : (muted ? dusk : mist));
            g.drawText (laneName ((Lane) l), lr.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
            g.setColour (line.withAlpha (0.5f));
            g.drawHorizontalLine ((int) (lr.getBottom()), grid.getX(), grid.getRight());
        }
    }

    // Sections
    for (int i = 0; i < (int) arr.sections.size(); ++i)
    {
        const auto& s = arr.sections[(size_t) i];
        const auto col = sectionColumn (i);
        const bool sel = i == selected;
        const bool playing = proc.isPreviewing() && proc.getPreviewSection() == i;
        if (sel)
        {
            g.setColour (indigo.withAlpha (0.08f));
            g.fillRect (col);
        }
        // header
        auto head = col.withHeight (headerH - 4.0f).reduced (1.5f, 0);
        g.setColour (sel ? panelHi : panel.withAlpha (0.8f));
        g.fillRoundedRectangle (head, 3.0f);
        if (sel || playing)
        {
            g.setGradientFill (aurora (head.getX(), 0, head.getRight(), 0, 1.0f));
            g.fillRect (head.getX() + 2.0f, head.getBottom() - 2.0f, head.getWidth() - 4.0f, 2.0f);
        }
        const bool iconsShown = col.getWidth() > 56.0f && (sel || hover.section == i || s.locked);
        g.setFont (display (col.getWidth() > 120.0f ? 9.5f : 8.0f, col.getWidth() > 120.0f ? 0.08f : 0.02f));
        g.setColour (s.isReference ? amber : (sel ? moon : mist));
        auto nameR = head.reduced (6.0f, 0).withHeight (15.0f).translated (0, 2.0f);
        if (iconsShown && col.getWidth() < 140.0f) nameR = nameR.translated (0, -1.0f).withTrimmedRight (0);
        g.drawText (s.isReference ? utf8 ("TON ") + s.name : s.name, nameR, juce::Justification::centredLeft, true);
        g.setFont (mono (7.0f));
        g.setColour (dusk);
        g.drawText (juce::String (s.bars) + " MES.", head.reduced (6.0f, 0).withTrimmedTop (16.0f), juce::Justification::centredLeft, true);
        if (playing) drawLed (g, { head.getRight() - 40.0f, head.getBottom() - 8.0f }, 2.4f, mint, true);
        if (col.getWidth() > 56.0f)
        {
            const auto icons = juce::Rectangle<float> (col.getRight() - 32.0f, col.getY() + headerH - 17.0f, 30.0f, 12.0f);
            if (iconsShown)
            {
                drawPadlock (g, icons.withWidth (11.0f), s.locked ? amber : (hover.lockIcon && hover.section == i ? moon : dusk), s.locked);
                if (! s.isReference)
                    drawDice (g, icons.withTrimmedLeft (17.0f).withWidth (11.0f), hover.diceIcon && hover.section == i ? moon : dusk);
            }
        }
        // clips
        for (int l = 0; l < numLanes; ++l)
            drawClip (g, s, (Lane) l, cellRect (i, l), sel);
        // separator
        g.setColour (line);
        g.drawVerticalLine ((int) col.getRight(), col.getY(), col.getBottom());
    }

    // Ruler (bars)
    {
        g.setFont (mono (7.5f));
        g.setColour (dusk);
        const int total = arr.totalBars();
        for (int b = 0; b < total; b += 8)
        {
            const float x = grid.getX() + grid.getWidth() * (float) b / (float) total;
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 2.0f, grid.getBottom() + 1.0f, 30.0f, rulerH - 2.0f), juce::Justification::centredLeft);
        }
    }

    // Playhead
    if (proc.isPreviewing())
    {
        const float x = grid.getX() + grid.getWidth() * (float) (proc.getPreviewBeat() / (std::max (1, arr.totalBars()) * 4.0));
        g.setColour (cyan.withAlpha (0.25f));
        g.fillRect (x - 2.0f, grid.getY(), 4.0f, grid.getHeight());
        g.setColour (cyan);
        g.fillRect (x - 0.5f, grid.getY(), 1.5f, grid.getHeight());
    }
    drawScreenGlass (g, r.reduced (2.0f), 14.0f, 0.35f);
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.position);
    if (h.section != hover.section || h.lane != hover.lane || h.header != hover.header || h.lockIcon != hover.lockIcon
        || h.diceIcon != hover.diceIcon || h.laneLabel != hover.laneLabel)
    {
        hover = h;
        if (h.lockIcon) setTooltip (utf8 ("Verrouiller : cette partie ne change plus."));
        else if (h.diceIcon) setTooltip (utf8 ("Nouvelle idée pour cette partie."));
        else if (h.header) setTooltip (utf8 ("Clic : écouter en boucle. Clic droit : plus intense, plus sombre..."));
        else if (h.laneLabel) setTooltip (utf8 ("Couper / remettre cette partie dans la préécoute."));
        else if (h.section >= 0) setTooltip (utf8 ("Glisse ce clip dans ton DAW."));
        else setTooltip ({});
        repaint();
    }
}

void ArrangementView::mouseExit (const juce::MouseEvent&) { hover = {}; repaint(); }

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    pressed = hitAt (e.position);
    dragStarted = false;
    if (e.mods.isPopupMenu() && pressed.section >= 0) { selected = pressed.section; if (onSelect) onSelect (selected); showSectionMenu (pressed.section); pressed = {}; }
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || pressed.section < 0 || pressed.lane < 0 || pressed.header || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    const auto file = proc.exportClipForDrag (pressed.section, (Lane) pressed.lane);
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void ArrangementView::mouseUp (const juce::MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu()) { dragStarted = false; return; }
    const auto h = hitAt (e.position);
    if (h.laneLabel && pressed.laneLabel && h.lane == pressed.lane)
    {
        proc.setPreviewLaneMuted ((Lane) h.lane, ! proc.isPreviewLaneMuted ((Lane) h.lane));
        repaint();
        return;
    }
    if (h.section < 0 || h.section != pressed.section) return;
    selected = h.section;
    if (h.lockIcon)
        proc.editComposer ([s = h.section] (Composer& c) { c.toggleLock (s); });
    else if (h.diceIcon)
    {
        proc.editComposer ([s = h.section] (Composer& c) { c.regenerateSection (s); });
        proc.previewSection (h.section, true);
    }
    else if (h.header)
    {
        if (proc.isPreviewing() && proc.getPreviewSection() == h.section) proc.stopPreview();
        else proc.previewSection (h.section, true);
    }
    if (onSelect) onSelect (selected);
    repaint();
}

void ArrangementView::showSectionMenu (int section)
{
    const auto& arr = proc.getComposer().getArrangement();
    if (section < 0 || section >= (int) arr.sections.size()) return;
    const auto& s = arr.sections[(size_t) section];
    juce::PopupMenu m;
    m.addSectionHeader (s.name);
    m.addItem (1, utf8 ("Écouter"));
    m.addItem (2, s.locked ? utf8 ("Déverrouiller") : utf8 ("Verrouiller"));
    m.addItem (3, utf8 ("Nouvelle idée"), ! s.isReference);
    m.addSeparator();
    m.addItem (10, "Plus intense");
    m.addItem (11, "Plus sombre", ! s.isReference);
    m.addItem (12, "Plus lumineux", ! s.isReference);
    m.addItem (13, utf8 ("Plus atmosphérique"), ! s.isReference);
    m.addItem (14, utf8 ("Plus mélodique"), ! s.isReference);
    m.addItem (15, "Minimal", ! s.isReference);
    m.addItem (16, "Progressif (16 mesures)", ! s.isReference);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<ArrangementView> (this), section, role = s.role] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& p = safe->proc;
        if (r == 1) { p.previewSection (section, true); return; }
        if (r == 2) { p.editComposer ([section] (Composer& c) { c.toggleLock (section); }); return; }
        if (r == 3) { p.editComposer ([section] (Composer& c) { c.regenerateSection (section); }); p.previewSection (section, true); return; }
        ComposeCommand::Action a;
        a.role = role; a.create = false;
        if (r == 10) a.mood.intense = 1;
        if (r == 11) a.mood.darker = 1;
        if (r == 12) a.mood.brighter = 1;
        if (r == 13) a.mood.atmos = 1;
        if (r == 14) a.mood.melodic = 1;
        if (r == 15) a.mood.minimal = 1;
        if (r == 16) { a.mood.progressive = 1; a.bars = 16; }
        p.editComposer ([section, a] (Composer& c) { c.applyToSection (section, a.mood, a.bars); });
        p.previewSection (section, true);
    });
}

// ======================================================================================
ComposeColumn::ComposeColumn (FINISHAudioProcessor& p) : proc (p)
{
    for (int f = 0; f < numFlavours; ++f)
    {
        auto& b = flavours[(size_t) f];
        b.setButtonText (f == 0 ? utf8 ("FIDÈLE") : flavourName ((Flavour) f));
        b.setClickingTogglesState (false);
        b.onClick = [this, f]
        {
            proc.editComposer ([f] (Composer& c) { c.selectFlavour ((Flavour) f); });
            if (proc.isPreviewing()) proc.previewSection (proc.getPreviewSection(), true);
            refresh();
        };
        addAndMakeVisible (b);
    }
    flavours[0].setTooltip (utf8 ("Au plus près de ton morceau : tes patterns, ta basse, ton motif."));
    flavours[1].setTooltip (utf8 ("Plus d'espace : intro sans kick, long break, pont harmonique."));
    flavours[2].setTooltip (utf8 ("Plus d'énergie : sections courtes, roulements, drop 2 une octave plus haut."));
    creativity.setRange (0.0, 1.0, 0.01);
    creativity.setValue (proc.getComposer().getCreativity(), juce::dontSendNotification);
    creativity.setTooltip (utf8 ("Créativité : à quel point les nouvelles parties s'éloignent de ton matériel."));
    creativity.onDragEnd = [this] { proc.editComposer ([v = (float) creativity.getValue()] (Composer& c) { c.setCreativity (v); }); };
    addAndMakeVisible (creativity);
}

void ComposeColumn::resized()
{
    const int w = getWidth();
    const int bw = (w - 16) / 3;
    for (int f = 0; f < numFlavours; ++f) flavours[(size_t) f].setBounds (8 + f * bw, 252, bw - 4, 30);
    creativity.setBounds (8, 312, w - 16, 22);
}

void ComposeColumn::refresh()
{
    const auto& c = proc.getComposer();
    for (int f = 0; f < numFlavours; ++f)
    {
        flavours[(size_t) f].setToggleState (c.hasArrangement() && (int) c.getFlavour() == f, juce::dontSendNotification);
        flavours[(size_t) f].setEnabled (c.hasArrangement());
    }
    if (! creativity.isMouseButtonDown()) creativity.setValue (c.getCreativity(), juce::dontSendNotification);
    creativity.setEnabled (c.hasArrangement());
    repaint();
}

void ComposeColumn::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    const auto& c = proc.getComposer();
    const auto& d = c.getDNA();
    engraved (g, u8 ("PLAY  \xc2\xb7  PRESS  \xc2\xb7  LISTEN"), { 0.0f, 126.0f, w, 10.0f }, 6.5f);

    auto lcd = juce::Rectangle<float> (8.0f, 144.0f, w - 16.0f, 92.0f);
    drawScreen (g, lcd, 6.0f, true);
    g.setColour (dusk);
    g.setFont (mono (7.5f));
    g.drawText (utf8 ("CE QUE FINISH A ENTENDU"), lcd.reduced (10.0f, 6.0f).withHeight (10.0f), juce::Justification::centredLeft);
    if (! d.valid)
    {
        g.setColour (dusk);
        g.setFont (mono (11.0f));
        g.drawText ("---", lcd, juce::Justification::centred);
    }
    else
    {
        keyArea = juce::Rectangle<float> (lcd.getX() + 10.0f, lcd.getY() + 20.0f, 120.0f, 30.0f);
        bpmArea = juce::Rectangle<float> (lcd.getX() + 136.0f, lcd.getY() + 20.0f, lcd.getWidth() - 146.0f, 30.0f);
        styleArea = juce::Rectangle<float> (lcd.getX() + 10.0f, lcd.getY() + 56.0f, lcd.getWidth() - 20.0f, 26.0f);
        const bool keyUnsure = d.keyConfidence < 0.45f && ! d.keyOverridden;
        const bool styleUnsure = d.styleConfidence < 0.5f && ! d.styleOverridden;
        neonText (g, d.keyName(), display (17.0f, 0.06f), keyArea, keyUnsure ? amber : cyan, juce::Justification::centredLeft, 0.8f);
        g.setFont (mono (8.0f));
        g.setColour (keyUnsure ? amber : dusk);
        g.drawText (keyUnsure ? utf8 ("? ") + juce::String (juce::roundToInt (d.keyConfidence * 100)) + "%" : juce::String (juce::roundToInt (d.keyConfidence * 100)) + "%",
                    keyArea.withTrimmedLeft (98.0f), juce::Justification::centredLeft);
        g.setColour (moon);
        g.setFont (mono (15.0f));
        g.drawText (juce::String (d.bpm, 1), bpmArea.withTrimmedRight (26.0f), juce::Justification::centredRight);
        g.setColour (dusk); g.setFont (mono (7.5f));
        g.drawText ("BPM", bpmArea, juce::Justification::centredRight);
        g.setColour (styleUnsure ? amber : mist);
        g.setFont (display (9.5f, 0.1f));
        g.drawText (juce::String (styleName (d.style)) + (styleUnsure ? "  ?" : ""), styleArea, juce::Justification::centredLeft);
        g.setFont (mono (7.5f));
        g.setColour (dusk);
        juce::StringArray parts;
        if (d.hasDrums) parts.add ("DRUMS");
        if (d.hasBass) parts.add ("BASS");
        if (d.hasChords) parts.add ("CHORDS");
        if (d.hasMelody) parts.add (utf8 ("MÉLODIE"));
        g.drawText (parts.joinIntoString (" + "), styleArea, juce::Justification::centredRight);
    }
    drawScreenGlass (g, lcd, 6.0f, 0.5f);

    engraved (g, "VERSION", { 8.0f, 240.0f, w - 16.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
    engraved (g, utf8 ("FIDÈLE  ←  CRÉATIVITÉ  →  LIBRE"), { 8.0f, 296.0f, w - 16.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
}

void ComposeColumn::mouseUp (const juce::MouseEvent& e)
{
    const auto& d = proc.getComposer().getDNA();
    if (! d.valid) return;
    if (keyArea.contains (e.position))
    {
        juce::PopupMenu m;
        m.addSectionHeader (utf8 ("Corriger la tonalité"));
        juce::PopupMenu minors, majors;
        for (int k = 0; k < 12; ++k)
        {
            minors.addItem (100 + k, pitchClassName (k) + " MIN", true, d.minor && d.keyRoot == k);
            majors.addItem (200 + k, pitchClassName (k) + " MAJ", true, ! d.minor && d.keyRoot == k);
        }
        if (d.keyConfidence < 0.9f) m.addItem (300, utf8 ("Plutôt ") + pitchClassName (d.altKeyRoot) + (d.altMinor ? " MIN" : " MAJ"));
        m.addSubMenu ("Mineur", minors);
        m.addSubMenu ("Majeur", majors);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, alt = d.altKeyRoot, altMinor = d.altMinor] (int r)
        {
            if (r == 0) return;
            const int root = r == 300 ? alt : r % 100;
            const bool minor = r == 300 ? altMinor : r < 200;
            proc.editComposer ([root, minor] (Composer& c) { c.overrideKey (root, minor); });
            refresh();
        });
    }
    else if (styleArea.contains (e.position))
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Style");
        for (int s = 0; s < numStyles; ++s) m.addItem (1 + s, styleName ((Style) s), true, (int) d.style == s);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r == 0) return;
            proc.editComposer ([r] (Composer& c) { c.overrideStyle ((Style) (r - 1)); });
            refresh();
        });
    }
    else if (bpmArea.contains (e.position))
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Tempo");
        m.addItem (1, "x2 (" + juce::String (d.bpm * 2.0, 1) + ")");
        m.addItem (2, "/2 (" + juce::String (d.bpm * 0.5, 1) + ")");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, bpm = d.bpm] (int r)
        {
            if (r == 0) return;
            proc.editComposer ([r, bpm] (Composer& c) { c.overrideTempo (r == 1 ? bpm * 2.0 : bpm * 0.5); });
            refresh();
        });
    }
}

// ======================================================================================
ComposeConsole::ComposeConsole (FINISHAudioProcessor& p) : proc (p)
{
    for (auto* b : { &playPart, &lock, &newIdea, &playAll, &stop, &solo, &sendLive, &exportFiles, &undo })
        addAndMakeVisible (b);
    sendLive.getProperties().set ("style", "primary");
    solo.setClickingTogglesState (true);
    solo.setToggleState (proc.getPreviewSolo(), juce::dontSendNotification);
    lock.getProperties().set ("led", "amber");

    playPart.setTooltip (utf8 ("Écouter cette partie en boucle (sons intégrés de FINISH)."));
    lock.setTooltip (utf8 ("Verrouiller : la partie ne change plus, même si tu régénères tout."));
    newIdea.setTooltip (utf8 ("Une autre idée pour cette partie, même ADN."));
    playAll.setTooltip (utf8 ("Écouter tout l'arrangement depuis le début."));
    solo.setTooltip (utf8 ("SOLO : la préécoute remplace ce qui joue dans ton DAW. Éteint : elle s'ajoute."));
    sendLive.setTooltip (utf8 ("Crée les pistes et les clips directement dans ton set Ableton Live (nouvelles pistes uniquement)."));
    exportFiles.setTooltip (utf8 ("MIDI + WAV + guide dans Musique/FINISH (pour FL Studio et les autres DAW)."));

    playPart.onClick = [this] { if (section >= 0) proc.previewSection (section, true); if (onChange) onChange(); };
    lock.onClick = [this] { if (section >= 0) proc.editComposer ([s = section] (Composer& c) { c.toggleLock (s); }); if (onChange) onChange(); };
    newIdea.onClick = [this]
    {
        if (section < 0) return;
        proc.editComposer ([s = section] (Composer& c) { c.regenerateSection (s); });
        proc.previewSection (section, true);
        if (onChange) onChange();
    };
    playAll.onClick = [this] { proc.previewSection (-1, false); if (onChange) onChange(); };
    stop.onClick = [this] { proc.stopPreview(); if (onChange) onChange(); };
    solo.onClick = [this] { proc.setPreviewSolo (solo.getToggleState()); };
    sendLive.onClick = [this] { proc.sendArrangementToLive(); if (onChange) onChange(); };
    exportFiles.onClick = [this] { proc.exportArrangement(); if (onChange) onChange(); };
    undo.onClick = [this] { proc.editComposer ([] (Composer& c) { c.undo(); }); if (onChange) onChange(); };
}

void ComposeConsole::resized()
{
    playPart.setBounds (16, 118, 116, 32);
    lock.setBounds (138, 118, 92, 32);
    newIdea.setBounds (236, 118, 116, 32);

    const int x = 704;
    playAll.setBounds (x, 20, 124, 34);
    stop.setBounds (x + 130, 20, 92, 34);
    solo.setBounds (x + 228, 20, 92, 34);
    sendLive.setBounds (x, 70, 214, 40);
    exportFiles.setBounds (x + 222, 70, 98, 40);
    undo.setBounds (x + 326, 20, 92, 34);
}

juce::Rectangle<float> ComposeConsole::chipRect (int lane) const
{
    return { 372.0f + (float) lane * 52.0f, 116.0f, 49.0f, 36.0f };
}

void ComposeConsole::refresh()
{
    const auto& c = proc.getComposer();
    const bool has = c.hasArrangement();
    const bool sec = has && section >= 0 && section < (int) c.getArrangement().sections.size();
    playPart.setEnabled (sec);
    lock.setEnabled (sec);
    newIdea.setEnabled (sec && ! c.getArrangement().sections[(size_t) std::max (0, section)].isReference);
    lock.setToggleState (sec && c.getArrangement().sections[(size_t) section].locked, juce::dontSendNotification);
    for (auto* b : { &playAll, &stop, &sendLive, &exportFiles }) b->setEnabled (has);
    undo.setEnabled (c.canUndo());
    solo.setToggleState (proc.getPreviewSolo(), juce::dontSendNotification);
    repaint();
}

void ComposeConsole::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto left = r.withWidth (684.0f);
    drawScreen (g, left.reduced (2.0f), 10.0f);
    const auto& c = proc.getComposer();
    if (! c.hasArrangement() || section < 0 || section >= (int) c.getArrangement().sections.size())
    {
        g.setColour (dusk);
        g.setFont (mono (10.0f));
        g.drawText (c.hasArrangement() ? utf8 ("CHOISIS UNE PARTIE DANS L'ARRANGEMENT") : utf8 ("—"), left, juce::Justification::centred);
    }
    else
    {
        const auto& s = c.getArrangement().sections[(size_t) section];
        neonText (g, s.isReference ? "TON " + s.name : s.name, display (20.0f, 0.1f), { 18.0f, 12.0f, 400.0f, 32.0f },
                  s.isReference ? amber : cyan, juce::Justification::centredLeft, 0.8f);
        g.setFont (mono (9.5f));
        g.setColour (dusk);
        g.drawText (utf8 ("MESURES ") + barsRange (s) + "  ·  " + juce::String (s.bars) + utf8 (" MES.") + (s.locked ? utf8 ("  ·  VERROUILLÉ") : juce::String()),
                    juce::Rectangle<float> (18.0f, 44.0f, 640.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (mist);
        g.setFont (text (12.5f));
        g.drawFittedText (s.recipe, juce::Rectangle<int> (18, 62, 650, 40), juce::Justification::topLeft, 2, 1.0f);

        // drag chips, one per part
        for (int l = 0; l < numLanes; ++l)
        {
            const auto& clip = s.isReference ? s.layers[(size_t) l] : s.clips[(size_t) l];
            const bool has = l == (int) Lane::Fx ? ! s.clips[(size_t) l].fx.empty() : ! clip.notes.empty();
            auto cr = chipRect (l);
            g.setColour (laneColour ((Lane) l).withAlpha (has ? 0.18f : 0.04f));
            g.fillRoundedRectangle (cr, 4.0f);
            g.setColour (laneColour ((Lane) l).withAlpha (has ? 0.7f : 0.15f));
            g.drawRoundedRectangle (cr, 4.0f, 1.0f);
            g.setFont (label (6.0f));
            g.setColour (has ? moon : dusk);
            g.drawText (laneName ((Lane) l), cr.withHeight (18.0f).translated (0, 3.0f), juce::Justification::centred);
            g.setFont (mono (6.5f));
            g.setColour (has ? mist : dusk);
            g.drawText (l == (int) Lane::Fx ? "WAV" : "MIDI", cr.withTrimmedTop (19.0f).withTrimmedBottom (3.0f), juce::Justification::centred);
        }
        g.setFont (mono (7.0f));
        g.setColour (dusk);
        g.drawText (utf8 ("GLISSE ↓ VERS TON DAW"), juce::Rectangle<float> (372.0f, 104.0f, 310.0f, 12.0f), juce::Justification::centredLeft);
    }

    // Right side: delivery status
    auto right = r.withTrimmedLeft (692.0f);
    engraved (g, "PREVIEW", { right.getX() + 12.0f, 6.0f, 200.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
    engraved (g, "DELIVERY", { right.getX() + 12.0f, 58.0f, 200.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
    auto st = juce::Rectangle<float> (right.getX() + 10.0f, 120.0f, right.getWidth() - 16.0f, 38.0f);
    drawScreen (g, st, 4.0f, true);
    g.setFont (mono (8.5f));
    const auto live = c.getLiveStatus();
    g.setColour (live.isEmpty() ? dusk : (c.getLiveOk() ? mint : (c.isWaitingForLive() ? cyan : amber)));
    g.drawFittedText (live.isEmpty() ? utf8 ("ABLETON LIVE : SEND TO LIVE   ·   AUTRES DAW : EXPORT") : live.toUpperCase(),
                      st.reduced (8.0f, 3.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
}

void ComposeConsole::mouseDown (const juce::MouseEvent& e)
{
    dragLane = -1;
    dragStarted = false;
    for (int l = 0; l < numLanes; ++l)
        if (chipRect (l).contains (e.position)) dragLane = l;
}

void ComposeConsole::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0 || dragStarted || e.getDistanceFromDragStart() < 5 || section < 0) return;
    dragStarted = true;
    const auto f = proc.exportClipForDrag (section, (Lane) dragLane);
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}
