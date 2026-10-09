#include "Inspector.h"
#include "../Music/SectionDiagnosisEngine.h"

using namespace Night;

namespace
{
    const juce::String dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    juce::String targetText (const Variant& v, const std::vector<Section>& S)
    {
        if (! juce::isPositiveAndBelow (v.target.section, (int) S.size())) return {};
        const auto& s = S[(size_t) v.target.section];
        const auto [a, b] = v.target.resolve (S);
        switch (v.target.anchor)
        {
            case TargetSpec::Anchor::Head: return s.label + dot + "FIRST " + juce::String (juce::roundToInt (b - a)) + " s";
            case TargetSpec::Anchor::Tail:
            {
                const double len = b - a;
                return s.label + dot + "LAST " + (len < 1.0 ? juce::String (len, 2) : juce::String (juce::roundToInt (len))) + " s";
            }
            case TargetSpec::Anchor::Whole: break;
        }
        return s.label + dot + "WHOLE SECTION";
    }
}

// ======================================================================================
void VariantCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool lit = data.previewing || data.applied;
    const bool empty = data.name.isEmpty();
    const float alpha = data.refused ? 0.5f : 1.0f;

    // Rubber pad with backlight
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0, 2.5f), 9.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (hover && ! empty ? 0xff2c2140 : 0xff221933), 0, r.getY(),
                                             juce::Colour (0xff100b19), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 9.0f);
    if (data.previewing)
    {
        g.setGradientFill (aurora (r.getX(), 0, r.getRight(), 0, 0.22f));
        g.fillRoundedRectangle (r, 9.0f);
        for (int i = 3; i >= 1; --i)
        {
            g.setGradientFill (aurora (r.getX(), 0, r.getRight(), 0, 0.09f));
            g.drawRoundedRectangle (r.expanded ((float) i * 1.6f), 9.0f + (float) i, 2.5f);
        }
        g.setGradientFill (aurora (r.getX(), 0, r.getRight(), 0));
        g.drawRoundedRectangle (r, 9.0f, 1.6f);
    }
    else
    {
        g.setColour (data.applied ? mint.withAlpha (0.55f) : (data.selected ? lineHi.brighter (0.5f) : juce::Colours::black.withAlpha (0.6f)));
        g.drawRoundedRectangle (r, 9.0f, data.applied || data.selected ? 1.4f : 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 9.0f, r.getRight() - 9.0f);
    }

    // Letter
    const auto letterBox = juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 6.0f, 32.0f, 34.0f);
    const auto letterCol = data.applied ? mint : (data.previewing || data.selected ? cyan : violet);
    if (empty)
    {
        engraved (g, data.letter, letterBox, 18.0f, juce::Justification::centred, dusk.withAlpha (0.6f));
        return;
    }
    if (lit || data.selected || data.recommended) neonText (g, data.letter, display (22.0f, 0.0f), letterBox, letterCol, juce::Justification::centred, alpha);
    else engraved (g, data.letter, letterBox, 20.0f, juce::Justification::centred, mist.withMultipliedAlpha (alpha));

    // Name + target
    auto head = juce::Rectangle<float> (r.getX() + 44.0f, r.getY() + 9.0f, r.getWidth() - 54.0f, 16.0f);
    g.setColour (moon.withMultipliedAlpha (alpha));
    g.setFont (bold (12.5f));
    g.drawText (data.name, head, juce::Justification::centredLeft, true);
    g.setColour (dusk);
    g.setFont (mono (8.0f));
    g.drawText (data.target, head.translated (0, 16.0f).withHeight (11.0f), juce::Justification::centredLeft, true);

    // Status LED
    juce::String tag; juce::Colour tagColour = cyan;
    if (data.refused)          { tag = "DECLINED"; tagColour = rose; }
    else if (data.previewing)  { tag = "PREVIEW"; tagColour = cyan; }
    else if (data.applied)     { tag = "ON"; tagColour = mint; }
    else if (data.recommended) { tag = "BEST"; tagColour = amber; }
    if (tag.isNotEmpty())
    {
        drawLed (g, { r.getRight() - 12.0f, r.getY() + 13.0f }, 3.0f, tagColour, true);
        g.setColour (tagColour);
        g.setFont (label (6.5f));
        g.drawText (tag, juce::Rectangle<float> (r.getRight() - 84.0f, r.getY() + 7.0f, 64.0f, 12.0f), juce::Justification::centredRight);
    }

    // Description
    g.setColour ((data.refused ? rose : mist).withMultipliedAlpha (0.95f));
    g.setFont (text (10.5f));
    g.drawFittedText (data.refused ? data.reason : data.description,
                      juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 42.0f, r.getWidth() - 20.0f, 28.0f).toNearestInt(),
                      juce::Justification::topLeft, 2, 0.9f);

    // Relevance LED ladder
    const int leds = 12;
    const float lw = (r.getWidth() - 20.0f) / (float) leds;
    for (int i = 0; i < leds; ++i)
    {
        const bool on = ! data.refused && (float) (i + 1) / (float) leds <= data.score + 0.04f;
        auto cell = juce::Rectangle<float> (r.getX() + 10.0f + lw * (float) i, r.getBottom() - 9.0f, lw - 2.0f, 3.0f);
        g.setColour (on ? auroraAt ((float) i / (float) leds) : juce::Colour (0xff2a2140));
        g.fillRoundedRectangle (cell, 1.0f);
    }
}

// ======================================================================================
Inspector::Inspector (FINISHAudioProcessor& p) : proc (p)
{
    for (size_t i = 0; i < cards.size(); ++i)
    {
        addAndMakeVisible (cards[i]);
        cards[i].onClick = [this, i] { cardClicked ((int) i); };
    }
    for (auto* b : { &keepButton, &regenButton, &undoButton, &applyButton, &hearOriginal, &confirmButton, &changeButton, &linkButton })
        addAndMakeVisible (b);
    applyButton.getProperties().set ("style", "primary");
    hearOriginal.getProperties().set ("style", "chip");
    confirmButton.getProperties().set ("style", "chip");
    changeButton.getProperties().set ("style", "chip");
    linkButton.getProperties().set ("style", "link");
    hearOriginal.setClickingTogglesState (false);

    keepButton.setTooltip ("Keep this part exactly as it is: FINISH will not process it.");
    regenButton.setTooltip ("Ask for three other directions.");
    undoButton.setTooltip ("Go back to the previous state of the track map.");
    hearOriginal.setTooltip ("Hear this part untouched while the DAW plays it.");
    amountSlider.setTooltip ("Intensity of the selected direction.");
    confirmButton.setTooltip ("FINISH was not sure about this section. Confirm its type.");
    changeButton.setTooltip ("Change the type of this section.");

    amountSlider.setRange (0.0, 1.5, 0.01);
    amountSlider.setValue (1.0, juce::dontSendNotification);
    amountSlider.setDoubleClickReturnValue (true, 1.0);
    amountSlider.onValueChange = [this]
    {
        amount = (float) amountSlider.getValue();
        if (selection.kind == Selection::Kind::Track) return;
        const auto pv = proc.getTrack().getPreview();
        if (pv && pv->first == selection) proc.editTrack ([s = selection, v = pv->second, a = amount] (TrackEngine& e) { e.preview (s, v, a); });
        else if (proc.getTrack().editFor (selection) != nullptr) proc.editTrack ([s = selection, a = amount] (TrackEngine& e) { e.setAmount (s, a); });
        repaint();
    };
    addAndMakeVisible (amountSlider);

    keepButton.onClick = [this]
    {
        if (selection.kind == Selection::Kind::Track) { proc.resetMacros(); refresh(); return; }
        proc.editTrack ([s = selection] (TrackEngine& e) { e.keepOriginal (s); });
        selectedCard = -1;
        refresh();
    };
    regenButton.onClick = [this]
    {
        proc.editTrack ([s = selection] (TrackEngine& e) { e.regenerate (s); });
        selectedCard = -1;
        refresh();
    };
    undoButton.onClick = [this]
    {
        proc.editTrack ([] (TrackEngine& e) { e.undo(); });
        selectedCard = -1;
        refresh();
    };
    hearOriginal.onClick = [this]
    {
        const auto pv = proc.getTrack().getPreview();
        if (pv && pv->first == selection && pv->second < 0) proc.editTrack ([] (TrackEngine& e) { e.clearPreview(); });
        else proc.editTrack ([s = selection] (TrackEngine& e) { e.preview (s, -1); });
        refresh();
    };
    applyButton.onClick = [this]
    {
        if (selection.kind == Selection::Kind::Track)
        {
            if (selectedCard >= 0) proc.applyProposal (selectedCard);
        }
        else if (selectedCard >= 0)
            proc.editTrack ([s = selection, c = selectedCard, a = amount] (TrackEngine& e) { e.apply (s, c, a); });
        refresh();
    };
    confirmButton.onClick = [this]
    {
        proc.editTrack ([i = selection.index] (TrackEngine& e) { e.confirmSection (i); });
        refresh();
    };
    changeButton.onClick = [this] { showTypeMenu(); };
    linkButton.onClick = [this]
    {
        if (selection.kind != Selection::Kind::Section) return;
        const auto& v = proc.getTrack().getVerdicts();
        if (juce::isPositiveAndBelow (selection.index, (int) v.size()) && v[(size_t) selection.index].transitionRef >= 0 && onSelect)
            onSelect (Selection::transition (v[(size_t) selection.index].transitionRef));
    };
    refresh();
}

Inspector::Mode Inspector::mode() const
{
    if (! proc.getFeatures().valid && ! proc.getTrack().hasMap()) return Mode::Idle;
    if (proc.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Listening) return Mode::Idle;
    if (selection.kind == Selection::Kind::Section && juce::isPositiveAndBelow (selection.index, (int) proc.getMusic().sections.size())) return Mode::Section;
    if (selection.kind == Selection::Kind::Transition && juce::isPositiveAndBelow (selection.index, (int) proc.getTrack().getTransitions().size())) return Mode::Transition;
    return Mode::Track;
}

void Inspector::setSelection (Selection s)
{
    if (s != selection)
    {
        if (proc.getTrack().getPreview()) proc.editTrack ([] (TrackEngine& e) { e.clearPreview(); });
        selection = s;
        selectedCard = -1;
        const auto* e = proc.getTrack().editFor (s);
        amount = e != nullptr && ! e->keepOriginal ? e->amount : 1.0f;
        amountSlider.setValue (amount, juce::dontSendNotification);
    }
    refresh();
}

void Inspector::cardClicked (int i)
{
    const auto m = mode();
    if (m == Mode::Track)
    {
        const auto& props = proc.getDecision().proposals;
        if (i < (int) props.size() && ! props[(size_t) i].refused) selectedCard = i;
        refresh();
        return;
    }
    const auto* vs = proc.getTrack().variantsFor (selection);
    if (vs == nullptr || i >= (int) vs->size() || (*vs)[(size_t) i].refused) return;
    const auto pv = proc.getTrack().getPreview();
    if (pv && pv->first == selection && pv->second == i)
    {
        proc.editTrack ([] (TrackEngine& e) { e.clearPreview(); });   // second click: stop previewing
    }
    else
    {
        selectedCard = i;
        proc.editTrack ([s = selection, i, a = amount] (TrackEngine& e) { e.preview (s, i, a); });
    }
    refresh();
}

void Inspector::showTypeMenu()
{
    if (selection.kind != Selection::Kind::Section) return;
    const auto& S = proc.getMusic().sections;
    if (! juce::isPositiveAndBelow (selection.index, (int) S.size())) return;
    juce::PopupMenu m;
    m.addSectionHeader ("This section is a...");
    for (auto t : { SectionType::Intro, SectionType::Verse, SectionType::Chorus, SectionType::Build,
                    SectionType::Drop, SectionType::Break, SectionType::Bridge, SectionType::Outro })
        m.addItem ((int) t, sectionTypeName (t), true, t == S[(size_t) selection.index].type);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&changeButton),
                     [this, idx = selection.index] (int r)
                     {
                         if (r <= 0) return;
                         proc.editTrack ([idx, r] (TrackEngine& e) { e.setSectionType (idx, (SectionType) r); });
                         refresh();
                     });
}

void Inspector::refresh()
{
    const auto m = mode();
    const auto& engine = proc.getTrack();
    const auto& S = engine.getMusic().sections;
    const bool editable = m == Mode::Section || m == Mode::Transition;

    for (auto& c : cards) c.setVisible (true);
    for (auto* b : { &keepButton, &applyButton }) b->setVisible (m != Mode::Idle);
    regenButton.setVisible (editable);
    undoButton.setVisible (editable || (m == Mode::Track && engine.hasMap()));
    hearOriginal.setVisible (m == Mode::Section);
    amountSlider.setVisible (editable);
    undoButton.setEnabled (engine.canUndo());
    undoButton.setTopLeftPosition (574 + (editable ? 240 : 122), 126);

    const bool uncertain = m == Mode::Section && S[(size_t) selection.index].isUncertain();
    confirmButton.setVisible (uncertain);
    changeButton.setVisible (m == Mode::Section);

    linkButton.setVisible (false);
    if (m == Mode::Section)
    {
        const auto& v = engine.getVerdicts()[(size_t) selection.index];
        if (v.transitionRef >= 0 && v.transitionRef < (int) engine.getTransitions().size())
        {
            linkButton.setButtonText (u8 ("\xe2\x86\x92  ") + v.recommendation);
            linkButton.setVisible (true);
        }
    }

    keepButton.setButtonText (m == Mode::Track ? "RESET" : (m == Mode::Transition ? "KEEP AS IS" : "KEEP ORIG."));
    keepButton.setTooltip (m == Mode::Track ? "Return every global control to neutral."
                         : m == Mode::Transition ? "Leave this transition untouched (removes FINISH's edit on it)."
                                                 : "Keep this part exactly as it is: FINISH will not process it.");
    applyButton.setButtonText ("APPLY");

    const auto pv = engine.getPreview();
    hearOriginal.setToggleState (pv && pv->first == selection && pv->second < 0, juce::dontSendNotification);

    if (m == Mode::Idle)
    {
        const char* letters[] = { "A", "B", "C" };
        for (size_t i = 0; i < cards.size(); ++i)
        {
            CardData d;
            d.letter = letters[i];
            cards[i].setData (d);
        }
    }
    else if (m == Mode::Track)
    {
        const auto& props = proc.getDecision().proposals;
        for (size_t i = 0; i < cards.size(); ++i)
        {
            CardData d;
            if (i < props.size())
            {
                const auto& p = props[i];
                d.letter = p.title.substring (0, 1);
                d.name = p.title.substring (0, 1) + p.title.substring (1).toLowerCase();
                d.target = "WHOLE TRACK" + dot + "GLOBAL CONTROLS";
                d.description = p.description;
                d.reason = p.reason;
                d.score = p.confidence;
                d.refused = p.refused;
                d.recommended = i == 0 && ! p.refused;
                d.applied = proc.getAppliedStrategy() == p.title;
                d.selected = selectedCard == (int) i;
            }
            cards[i].setData (d);
        }
        applyButton.setEnabled (selectedCard >= 0);
    }
    else if (editable)
    {
        const auto* vs = engine.variantsFor (selection);
        const auto* applied = engine.editFor (selection);
        for (size_t i = 0; i < cards.size(); ++i)
        {
            CardData d;
            if (vs != nullptr && i < vs->size())
            {
                const auto& v = (*vs)[i];
                d.letter = juce::String::charToString ((juce::juce_wchar) v.letter);
                d.name = v.name;
                d.target = targetText (v, S);
                d.description = v.description;
                d.reason = v.reason;
                d.score = v.score;
                d.refused = v.refused;
                d.recommended = v.recommended;
                d.applied = applied != nullptr && ! applied->keepOriginal && applied->variant.name == v.name && applied->variant.letter == v.letter;
                d.previewing = pv && pv->first == selection && pv->second == (int) i;
                d.selected = selectedCard == (int) i;
            }
            cards[i].setData (d);
        }
        applyButton.setEnabled (selectedCard >= 0 && vs != nullptr && selectedCard < (int) vs->size() && ! (*vs)[(size_t) selectedCard].refused);
    }
    repaint();
}

void Inspector::resized()
{
    const int px = 574, pw = getWidth() - px - 6;
    const int cardW = (pw - 16) / 3;
    for (size_t i = 0; i < cards.size(); ++i)
        cards[i].setBounds (px + (int) i * (cardW + 8), 20, cardW, 92);
    hearOriginal.setBounds (getWidth() - 6 - 108, 0, 108, 18);
    keepButton.setBounds (px, 126, 116, 32);
    regenButton.setBounds (px + 122, 126, 112, 32);
    undoButton.setBounds (px + 240, 126, 76, 32);
    amountSlider.setBounds (px + 322, 132, 76, 22);
    applyButton.setBounds (getWidth() - 6 - 110, 126, 110, 32);
    confirmButton.setBounds (392, 38, 70, 20);
    changeButton.setBounds (468, 38, 76, 20);
    linkButton.setBounds (228, 132, 318, 18);
}

// ---- Paint --------------------------------------------------------------------------------
void Inspector::paintDNA (juce::Graphics& g, juce::Rectangle<float> r, const SectionDNA& d)
{
    const std::pair<const char*, float> bars[] = { { "NRG", d.nEnergy }, { "DENS", d.nDensity }, { "PUNCH", d.nPunch },
                                                   { "LOW", d.nLow }, { "AIR", d.nAir }, { "WIDTH", d.nWidth } };
    const float bw = r.getWidth() / 6.0f;
    const int segs = 9;
    for (int i = 0; i < 6; ++i)
    {
        auto col = juce::Rectangle<float> (r.getX() + bw * (float) i, r.getY(), bw, r.getHeight());
        auto ladder = col.withSizeKeepingCentre (14.0f, col.getHeight() - 16.0f).withY (col.getY());
        const float sh = ladder.getHeight() / (float) segs;
        const float v = juce::jlimit (0.04f, 1.0f, bars[i].second);
        for (int k = 0; k < segs; ++k)
        {
            const bool on = (float) (k + 1) / (float) segs <= v + 0.05f;
            auto cell = juce::Rectangle<float> (ladder.getX(), ladder.getBottom() - sh * (float) (k + 1) + 1.0f, ladder.getWidth(), sh - 2.0f);
            const auto c = auroraAt ((float) k / (float) (segs - 1));
            if (on)
            {
                g.setColour (c.withAlpha (0.25f));
                g.fillRoundedRectangle (cell.expanded (2.0f, 1.0f), 2.0f);
                g.setColour (c);
            }
            else g.setColour (juce::Colour (0xff1e1630));
            g.fillRoundedRectangle (cell, 1.0f);
        }
        g.setColour (dusk);
        g.setFont (mono (7.5f));
        g.drawText (bars[i].first, col.withTop (col.getBottom() - 11.0f), juce::Justification::centred, false);
    }
}

void Inspector::paintTransitionMeter (juce::Graphics& g, juce::Rectangle<float> r, const Transition& t)
{
    const auto col = t.works ? mint : amber;
    g.setColour (dusk);
    g.setFont (mono (8.0f));
    g.drawText ("CONTRAST ACROSS THE BOUNDARY", r.withHeight (12.0f), juce::Justification::centredLeft);
    const juce::String v = (t.contrastDb >= 0 ? "+" : "-") + juce::String (std::abs (t.contrastDb), 1).paddedLeft ('0', 4);
    drawSevenSeg (g, juce::Rectangle<float> (r.getX(), r.getY() + 18.0f, 92.0f, 34.0f), v.retainCharacters ("0123456789-"), col);
    g.setColour (col);
    g.setFont (mono (9.0f));
    g.drawText ("dB", juce::Rectangle<float> (r.getX() + 94.0f, r.getY() + 38.0f, 20.0f, 14.0f), juce::Justification::centredLeft);
    auto viz = juce::Rectangle<float> (r.getX() + 120.0f, r.getY() + 16.0f, r.getWidth() - 124.0f, 44.0f);
    const float before = 0.55f, after = juce::jlimit (0.1f, 0.95f, before + t.contrastDb / 12.0f);
    const float mid = viz.getCentreX();
    juce::Path p;
    p.startNewSubPath (viz.getX(), viz.getBottom() - viz.getHeight() * before);
    p.lineTo (mid - 4.0f, viz.getBottom() - viz.getHeight() * before);
    p.lineTo (mid + 4.0f, viz.getBottom() - viz.getHeight() * after);
    p.lineTo (viz.getRight(), viz.getBottom() - viz.getHeight() * after);
    glowStroke (g, p, col, 1.6f, 0.8f);
    g.setColour (lineHi);
    g.drawVerticalLine ((int) mid, viz.getY(), viz.getBottom());
}

void Inspector::paintTrackSummary (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& f = proc.getFeatures();
    const std::pair<const char*, juce::String> rows[] = {
        { "LOUDNESS", juce::String (f.integratedLufs, 1) + " LUFS" }, { "PLR", juce::String (f.plr, 1) + " dB" },
        { "WIDTH", juce::String (juce::roundToInt (f.sideRatio * 100.0f)) + " %" }, { "PUNCH", juce::String (f.punchIndex, 1) + " dB" } };
    const float rh = r.getHeight() / 2.0f, cw = r.getWidth() / 2.0f;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + cw * (float) (i % 2), r.getY() + rh * (float) (i / 2), cw, rh);
        g.setColour (dusk);
        g.setFont (mono (8.0f));
        g.drawText (rows[i].first, cell.removeFromTop (12.0f), juce::Justification::centredLeft);
        g.setColour (cyan);
        g.setFont (mono (13.0f));
        g.drawText (rows[i].second, cell, juce::Justification::topLeft);
    }
}

void Inspector::paint (juce::Graphics& g)
{
    const auto lcd = juce::Rectangle<float> (6.0f, 8.0f, 552.0f, 152.0f);
    drawScreen (g, lcd, 8.0f, true);
    const auto m = mode();
    const auto& engine = proc.getTrack();

    // Faceplate engravings around the pads
    engraved (g, "THREE DIRECTIONS  A / B / C", { 574.0f, 2.0f, 300.0f, 12.0f }, 6.5f, juce::Justification::centredLeft);
    if (amountSlider.isVisible())
    {
        engraved (g, "AMOUNT", { 896.0f, 118.0f, 76.0f, 10.0f }, 6.0f);
        g.setColour (cyan);
        g.setFont (mono (9.5f));
        g.drawText (juce::String (juce::roundToInt (amount * 100.0f)) + "%", juce::Rectangle<float> (974.0f, 126.0f, 36.0f, 32.0f), juce::Justification::centred);
    }

    g.saveState();
    g.reduceClipRegion (lcd.toNearestInt());

    auto statusPill = [&] (const juce::String& t, juce::Colour c)
    {
        const float tw = juce::GlyphArrangement::getStringWidth (mono (8.5f), t) + 26.0f;
        auto pill = juce::Rectangle<float> (lcd.getRight() - 12.0f - tw, 14.0f, tw, 18.0f);
        drawLed (g, { pill.getX() + 8.0f, pill.getCentreY() }, 2.6f, c, true);
        g.setColour (c);
        g.setFont (mono (8.5f));
        g.drawText (t, pill.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
    };
    auto body = [&] (const juce::String& headline, const juce::String& explanation)
    {
        g.setColour (moon);
        g.setFont (bold (12.5f));
        g.drawFittedText (headline, juce::Rectangle<float> (228.0f, 64.0f, 316.0f, 18.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (mist);
        g.setFont (text (11.0f));
        g.drawFittedText (explanation, juce::Rectangle<float> (228.0f, 84.0f, 316.0f, 46.0f).toNearestInt(), juce::Justification::topLeft, 3, 0.92f);
    };
    auto header = [&] (const juce::String& title, juce::Colour c, const juce::String& sub, juce::Colour subCol)
    {
        neonText (g, title, display (15.0f, 0.08f), { 22.0f, 14.0f, 360.0f, 22.0f }, c, juce::Justification::centredLeft, 0.8f);
        g.setColour (subCol);
        g.setFont (mono (9.0f));
        g.drawText (sub, juce::Rectangle<float> (22.0f, 39.0f, 364.0f, 14.0f), juce::Justification::centredLeft, true);
    };

    if (m == Mode::Idle)
    {
        header ("FINISH MY TRACK", violet, "STANDBY", dusk);
        const char* caps[] = { "WHOLE TRACK      understands the song before touching it",
                               "SECTION DNA      measures every part and its role",
                               "TRANSITIONS      judges how sections hand over",
                               "A / B / C        three real directions per part",
                               "COHERENCE        preview first, keep the track one piece" };
        float y = 62.0f;
        for (auto* c : caps)
        {
            g.setColour (cyan.withAlpha (0.8f));
            g.setFont (mono (9.5f));
            g.drawText (c, juce::Rectangle<float> (22.0f, y, 520.0f, 14.0f), juce::Justification::centredLeft);
            y += 17.0f;
        }
    }
    else if (m == Mode::Track)
    {
        header ("WHOLE TRACK", cyan, u8 ("GLOBAL CONTROLS \xc2\xb7 PUNCH / WIDE / MASSIVE"), dusk);
        const int score = proc.getScore();
        statusPill ("SCORE " + juce::String (score), score >= 85 ? mint : (score >= 65 ? amber : rose));
        paintTrackSummary (g, { 22.0f, 64.0f, 196.0f, 80.0f });
        const auto& d = proc.getDiagnoses();
        auto warnings = engine.coherenceWarnings (proc.getCoherence());
        const auto& notes = proc.getDecision().notes;
        juce::String headline = d.empty() ? juce::String ("Nothing major is holding this track back.")
                                          : d.front().title.substring (0, 1) + d.front().title.substring (1).toLowerCase() + ".";
        juce::String expl = ! warnings.isEmpty() ? warnings[0] : (! notes.isEmpty() ? notes[0] : (d.empty() ? juce::String ("Pick a section on the map to refine it.") : d.front().explanation));
        body (headline, expl);
    }
    else if (m == Mode::Section)
    {
        const auto& s = engine.getMusic().sections[(size_t) selection.index];
        const auto& dna = engine.getDNA()[(size_t) selection.index];
        const auto& v = engine.getVerdicts()[(size_t) selection.index];
        const auto pv = engine.getPreview();
        juce::String sub = roleName (dna.role).toUpperCase() + u8 (" \xc2\xb7 ") + mmss (s.startTime) + "-" + mmss (s.endTime);
        juce::Colour subCol = dusk;
        if (pv && pv->first == selection) { sub = "PREVIEW: PLAY " + mmss (s.startTime) + "-" + mmss (s.endTime) + " IN YOUR DAW"; subCol = cyan; }
        else if (s.isUncertain()) { sub = "NOT SURE: " + sectionTypeName (s.type) + " OR " + sectionTypeName (s.alternative) + "?"; subCol = amber; }
        else sub << u8 (" \xc2\xb7 ") << juce::String (juce::roundToInt (s.confidence * 100.0f)) << "%";
        header (s.label, isPeakType (s.type) ? violet : cyan, sub, subCol);
        statusPill (SectionDiagnosisEngine::statusText (v.potential), v.potential < 0.25f ? mint : (v.potential < 0.5f ? amber : rose));
        paintDNA (g, { 18.0f, 62.0f, 196.0f, 88.0f }, dna);
        const auto dash = u8 ("\xe2\x80\x94 ");
        const auto head = v.headline.fromFirstOccurrenceOf (dash, false, false);
        body (head.isNotEmpty() ? head : v.headline,
              v.explanation + (v.transitionRef < 0 && v.recommendation.isNotEmpty() ? " " + v.recommendation : juce::String()));
    }
    else if (m == Mode::Transition)
    {
        const auto& t = engine.getTransitions()[(size_t) selection.index];
        const auto [a, b] = engine.rangeOf (selection);
        const auto pv = engine.getPreview();
        const bool previewing = pv && pv->first == selection;
        header (t.title, t.works ? mint : amber,
                previewing ? "PREVIEW: PLAY " + mmss (a) + "-" + mmss (b) + " IN YOUR DAW" : "TRANSITION " + mmss (a) + "-" + mmss (b),
                previewing ? cyan : dusk);
        statusPill (t.works ? "WORKS" : (t.severity > 0.6f ? "WEAK CONTRAST" : "CAN IMPROVE"), t.works ? mint : amber);
        paintTransitionMeter (g, { 22.0f, 64.0f, 196.0f, 80.0f }, t);
        body (t.headline, t.explanation + (t.works ? " Keep it, or explore a direction." : juce::String()));
    }

    g.restoreState();
    drawScreenGlass (g, lcd, 8.0f, 0.6f);
}
