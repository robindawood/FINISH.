#include "PresetBrowser.h"
#include "../PluginProcessor.h"

using namespace Theme;

namespace
{
    juce::StringArray catNames()
    {
        juce::StringArray c { "TOUS" };
        for (int s = 1; s < numStyles; ++s) c.add (styleShortNames()[s]);
        c.add ("FAVORIS");
        c.add ("MES PRESETS");
        return c;
    }
    constexpr float rowH = 62.0f;
}

PresetBrowser::PresetBrowser (WhaleAudioProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty (fr ("Rechercher un preset…"), dim);
    search.setFont (body (14.0f));
    search.setIndents (30, 7);
    search.onTextChange = [this] { scroll = 0; rebuild(); repaint(); };
    addAndMakeVisible (search);
    rebuild();
}

void PresetBrowser::refresh()
{
    proc.presets.refreshUserPresets();
    const int cur = proc.presets.getCurrent();
    if (juce::isPositiveAndBelow (cur, proc.presets.getAll().size()))
        category = proc.presets.getAll()[cur].factory ? proc.presets.getAll()[cur].style : 8;
    rebuild();
    repaint();
}

juce::Rectangle<float> PresetBrowser::panel() const
{
    const auto b = getLocalBounds().toFloat();
    const float w = juce::jmin (980.0f, b.getWidth() - 80.0f);
    return { b.getCentreX() - w * 0.5f, 6.0f, w, b.getHeight() - 16.0f };
}

juce::Rectangle<float> PresetBrowser::catBounds (int i) const
{
    const auto p = panel();
    return { p.getX() + 18, p.getY() + 72 + (float) i * 38.0f + (i >= 7 ? 12.0f : 0.0f), 200, 32 };
}

juce::Rectangle<float> PresetBrowser::rowBounds (int vi) const
{
    const auto p = panel();
    return { p.getX() + 240, p.getY() + 70 + (float) (vi - scroll) * rowH, p.getWidth() - 258, rowH - 6 };
}

juce::Rectangle<float> PresetBrowser::saveBounds() const
{
    const auto p = panel();
    return { p.getX() + 18, p.getBottom() - 46, 210, 32 };
}

void PresetBrowser::resized()
{
    const auto p = panel();
    search.setBounds (juce::Rectangle<float> (p.getX() + 18, p.getY() + 18, p.getWidth() - 36, 38).toNearestInt());
}

void PresetBrowser::rebuild()
{
    visible.clear();
    const auto q = search.getText().trim();
    const auto& all = proc.presets.getAll();
    for (int i = 0; i < all.size(); ++i)
    {
        const auto& pr = all[i];
        bool ok = true;
        if (category >= 1 && category <= 6) ok = pr.factory && pr.style == category;
        else if (category == 7) ok = proc.presets.isFavourite (pr.name);
        else if (category == 8) ok = ! pr.factory;
        if (ok && q.isNotEmpty()) ok = pr.name.containsIgnoreCase (q) || pr.description.containsIgnoreCase (q);
        if (ok) visible.add (i);
    }
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0x99040209));
    const auto p = panel();
    g.setColour (juce::Colour (0xf70c0820));
    g.fillRoundedRectangle (p, 16.0f);
    g.setColour (juce::Colour (0xff6e50be));
    g.drawRoundedRectangle (p, 16.0f, 1.4f);
    // search icon
    const auto sb = search.getBounds().toFloat();
    g.setColour (dim);
    g.drawEllipse (sb.getX() + 10, sb.getCentreY() - 7, 11, 11, 1.8f);
    g.drawLine (sb.getX() + 19, sb.getCentreY() + 3, sb.getX() + 24, sb.getCentreY() + 8, 1.8f);

    const auto cats = catNames();
    for (int i = 0; i < cats.size(); ++i)
    {
        const auto r = catBounds (i);
        if (i == category) { g.setColour (juce::Colour (0xff36186a)); g.fillRoundedRectangle (r, 8.0f); }
        if (i == 7)
        {
            g.setColour (line);
            g.drawHorizontalLine ((int) r.getY() - 7, r.getX() + 6, r.getRight() - 6);
            juce::Path star; star.addStar ({ r.getX() + 16, r.getCentreY() }, 5, 3.5f, 7.5f);
            g.setColour (magenta); g.fillPath (star);
        }
        g.setColour (i == category ? juce::Colours::white : juce::Colour (0xffaa9fd2));
        g.setFont (bold (13.5f, 0.03f));
        g.drawText (cats[i], r.withTrimmedLeft (i == 7 ? 32.0f : 14.0f), juce::Justification::centredLeft);
    }
    g.setColour (line);
    g.drawVerticalLine ((int) p.getX() + 230, p.getY() + 70, p.getBottom() - 60);

    const auto& all = proc.presets.getAll();
    const int cur = proc.presets.getCurrent();
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (juce::Rectangle<float> (p.getX() + 236, p.getY() + 66, p.getWidth() - 250, p.getHeight() - 132).toNearestInt());
    for (int vi = 0; vi < visible.size(); ++vi)
    {
        const auto r = rowBounds (vi);
        if (r.getBottom() < p.getY() + 60 || r.getY() > p.getBottom()) continue;
        const auto& pr = all[visible[vi]];
        const bool sel = visible[vi] == cur;
        if (sel || vi == hoverRow)
        {
            g.setColour (sel ? juce::Colour (0xff2c1458) : juce::Colour (0xff181030));
            g.fillRoundedRectangle (r, 10.0f);
            if (sel) { g.setColour (magenta); g.drawRoundedRectangle (r, 10.0f, 1.4f); }
        }
        g.setColour (text);
        g.setFont (bold (16.0f));
        g.drawText (pr.name, r.withTrimmedLeft (18).withHeight (r.getHeight() * 0.55f).translated (0, 4), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (body (13.0f));
        g.drawText (pr.description + (pr.factory ? juce::String() : fr ("  ·  à toi")), r.withTrimmedLeft (18).withTrimmedTop (r.getHeight() * 0.52f).withTrimmedRight (200),
                    juce::Justification::centredLeft);
        // target chip
        const juce::Rectangle<float> chip (r.getRight() - 170, r.getCentreY() - 13, 110, 26);
        g.setColour (lineHi);
        g.drawRoundedRectangle (chip, 6.0f, 1.0f);
        g.setColour (text);
        g.setFont (mono (11.0f));
        const juce::String tg = pr.target == styleTarget ? juce::String::formatted ("%.1f LUFS", (double) AutoMaster::profile (pr.style).lufs_i.median)
                                                         : juce::String (loudnessTargets[pr.target].shortName);
        g.drawText (tg, chip, juce::Justification::centred);
        juce::Path star; star.addStar ({ r.getRight() - 30, r.getCentreY() }, 5, 4.0f, 9.0f);
        if (proc.presets.isFavourite (pr.name)) { g.setColour (magenta); g.fillPath (star); }
        else { g.setColour (faint); g.strokePath (star, juce::PathStrokeType (1.2f)); }
    }
    if (visible.isEmpty())
    {
        g.setColour (dim);
        g.setFont (body (14.0f));
        g.drawText (category == 8 ? fr ("Pas encore de preset à toi : règle WHALE puis « Sauver mon preset ».") : fr ("Aucun preset ici."),
                    juce::Rectangle<float> (p.getX() + 250, p.getY() + 90, p.getWidth() - 280, 30), juce::Justification::centredLeft);
    }
    g.restoreState();

    // footer
    g.setColour (line);
    g.drawHorizontalLine ((int) p.getBottom() - 60, p.getX() + 18, p.getRight() - 18);
    const auto s = saveBounds();
    g.setColour (juce::Colour (0xff36186a));
    g.fillRoundedRectangle (s, 16.0f);
    g.setColour (magenta);
    g.drawRoundedRectangle (s, 16.0f, 1.2f);
    g.setColour (juce::Colours::white);
    g.setFont (bold (13.0f, 0.04f));
    g.drawText (fr ("+ SAUVER MON PRESET"), s, juce::Justification::centred);
    g.setColour (dim);
    g.setFont (body (13.0f));
    g.drawText (fr ("Astuce : choisis un preset puis AUTO pour l'adapter à ton morceau.   Échap pour fermer"),
                juce::Rectangle<float> (s.getRight() + 20, s.getY(), p.getRight() - s.getRight() - 40, s.getHeight()), juce::Justification::centredRight);
}

void PresetBrowser::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int vi = 0; vi < visible.size(); ++vi)
        if (rowBounds (vi).contains (e.position)) h = vi;
    if (h != hoverRow) { hoverRow = h; repaint(); }
}

void PresetBrowser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int maxRows = (int) ((panel().getHeight() - 132) / rowH);
    scroll = juce::jlimit (0, juce::jmax (0, visible.size() - maxRows), scroll + (w.deltaY < 0 ? 1 : -1));
    repaint();
}

void PresetBrowser::saveCurrent()
{
    auto* w = new juce::AlertWindow (fr ("Sauver mon preset"), fr ("Nom du preset :"), juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presets.getCurrentName() == "Init" ? fr ("Mon master") : proc.presets.getCurrentName() + fr (" (perso)"));
    w->addTextEditor ("desc", "", fr ("Description (facultatif) :"));
    w->addButton ("SAUVER", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("ANNULER", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBrowser> sp (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int result)
    {
        if (sp == nullptr || result != 1) return;
        auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty()) return;
        auto preset = sp->proc.presets.capture (sp->proc.state, name);
        preset.description = w->getTextEditorContents ("desc").trim();
        sp->proc.presets.saveUserPreset (preset);
        for (int i = 0; i < sp->proc.presets.getAll().size(); ++i)
            if (sp->proc.presets.getAll()[i].name == name && ! sp->proc.presets.getAll()[i].factory) sp->proc.presets.setCurrent (i);
        sp->category = 8;
        sp->rebuild();
        sp->repaint();
    }), true);
}

void PresetBrowser::mouseUp (const juce::MouseEvent& e)
{
    if (! panel().contains (e.position)) { if (onClose) onClose(); return; }
    if (saveBounds().contains (e.position)) { saveCurrent(); return; }
    const auto cats = catNames();
    for (int i = 0; i < cats.size(); ++i)
        if (catBounds (i).contains (e.position)) { category = i; scroll = 0; rebuild(); repaint(); return; }
    for (int vi = 0; vi < visible.size(); ++vi)
    {
        const auto r = rowBounds (vi);
        if (! r.contains (e.position)) continue;
        const auto& pr = proc.presets.getAll()[visible[vi]];
        if (e.position.x > r.getRight() - 50)
            proc.presets.setFavourite (pr.name, ! proc.presets.isFavourite (pr.name));
        else
        {
            proc.applyPreset (visible[vi]);
            if (e.getNumberOfClicks() > 1 && onClose) { onClose(); return; }
        }
        rebuild();
        repaint();
        return;
    }
}
