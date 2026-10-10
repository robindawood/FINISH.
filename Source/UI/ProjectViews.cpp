#include "ProjectViews.h"

using namespace Night;

namespace
{
    constexpr float labelW = 132.0f, headerH = 30.0f, topH = 24.0f;

    juce::Colour kindColour (SecKind k)
    {
        switch (k)
        {
            case SecKind::Intro: return cyan;
            case SecKind::Build: case SecKind::Build2: return amber;
            case SecKind::Drop: return violet;
            case SecKind::Drop2: return violet.interpolatedWith (indigo, 0.4f);
            case SecKind::Break: return indigo;
            case SecKind::Outro: return mint;
            case SecKind::Rise: return gold;
            default: return mist;
        }
    }

    juce::Colour roleColour (TrackRole r)
    {
        static const juce::uint32 c[] = { 0xffff5a8a, 0xffff8fb1, 0xff9a6bff, 0xff2de2ff, 0xffffc857, 0xff39ffb0, 0xffff7a3d, 0xffffd84d, 0xffb9addb, 0xff6f6394 };
        return juce::Colour (c[(int) r]);
    }

    void padlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool closed)
    {
        auto body = r.withTrimmedTop (r.getHeight() * 0.45f);
        juce::Path sh;
        const float sx = r.getCentreX(), sw = r.getWidth() * 0.3f, top = r.getY() + 1.0f, base = body.getY();
        sh.startNewSubPath (sx - sw, base);
        sh.lineTo (sx - sw, top + sw);
        sh.addCentredArc (sx, top + sw, sw, sw, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
        sh.lineTo (sx + sw, closed ? base : base - 3.0f);
        g.setColour (c);
        g.strokePath (sh, juce::PathStrokeType (1.3f));
        g.fillRoundedRectangle (body, 1.5f);
    }
}

// ======================================================================================
ProjectView::ProjectView (FINISHAudioProcessor& p) : proc (p) {}

juce::Rectangle<float> ProjectView::grid() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (topH).withTrimmedLeft (labelW);
}

std::vector<int> ProjectView::rows() const
{
    std::vector<int> r;
    const auto& prj = proc.getProject();
    const auto& p = prj.getProject();
    for (auto& t : p.tracks)
        if (! t.group && t.hasContent()) r.push_back (t.index);
    if (r.size() > 22) r.resize (22);
    return r;
}

float ProjectView::xOfBar (double bar) const
{
    const auto g = grid();
    const auto& plan = proc.getProject().getPlan();
    const double total = std::max (1, plan.lastBar());
    return g.getX() + (float) (bar / total) * g.getWidth();
}

ProjectView::Hit ProjectView::hitAt (juce::Point<float> pt) const
{
    Hit h;
    const auto& prj = proc.getProject();
    if (! prj.hasPlan()) return h;
    const auto g = grid();
    const auto rs = rows();
    if (rs.empty()) return h;
    const float rowH = (g.getHeight() - headerH) / (float) rs.size();
    int row = -1;
    if (pt.y >= g.getY() + headerH && pt.y < g.getBottom()) row = juce::jlimit (0, (int) rs.size() - 1, (int) ((pt.y - g.getY() - headerH) / rowH));
    if (pt.x < g.getX() && pt.x >= g.getX() - labelW && row >= 0) { h.label = true; h.track = rs[(size_t) row]; return h; }
    const auto& plan = prj.getPlan();
    for (int i = 0; i < (int) plan.sections.size(); ++i)
    {
        const auto& s = plan.sections[(size_t) i];
        const float x0 = xOfBar (s.startBar), x1 = xOfBar (s.startBar + s.bars);
        if (pt.x < x0 || pt.x >= x1) continue;
        h.section = i;
        if (pt.y >= g.getY() && pt.y < g.getY() + headerH) h.header = true;
        else if (row >= 0) h.track = rs[(size_t) row];
        return h;
    }
    return h;
}

void ProjectView::paintEmpty (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& prj = proc.getProject();
    const auto& d = prj.getDiagnosis();
    auto inner = r.reduced (36.0f, 26.0f);
    const bool reading = prj.getPhase() == ProjectSession::Phase::Reading;
    drawLed (g, { inner.getX() + 6.0f, inner.getY() + 12.0f }, 5.0f, d.connected() ? mint : (d.status == LiveBridge::Status::LiveNotRunning ? amber : rose), true);
    neonText (g, d.connected() ? (reading ? utf8 ("LECTURE DE TON PROJET…") : utf8 ("CONNECTÉ À ABLETON LIVE")) : utf8 ("PAS ENCORE CONNECTÉ À LIVE"),
              display (15.0f, 0.08f), inner.withHeight (26.0f).withTrimmedLeft (22.0f), d.connected() ? mint : amber, juce::Justification::centredLeft, 0.7f);
    g.setColour (mist);
    g.setFont (text (13.0f));
    auto body = inner.withTrimmedTop (40.0f);
    if (d.connected())
    {
        g.drawFittedText (reading ? utf8 ("FINISH lit tes pistes, tes clips et tes notes…")
                                  : utf8 ("Appuie sur FINISH : je lis ton set (pistes, clips, notes MIDI, instruments), je te montre ce qui manque\n"
                                          "et je propose de le compléter avec TES pistes et TES sons. Rien n'est écrit sans ton accord."),
                          body.withHeight (60.0f).toNearestInt(), juce::Justification::topLeft, 4);
        return;
    }
    g.setFont (bold (12.5f));
    g.setColour (moon);
    g.drawText (d.title, body.withHeight (18.0f), juce::Justification::centredLeft);
    g.setFont (text (12.0f));
    float y = body.getY() + 26.0f;
    for (int i = 0; i < d.steps.size(); ++i)
    {
        g.setColour (cyan);
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (body.getX(), y, 18.0f, 18.0f), juce::Justification::centredLeft);
        g.setColour (mist);
        g.drawFittedText (d.steps[i], juce::Rectangle<int> ((int) body.getX() + 20, (int) y, (int) body.getWidth() - 20, 34), juce::Justification::topLeft, 2);
        y += 34.0f;
    }
    g.setColour (dusk);
    g.setFont (text (11.0f));
    g.drawFittedText (LiveBridge::linkVersusControlSurface(), juce::Rectangle<int> ((int) body.getX(), (int) y + 4, (int) body.getWidth(), 48),
                      juce::Justification::topLeft, 3);
}

void ProjectView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r.reduced (2.0f), 14.0f);
    const auto& prj = proc.getProject();
    if (! prj.hasPlan())
    {
        paintEmpty (g, r);
        drawScreenGlass (g, r.reduced (2.0f), 14.0f, 0.4f);
        return;
    }
    const auto& p = prj.getProject();
    const auto& a = prj.getAnalysis();
    const auto& plan = prj.getPlan();
    const auto gr = grid();
    const auto rs = rows();

    // Top line
    {
        auto top = r.reduced (14.0f, 8.0f).withHeight (16.0f);
        g.setFont (mono (9.0f));
        g.setColour (cyan);
        g.drawText (utf8 ("TON PROJET"), top, juce::Justification::centredLeft);
        g.setColour (mist);
        g.drawText (juce::String ((int) p.tracks.size()) + utf8 (" PISTES  ·  ") + a.keyName() + "  ·  " + juce::String (p.tempo, 1) + " BPM  ·  "
                    + levelName (plan.level) + "  V" + juce::String (prj.getVersionIndex() + 1),
                    top.withTrimmedLeft (84.0f), juce::Justification::centredLeft);
        g.setColour (dusk);
        g.drawText (utf8 ("HACHURÉ = DÉJÀ DANS TON SET   ·   COULEUR = À AJOUTER"), top, juce::Justification::centredRight);
    }
    if (rs.empty()) return;
    const float rowH = (gr.getHeight() - headerH) / (float) rs.size();

    // Track labels
    for (size_t k = 0; k < rs.size(); ++k)
    {
        const int ti = rs[k];
        const auto& t = p.tracks[(size_t) ti];
        auto lr = juce::Rectangle<float> (gr.getX() - labelW, gr.getY() + headerH + rowH * (float) k, labelW - 6.0f, rowH);
        const bool locked = prj.isTrackLocked (ti);
        const bool hov = hover.label && hover.track == ti;
        g.setColour (roleColour (t.role).withAlpha (0.9f));
        g.fillRoundedRectangle (lr.withWidth (3.0f).reduced (0, 2.0f), 1.5f);
        g.setFont (text (juce::jlimit (8.5f, 11.0f, rowH * 0.62f)));
        g.setColour (hov ? moon : mist);
        g.drawText (t.name, lr.withTrimmedLeft (8.0f).withTrimmedRight (34.0f), juce::Justification::centredLeft, true);
        g.setFont (mono (6.5f));
        g.setColour (dusk);
        g.drawText (trackRoleName (t.role), lr.withTrimmedRight (14.0f), juce::Justification::centredRight);
        if (locked || hov) padlock (g, lr.withLeft (lr.getRight() - 11.0f).withSizeKeepingCentre (9.0f, std::min (11.0f, rowH - 2.0f)), locked ? amber : dusk, locked);
    }

    // Sections
    for (int i = 0; i < (int) plan.sections.size(); ++i)
    {
        const auto& s = plan.sections[(size_t) i];
        const float x0 = xOfBar (s.startBar), x1 = xOfBar (s.startBar + s.bars);
        const auto col = kindColour (s.kind);
        auto head = juce::Rectangle<float> (x0, gr.getY(), x1 - x0, headerH - 4.0f).reduced (1.0f, 0);
        const bool playing = proc.isPreviewingProject() && proc.getPreviewSection() == i;
        const bool hov = hover.header && hover.section == i;
        if (s.existing)
        {
            g.setColour (panelHi.withAlpha (0.6f));
            g.fillRoundedRectangle (head, 3.0f);
        }
        else
        {
            g.setColour (col.withAlpha (playing ? 0.45f : (hov ? 0.3f : 0.18f)));
            g.fillRoundedRectangle (head, 3.0f);
            g.setColour (col.withAlpha (0.8f));
            g.drawRoundedRectangle (head, 3.0f, 1.0f);
        }
        g.setFont (label (head.getWidth() < 60.0f ? 6.0f : 7.5f));
        g.setColour (s.existing ? mist : moon);
        g.drawText ((s.existing ? utf8 ("TON ") : juce::String()) + s.name, head.reduced (5.0f, 0).withTrimmedBottom (11.0f), juce::Justification::centredLeft, true);
        g.setFont (mono (6.5f));
        g.setColour (dusk);
        g.drawText (juce::String (s.startBar + 1) + "-" + juce::String (s.startBar + s.bars) + (s.existing ? juce::String() : (playing ? utf8 (" ■") : utf8 (" ▶"))),
                    head.reduced (5.0f, 1.0f).withTrimmedTop (14.0f), juce::Justification::centredLeft, true);

        for (size_t k = 0; k < rs.size(); ++k)
        {
            const int ti = rs[k];
            auto cell = juce::Rectangle<float> (x0, gr.getY() + headerH + rowH * (float) k, x1 - x0, rowH).reduced (1.0f, rowH > 12.0f ? 1.5f : 0.5f);
            const auto rc = roleColour (p.tracks[(size_t) ti].role);
            if (s.existing)
            {
                // What the user's track actually plays there (per bar)
                g.saveState();
                g.reduceClipRegion (cell.toNearestInt());
                g.setColour (mist.withAlpha (0.05f));
                for (float x = cell.getX() - cell.getHeight(); x < cell.getRight(); x += 6.0f)
                    g.drawLine (x, cell.getBottom(), x + cell.getHeight(), cell.getY(), 1.0f);
                g.restoreState();
                if ((size_t) ti < a.activity.size())
                    for (int b = s.startBar; b < s.startBar + s.bars && b < (int) a.activity[(size_t) ti].size(); ++b)
                        if (a.activity[(size_t) ti][(size_t) b] > 0)
                        {
                            const float bx0 = xOfBar (b), bx1 = xOfBar (b + 1);
                            g.setColour (rc.withAlpha (0.28f));
                            g.fillRect (juce::Rectangle<float> (bx0, cell.getY(), bx1 - bx0 - 0.5f, cell.getHeight()));
                        }
                continue;
            }
            if ((size_t) ti >= s.cells.size()) continue;
            const auto& c = s.cells[(size_t) ti];
            const bool hovCell = ! hover.header && hover.section == i && hover.track == ti;
            if (c.parts.empty())
            {
                if (hovCell) { g.setColour (line); g.drawRect (cell, 1.0f); }
                continue;
            }
            for (auto& part : c.parts)
            {
                const float px0 = xOfBar (s.startBar + part.startBar), px1 = xOfBar (s.startBar + part.startBar + part.bars);
                auto pr = juce::Rectangle<float> (px0, cell.getY(), px1 - px0, cell.getHeight()).reduced (0.5f, 0);
                const float alpha = c.off ? 0.06f : (part.exactCopy() ? 0.16f : 0.26f);
                g.setColour (rc.withAlpha (alpha));
                g.fillRoundedRectangle (pr, 2.0f);
                g.setColour (rc.withAlpha (c.off ? 0.2f : 0.7f));
                g.drawRoundedRectangle (pr, 2.0f, part.exactCopy() ? 0.7f : 1.1f);
                if (! c.off && ! part.notes.empty() && pr.getHeight() > 6.0f)
                {
                    int lo = 127, hi = 0;
                    for (auto& n : part.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
                    const float span = (float) std::max (6, hi - lo + 1);
                    const double beats = part.bars * plan.beatsPerBar;
                    g.setColour (rc.withAlpha (0.95f));
                    for (auto& n : part.notes)
                    {
                        const float nx = pr.getX() + pr.getWidth() * (float) (n.start / beats);
                        const float nw = std::max (1.0f, pr.getWidth() * (float) (n.length / beats));
                        const float ny = pr.getBottom() - 2.0f - (pr.getHeight() - 4.0f) * (float) (n.pitch - lo + 0.5f) / span;
                        g.fillRect (nx, ny - 0.6f, nw, 1.3f);
                    }
                }
                else if (! c.off && pr.getHeight() > 9.0f && pr.getWidth() > 34.0f)
                {
                    g.setFont (mono (6.5f));
                    g.setColour (rc);
                    g.drawText (utf8 ("COPIE"), pr.reduced (3.0f, 0), juce::Justification::centredLeft);
                }
            }
            if (c.off)
            {
                g.setColour (rose.withAlpha (0.6f));
                g.drawLine (cell.getX() + 3, cell.getY() + 2, cell.getRight() - 3, cell.getBottom() - 2, 1.0f);
            }
            if (c.locked && cell.getWidth() > 14.0f) padlock (g, cell.withLeft (cell.getRight() - 10.0f).withSizeKeepingCentre (7.0f, std::min (9.0f, cell.getHeight() - 2.0f)), amber, true);
            if (hovCell) { g.setColour (moon.withAlpha (0.5f)); g.drawRoundedRectangle (cell, 2.0f, 1.0f); }
        }
        // FX row hint
        if (! s.existing && ! s.fx.empty())
        {
            g.setColour (sun.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x1 - 7.0f, gr.getY() + 6.0f }));
        }
    }
    // Where the new part starts
    {
        const float x = xOfBar (plan.firstNewBar);
        g.setColour (cyan.withAlpha (0.6f));
        for (float y = gr.getY(); y < gr.getBottom(); y += 6.0f) g.drawLine (x, y, x, y + 3.0f, 1.0f);
    }
    // Layers on the existing sections
    for (auto& l : plan.layers)
        for (size_t k = 0; k < rs.size(); ++k)
            if (rs[k] == l.track)
            {
                const float x0 = xOfBar (l.startBar), x1 = xOfBar (l.startBar + l.bars);
                auto cell = juce::Rectangle<float> (x0, gr.getY() + headerH + rowH * (float) k, x1 - x0, rowH).reduced (0.5f, 1.0f);
                g.setColour (gold.withAlpha (0.85f));
                g.drawRoundedRectangle (cell, 2.0f, 1.2f);
            }
    // Preview playhead
    if (proc.isPreviewingProject())
    {
        const double bar = plan.firstNewBar + proc.getPreviewBeat() / plan.beatsPerBar;
        const float x = xOfBar (bar);
        g.setColour (moon.withAlpha (0.85f));
        g.drawLine (x, gr.getY(), x, gr.getBottom(), 1.2f);
    }
    drawScreenGlass (g, r.reduced (2.0f), 14.0f, 0.35f);
}

void ProjectView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.position);
    if (h.section != hover.section || h.track != hover.track || h.header != hover.header || h.label != hover.label)
    {
        hover = h;
        const auto& plan = proc.getProject().getPlan();
        juce::String tip;
        if (h.label) tip = utf8 ("Clic : verrouiller cette piste (FINISH n'en écrit que des copies exactes).");
        else if (h.section >= 0 && h.section < (int) plan.sections.size())
        {
            const auto& s = plan.sections[(size_t) h.section];
            if (h.header) tip = s.existing ? utf8 ("Ta section, déjà dans ton set (FINISH n'y touche pas).") : s.recipe + utf8 ("  —  clic : écouter (aperçu), clic droit : autre variante");
            else if (h.track >= 0 && ! s.existing && (size_t) h.track < s.cells.size())
                tip = proc.getProject().getProject().tracks[(size_t) h.track].name + " : " + s.cells[(size_t) h.track].summary() + utf8 ("  —  clic : activer/désactiver");
        }
        setTooltip (tip);
        setMouseCursor (h.section >= 0 || h.label ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ProjectView::mouseExit (const juce::MouseEvent&) { hover = {}; repaint(); }

void ProjectView::showSectionMenu (int section)
{
    juce::PopupMenu m;
    m.addItem (1, utf8 ("Écouter (aperçu)"));
    m.addItem (2, utf8 ("Autre variante de cette partie"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, section] (int r)
    {
        if (r == 1) proc.previewProject (section, true);
        if (r == 2)
        {
            proc.editProject ([section] (ProjectSession& s) { s.cycleSectionVariant (section); });
            if (proc.isPreviewingProject()) proc.previewProject (section, true);
        }
        if (onChange) onChange();
        repaint();
    });
}

void ProjectView::mouseUp (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.position);
    const auto& plan = proc.getProject().getPlan();
    if (h.label && h.track >= 0)
        proc.editProject ([t = h.track] (ProjectSession& s) { s.toggleTrackLock (t); });
    else if (h.section >= 0 && h.section < (int) plan.sections.size() && ! plan.sections[(size_t) h.section].existing)
    {
        if (h.header)
        {
            if (e.mods.isPopupMenu()) { showSectionMenu (h.section); return; }
            if (proc.isPreviewingProject() && proc.getPreviewSection() == h.section) proc.stopPreview();
            else proc.previewProject (h.section, true);
        }
        else if (h.track >= 0)
            proc.editProject ([sec = h.section, t = h.track] (ProjectSession& s) { s.toggleCell (sec, t); });
    }
    if (onChange) onChange();
    repaint();
}

// ======================================================================================
ProjectColumn::ProjectColumn (FINISHAudioProcessor& p) : proc (p)
{
    const char* ln[] = { "COMPLÉTER", "ENRICHIR", "AMÉLIORER", "FINALISER" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = levels[(size_t) i];
        b.setButtonText (utf8 (ln[i]));
        b.setTooltip (levelHelp ((Level) i));
        b.onClick = [this, i]
        {
            proc.editProject ([i] (ProjectSession& s) { s.setLevel ((Level) i); });
            if (i == (int) Level::Finalise) proc.applyRecommendedTarget();
            if (proc.isPreviewingProject()) proc.previewProject (proc.getPreviewSection(), true);
            if (onChange) onChange();
        };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto& b = versions[(size_t) i];
        b.setButtonText ("V" + juce::String (i + 1));
        b.setTooltip (utf8 ("Trois versions de chaque nouvelle partie, toutes faites avec tes pistes."));
        b.onClick = [this, i]
        {
            proc.editProject ([i] (ProjectSession& s) { s.setVersion (i); });
            if (proc.isPreviewingProject()) proc.previewProject (proc.getPreviewSection(), true);
            if (onChange) onChange();
        };
        addAndMakeVisible (b);
    }
    install.setTooltip (utf8 ("Copie le script FINISH dans ta User Library d'Ableton (puis redémarre Live)."));
    install.onClick = [this]
    {
        juce::String report;
        ArrangementExport::installLiveScript (report, true);
        proc.editProject ([] (ProjectSession& s) { s.refreshDiagnosis(); });
        if (onChange) onChange();
    };
    addChildComponent (install);
}

void ProjectColumn::resized()
{
    const int w = getWidth();
    const int bw = (w - 16) / 2;
    for (int i = 0; i < 4; ++i) levels[(size_t) i].setBounds (8 + (i % 2) * bw, 222 + (i / 2) * 32, bw - 4, 28);
    const int vw = (w - 16) / 3;
    for (int i = 0; i < 3; ++i) versions[(size_t) i].setBounds (8 + i * vw, 306, vw - 4, 28);
    install.setBounds (w - 98, 182, 90, 22);
}

void ProjectColumn::refresh()
{
    const auto& prj = proc.getProject();
    const bool has = prj.hasPlan();
    for (int i = 0; i < 4; ++i)
    {
        levels[(size_t) i].setEnabled (has);
        levels[(size_t) i].setToggleState (has && (int) prj.getOptions().level == i, juce::dontSendNotification);
    }
    for (int i = 0; i < 3; ++i)
    {
        versions[(size_t) i].setEnabled (has);
        versions[(size_t) i].setToggleState (has && prj.getVersionIndex() == i, juce::dontSendNotification);
    }
    install.setVisible (prj.getDiagnosis().status == LiveBridge::Status::NotInstalled);
    repaint();
}

void ProjectColumn::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    const auto& prj = proc.getProject();
    const auto& d = prj.getDiagnosis();
    engraved (g, utf8 ("PRESS  ·  LIRE  ·  PROPOSER"), { 0.0f, 126.0f, w, 10.0f }, 6.5f);
    auto lcd = juce::Rectangle<float> (8.0f, 142.0f, w - 16.0f, 64.0f);
    drawScreen (g, lcd, 6.0f, true);
    const auto ledCol = d.connected() ? mint : (d.status == LiveBridge::Status::LiveNotRunning || d.status == LiveBridge::Status::OldScript ? amber : rose);
    drawLed (g, { lcd.getX() + 12.0f, lcd.getY() + 13.0f }, 3.5f, ledCol, true);
    g.setFont (mono (7.5f));
    g.setColour (dusk);
    g.drawText ("ABLETON LIVE", lcd.reduced (22.0f, 6.0f).withHeight (12.0f), juce::Justification::centredLeft);
    g.setFont (text (11.0f));
    g.setColour (d.connected() ? mint : amber);
    g.drawFittedText (d.title, lcd.reduced (10.0f, 4.0f).withTrimmedTop (18.0f).withTrimmedRight (d.status == LiveBridge::Status::NotInstalled ? 92.0f : 0.0f).toNearestInt(),
                      juce::Justification::topLeft, 2, 0.9f);
    if (prj.hasProject())
    {
        g.setFont (mono (7.0f));
        g.setColour (dusk);
        const auto& p = prj.getProject();
        g.drawText (juce::String ((int) p.tracks.size()) + " pistes  " + juce::String (p.countClips (false)) + " clips  " + juce::String (p.countNotes()) + " notes",
                    lcd.reduced (10.0f, 4.0f), juce::Justification::bottomLeft);
    }
    drawScreenGlass (g, lcd, 6.0f, 0.5f);
    engraved (g, "INTERVENTION", { 8.0f, 210.0f, w - 16.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
    engraved (g, "VERSION", { 8.0f, 294.0f, w - 16.0f, 10.0f }, 6.5f, juce::Justification::centredLeft);
}

// ======================================================================================
ProjectConsole::ProjectConsole (FINISHAudioProcessor& p) : proc (p)
{
    read.setButtonText (utf8 ("LIRE LE PROJET"));
    listen.setButtonText (utf8 ("ÉCOUTER"));
    stop.setButtonText ("STOP");
    previous.setButtonText (utf8 ("PLAN PRÉCÉDENT"));
    send.setButtonText (utf8 ("ENVOYER DANS LIVE"));
    undoSend.setButtonText (utf8 ("ANNULER L'ENVOI"));
    journal.setButtonText ("JOURNAL");
    for (auto* b : { &read, &listen, &stop, &previous, &send, &undoSend, &journal }) addAndMakeVisible (b);
    send.getProperties().set ("style", "primary");
    undoSend.getProperties().set ("led", "amber");
    journal.setClickingTogglesState (true);

    text.setMultiLine (true, true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setFont (mono (10.5f));
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::textColourId, mist);
    addAndMakeVisible (text);

    read.setTooltip (utf8 ("Relire ton set Live (après tes modifications)."));
    listen.setTooltip (utf8 ("Aperçu approximatif des nouvelles parties avec les sons de FINISH. Le vrai son : tes instruments, dans Live, après l'envoi."));
    previous.setTooltip (utf8 ("Revenir au plan d'avant (« garde la version précédente »)."));
    send.setTooltip (utf8 ("Écrit le plan dans ton set, sur tes pistes. Rien d'existant n'est modifié. Confirmation demandée."));
    undoSend.setTooltip (utf8 ("Retire de Live tout ce que le dernier envoi a ajouté."));
    journal.setTooltip (utf8 ("Ce qui a été analysé, généré, envoyé, et ce qu'il reste à faire."));

    read.onClick = [this] { proc.readLiveProject(); if (onChange) onChange(); };
    listen.onClick = [this] { proc.previewProject (-1, false); if (onChange) onChange(); };
    stop.onClick = [this] { proc.stopPreview(); if (onChange) onChange(); };
    previous.onClick = [this] { proc.editProject ([] (ProjectSession& s) { s.undoPlan(); }); if (onChange) onChange(); };
    send.onClick = [this] { confirmAndSend(); };
    undoSend.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, utf8 ("Annuler l'envoi"),
            utf8 ("Retirer de Live tout ce que FINISH vient d'y écrire ?"), utf8 ("Retirer"), utf8 ("Garder"), this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) proc.undoProjectSend(); if (onChange) onChange(); }));
    };
    journal.onClick = [this] { showingJournal = journal.getToggleState(); shownAnswer = -1; refresh(); };
}

void ProjectConsole::confirmAndSend()
{
    const auto& prj = proc.getProject();
    if (! prj.hasPlan()) return;
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, utf8 ("Envoyer dans Live"), prj.confirmText(),
        utf8 ("Envoyer"), utf8 ("Pas encore"), this,
        juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) proc.sendProjectToLive(); if (onChange) onChange(); }));
}

void ProjectConsole::resized()
{
    text.setBounds (14, 10, 668, getHeight() - 20);
    const int x = 704;
    read.setBounds (x, 12, 140, 30);
    listen.setBounds (x + 146, 12, 120, 30);
    stop.setBounds (x + 272, 12, 70, 30);
    journal.setBounds (x + 348, 12, 74, 30);
    send.setBounds (x, 54, 214, 40);
    undoSend.setBounds (x + 222, 54, 200, 40);
    previous.setBounds (x, 106, 140, 26);
}

void ProjectConsole::refresh()
{
    const auto& prj = proc.getProject();
    const bool connected = prj.getDiagnosis().connected();
    const bool idle = prj.getPhase() == ProjectSession::Phase::Idle || prj.getPhase() == ProjectSession::Phase::Ready;
    read.setEnabled (connected && idle);
    listen.setEnabled (prj.hasPlan());
    stop.setEnabled (proc.isPreviewing());
    send.setEnabled (connected && idle && prj.hasPlan() && prj.getPlan().countWrites() > 0);
    undoSend.setEnabled (connected && idle && prj.hasSent());
    previous.setEnabled (prj.canUndoPlan());

    if (showingJournal)
    {
        juce::String j;
        static const char* kinds[] = { "ANALYSÉ", "GÉNÉRÉ", "ENVOYÉ", "ANNULÉ", "À FAIRE", "MASTER", "INFO", "ERREUR" };
        for (auto it = prj.getJournal().rbegin(); it != prj.getJournal().rend(); ++it)
            j << juce::Time ((juce::int64) (it->time * 1000.0)).formatted ("%H:%M:%S") << "  " << utf8 (kinds[(int) it->kind]).paddedRight (' ', 8) << "  " << it->text << "\n";
        if (j.isEmpty()) j = utf8 ("Journal vide.");
        if (text.getText() != j) text.setText (j, false);
    }
    else if (proc.getAnswerVersion() != shownAnswer)
    {
        shownAnswer = proc.getAnswerVersion();
        text.setText (proc.getAnswer().isNotEmpty() ? proc.getAnswer()
                                                    : utf8 ("Ici : ce que FINISH lit dans ton projet, son plan, et le compte rendu de chaque envoi.\n"
                                                            "Demande-lui ce que tu veux dans la barre du haut : « termine mon morceau sans toucher à ma mélodie », "
                                                            "« propose trois versions de mon build-up », « analyse mon mix »…"), false);
        text.moveCaretToTop (false);
    }
    repaint();
}

void ProjectConsole::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r.withWidth (684.0f).reduced (2.0f), 10.0f);
    auto right = r.withTrimmedLeft (692.0f);
    const auto& prj = proc.getProject();
    auto st = juce::Rectangle<float> (right.getX() + 150.0f, 104.0f, right.getWidth() - 156.0f, 54.0f);
    drawScreen (g, st, 4.0f, true);
    g.setFont (mono (8.0f));
    const auto status = prj.getStatus();
    g.setColour (prj.getPhase() == ProjectSession::Phase::Idle || prj.getPhase() == ProjectSession::Phase::Ready ? mist : cyan);
    g.drawFittedText (status, st.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 3, 0.9f);
}
