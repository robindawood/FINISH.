#include "TrackEngine.h"
#include "SectionDetector.h"
#include "SectionFeatureAnalyzer.h"
#include "TransitionAnalyzer.h"
#include "SectionDiagnosisEngine.h"
#include "../Diagnosis/DiagnosisEngine.h"

namespace
{
    const juce::String dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    constexpr size_t maxUndo = 64;
}

// ======================================================================================
// Track map

std::vector<AnalysisFrame> TrackEngine::buildGrid (const std::vector<AnalysisFrame>& frames, double& startTime)
{
    std::vector<AnalysisFrame> timed;
    for (auto& f : frames) if (f.hostTime >= 0.0) timed.push_back (f);

    // Host gave no song position (or barely): keep the order in which the audio was heard.
    if (timed.size() < frames.size() / 2 || timed.empty())
    {
        startTime = 0.0;
        return frames;
    }

    double t0 = timed.front().hostTime, t1 = t0;
    for (auto& f : timed) { t0 = std::min (t0, f.hostTime); t1 = std::max (t1, f.hostTime + f.seconds); }
    // Cap at 20 minutes of song time.
    t1 = std::min (t1, t0 + 20.0 * 60.0);
    const auto n = (size_t) std::max (1.0, std::ceil ((t1 - t0) / MusicState::frameSec));
    std::vector<AnalysisFrame> grid (n);
    for (auto& g : grid) g.seconds = (float) MusicState::frameSec;
    // Later frames win: replaying a passage refreshes it.
    for (auto& f : timed)
    {
        const auto i = (size_t) std::llround ((f.hostTime - t0) / MusicState::frameSec);
        if (i < n) { grid[i] = f; grid[i].seconds = (float) MusicState::frameSec; }
    }
    startTime = t0;
    return grid;
}

void TrackEngine::build (const std::vector<AnalysisFrame>& sessionFrames)
{
    double start = 0;
    grid = buildGrid (sessionFrames, start);
    music = SectionDetector().analyze (grid, start);
    edits.clear();
    undoStack.clear();
    generations.clear();
    previewState.reset();
    analyseStructure();
}

void TrackEngine::clear()
{
    const int v = version;
    *this = TrackEngine();
    version = v + 1;
}

void TrackEngine::analyseStructure()
{
    SectionDetector::relabel (music.sections);
    SectionDetector::updateLoudness (music);
    dna = SectionFeatureAnalyzer::analyze (grid, music);
    transitions = TransitionAnalyzer::analyze (grid, music, dna);
    verdicts = SectionDiagnosisEngine::analyze (music, dna, transitions);

    sectionVariants.clear();
    for (int i = 0; i < (int) music.sections.size(); ++i)
        sectionVariants.push_back (VariantGenerator::forSection (i, music, dna, generations[i]));
    for (auto& t : transitions)
        if (const int g = generations[1000 + t.index]; g != 0)
            TransitionAnalyzer::buildVariants (t, music, g);

    // Keep edit descriptions in sync with renamed sections.
    for (auto& e : edits)
    {
        const auto sel = e.key >= 1000 ? Selection::transition (e.key - 1000) : Selection::section (e.key);
        e.source = e.keepOriginal ? sourceName (sel, {}) : sourceName (sel, e.variant);
    }
    rankAll();
    changed();
}

void TrackEngine::setContext (const std::vector<Diagnosis>& globalDiagnoses, const IntentVector& in)
{
    intent = in;
    risks.stereoRisk = DiagnosisEngine::has (globalDiagnoses, DiagnosisType::StereoRisk);
    risks.squashed = DiagnosisEngine::has (globalDiagnoses, DiagnosisType::OverCompressed);
    rankAll();
    changed();
}

void TrackEngine::rankAll()
{
    for (size_t i = 0; i < sectionVariants.size() && i < verdicts.size(); ++i)
        VariantRanker::rank (sectionVariants[i], verdicts[i].wants, intent, risks, dna, verdicts[i].potential < 0.25f);
    for (auto& t : transitions)
        VariantRanker::rank (t.variants, t.wants, intent, risks, dna, t.works);
}

const std::vector<Variant>* TrackEngine::variantsFor (Selection s) const
{
    if (s.kind == Selection::Kind::Section && juce::isPositiveAndBelow (s.index, (int) sectionVariants.size()))
        return &sectionVariants[(size_t) s.index];
    if (s.kind == Selection::Kind::Transition && juce::isPositiveAndBelow (s.index, (int) transitions.size()))
        return &transitions[(size_t) s.index].variants;
    return nullptr;
}

// ======================================================================================
// Structure corrections

void TrackEngine::setSectionType (int s, SectionType type)
{
    if (! juce::isPositiveAndBelow (s, (int) music.sections.size())) return;
    pushUndo();
    auto& sec = music.sections[(size_t) s];
    if (sec.type != type) sec.alternative = sec.type;
    sec.type = type;
    sec.confirmed = true;
    generations.erase (s);
    // Edits on this section were written for its old role: drop them.
    edits.erase (std::remove_if (edits.begin(), edits.end(), [s] (const Edit& e) { return e.key == s; }), edits.end());
    previewState.reset();
    analyseStructure();
}

void TrackEngine::confirmSection (int s)
{
    if (! juce::isPositiveAndBelow (s, (int) music.sections.size())) return;
    music.sections[(size_t) s].confirmed = true;
    rankAll();
    changed();
}

bool TrackEngine::moveBoundary (int b, double t)
{
    auto& S = music.sections;
    if (! juce::isPositiveAndBelow (b, (int) S.size() - 1)) return false;
    const double minLen = 2.0;
    t = juce::jlimit (S[(size_t) b].startTime + minLen, S[(size_t) b + 1].endTime - minLen, t);
    if (std::abs (t - S[(size_t) b].endTime) < 0.05) return false;
    pushUndo();
    S[(size_t) b].endTime = t;
    S[(size_t) b + 1].startTime = t;
    S[(size_t) b].confirmed = S[(size_t) b + 1].confirmed = true;
    previewState.reset();
    analyseStructure();
    return true;
}

// ======================================================================================
// Edits

juce::String TrackEngine::sourceName (Selection s, const Variant& v) const
{
    juce::String where;
    if (s.kind == Selection::Kind::Section && juce::isPositiveAndBelow (s.index, (int) music.sections.size()))
        where = music.sections[(size_t) s.index].label;
    else if (s.kind == Selection::Kind::Transition && juce::isPositiveAndBelow (s.index, (int) transitions.size()))
        where = transitions[(size_t) s.index].title;
    if (v.name.isEmpty()) return where + dot + "Original";
    return where + dot + juce::String::charToString ((juce::juce_wchar) v.letter) + " " + v.name;
}

const TrackEngine::Edit* TrackEngine::editFor (Selection s) const
{
    const int key = keyFor (s);
    for (auto& e : edits) if (e.key == key) return &e;
    return nullptr;
}

void TrackEngine::pushUndo()
{
    undoStack.push_back ({ music.sections, edits });
    if (undoStack.size() > maxUndo) undoStack.erase (undoStack.begin());
    lastAmountKey = -1;
}

bool TrackEngine::undo()
{
    if (undoStack.empty()) return false;
    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    const bool structureChanged = entry.sections.size() != music.sections.size()
        || ! std::equal (entry.sections.begin(), entry.sections.end(), music.sections.begin(),
                         [] (const Section& a, const Section& b) { return a.type == b.type && std::abs (a.startTime - b.startTime) < 1.0e-6 && std::abs (a.endTime - b.endTime) < 1.0e-6; });
    music.sections = std::move (entry.sections);
    edits = std::move (entry.edits);
    previewState.reset();
    lastAmountKey = -1;
    if (structureChanged) analyseStructure();
    else changed();
    return true;
}

void TrackEngine::preview (Selection s, int v, float amount)
{
    const auto* vs = variantsFor (s);
    if (vs == nullptr || v >= (int) vs->size()) return;
    previewState = std::make_pair (s, v);
    previewAmount = juce::jlimit (0.0f, 1.5f, amount);
    changed();
}

void TrackEngine::clearPreview()
{
    if (! previewState) return;
    previewState.reset();
    changed();
}

void TrackEngine::apply (Selection s, int v, float amount)
{
    const auto* vs = variantsFor (s);
    if (vs == nullptr || ! juce::isPositiveAndBelow (v, (int) vs->size()) || (*vs)[(size_t) v].refused) return;
    pushUndo();
    const int key = keyFor (s);
    edits.erase (std::remove_if (edits.begin(), edits.end(), [key] (const Edit& e) { return e.key == key; }), edits.end());
    Edit e;
    e.key = key;
    e.variant = (*vs)[(size_t) v];
    e.amount = juce::jlimit (0.0f, 1.5f, amount);
    e.source = sourceName (s, e.variant);
    edits.push_back (e);
    previewState.reset();
    changed();
}

void TrackEngine::keepOriginal (Selection s)
{
    if (s.kind == Selection::Kind::Track) return;
    pushUndo();
    const int key = keyFor (s);
    edits.erase (std::remove_if (edits.begin(), edits.end(), [key] (const Edit& e) { return e.key == key; }), edits.end());
    if (s.kind == Selection::Kind::Section)
    {
        // Keep the section exactly as it was: no FINISH processing at all inside it.
        Edit e;
        e.key = key;
        e.keepOriginal = true;
        e.variant.target = { s.index, TargetSpec::Anchor::Whole, 0 };
        e.source = sourceName (s, {});
        edits.push_back (e);
    }
    previewState.reset();
    changed();
}

void TrackEngine::setAmount (Selection s, float amount)
{
    const int key = keyFor (s);
    for (auto& e : edits)
        if (e.key == key && ! e.keepOriginal)
        {
            if (lastAmountKey != key) { pushUndo(); lastAmountKey = key; }
            // pushUndo may have reallocated nothing in edits; e is still valid (no insertion).
            e.amount = juce::jlimit (0.0f, 1.5f, amount);
            changed();
            return;
        }
}

void TrackEngine::regenerate (Selection s)
{
    if (s.kind == Selection::Kind::Track) return;
    const int key = keyFor (s);
    ++generations[key];
    if (s.kind == Selection::Kind::Section && juce::isPositiveAndBelow (s.index, (int) sectionVariants.size()))
        sectionVariants[(size_t) s.index] = VariantGenerator::forSection (s.index, music, dna, generations[key]);
    else if (s.kind == Selection::Kind::Transition && juce::isPositiveAndBelow (s.index, (int) transitions.size()))
        TransitionAnalyzer::buildVariants (transitions[(size_t) s.index], music, generations[key]);
    previewState.reset();
    rankAll();
    changed();
}

void TrackEngine::removeAllEdits()
{
    if (edits.empty()) return;
    pushUndo();
    edits.clear();
    previewState.reset();
    changed();
}

std::vector<TrackEngine::Edit> TrackEngine::effectiveEdits() const
{
    auto out = edits;
    if (previewState)
    {
        const auto [sel, v] = *previewState;
        const int key = keyFor (sel);
        out.erase (std::remove_if (out.begin(), out.end(), [key] (const Edit& e) { return e.key == key; }), out.end());
        Edit e;
        e.key = key;
        if (v < 0)
        {
            if (sel.kind == Selection::Kind::Section)
            {
                e.keepOriginal = true;
                e.variant.target = { sel.index, TargetSpec::Anchor::Whole, 0 };
                out.push_back (e);
            }
        }
        else if (const auto* vs = variantsFor (sel); vs != nullptr && v < (int) vs->size())
        {
            e.variant = (*vs)[(size_t) v];
            e.amount = previewAmount;
            out.push_back (e);
        }
    }
    return out;
}

std::pair<double, double> TrackEngine::rangeOf (const Edit& e) const
{
    return e.variant.target.resolve (music.sections);
}

std::pair<double, double> TrackEngine::rangeOf (Selection s) const
{
    const auto& S = music.sections;
    if (s.kind == Selection::Kind::Section && juce::isPositiveAndBelow (s.index, (int) S.size()))
        return { S[(size_t) s.index].startTime, S[(size_t) s.index].endTime };
    if (s.kind == Selection::Kind::Transition && juce::isPositiveAndBelow (s.index, (int) S.size() - 1))
    {
        const double t = S[(size_t) s.index].endTime;
        return { std::max (S[(size_t) s.index].startTime, t - 8.0), std::min (S[(size_t) s.index + 1].endTime, t + 8.0) };
    }
    return { music.startTime, music.endTime() };
}

SectionPlan TrackEngine::compilePlan() const
{
    SectionPlan plan;
    double weighted[5] = {};
    for (const auto& e : effectiveEdits())
    {
        if (plan.count >= SectionPlan::capacity) break;
        const auto [a, b] = rangeOf (e);
        if (b <= a) continue;
        auto& p = plan.edits[(size_t) plan.count++];
        p.start = a; p.end = b;
        p.from = e.variant.from; p.to = e.variant.to;
        p.amount = e.amount;
        p.keepOriginal = e.keepOriginal;
        if (! e.keepOriginal)
        {
            const auto avg = SectionMacros::lerp (e.variant.from, e.variant.to, 0.5f).scaled (e.amount);
            const double w = b - a;
            weighted[0] += avg.impact * w; weighted[1] += avg.punch * w; weighted[2] += avg.width * w;
            weighted[3] += avg.warmth * w; weighted[4] += avg.loudness * w;
        }
    }
    const double dur = std::max (1.0, music.durationSec);
    plan.mean = { (float) (weighted[0] / dur), (float) (weighted[1] / dur), (float) (weighted[2] / dur),
                  (float) (weighted[3] / dur), (float) (weighted[4] / dur), 0.0f };
    return plan;
}

juce::StringArray TrackEngine::coherenceWarnings (float coherence) const
{
    juce::StringArray out;
    const auto plan = compilePlan();
    if (plan.count == 0) return out;
    const auto& S = music.sections;
    for (size_t i = 0; i + 1 < S.size(); ++i)
    {
        const auto a = plan.evaluate ((S[i].startTime + S[i].endTime) * 0.5, coherence);
        const auto b = plan.evaluate ((S[i + 1].startTime + S[i + 1].endTime) * 0.5, coherence);
        auto warn = [&] (const char* what, float d, float limit)
        {
            if (std::abs (d) > limit)
                out.add (juce::String ("Big ") + what + " jump between " + S[i].label + " and " + S[i + 1].label
                         + ". Raise COHERENCE or lower the amount.");
        };
        warn ("WIDTH", b.delta.width - a.delta.width, 0.22f);
        warn ("TONE", b.delta.warmth - a.delta.warmth, 0.22f);
        warn ("DENSITY", (b.delta.impact + b.delta.loudness) - (a.delta.impact + a.delta.loudness), 0.5f);
    }
    return out;
}

// ======================================================================================
// Persistence

namespace
{
    void writeMacros (juce::ValueTree& t, const char* prefix, const SectionMacros& m)
    {
        juce::String s;
        for (int i = 0; i < SectionMacros::numValues; ++i) s << juce::String (m.values()[i], 4) << " ";
        t.setProperty (prefix, s.trim(), nullptr);
    }
    SectionMacros readMacros (const juce::ValueTree& t, const char* prefix)
    {
        juce::StringArray parts;
        parts.addTokens (t[prefix].toString(), " ", "");
        SectionMacros m;
        for (int i = 0; i < SectionMacros::numValues && i < parts.size(); ++i) m.values()[i] = parts[i].getFloatValue();
        return m;
    }
    constexpr int floatsPerFrame = 12;
}

juce::ValueTree TrackEngine::toValueTree() const
{
    juce::ValueTree t ("TRACKMAP");
    if (! hasMap()) return t;
    t.setProperty ("start", music.startTime, nullptr);

    juce::MemoryBlock mb (grid.size() * floatsPerFrame * sizeof (float));
    auto* f = static_cast<float*> (mb.getData());
    for (size_t i = 0; i < grid.size(); ++i)
    {
        const auto& g = grid[i];
        const float v[floatsPerFrame] = { g.kPower, g.peak, g.midPow, g.sidePow, g.lowPow, g.highPow, g.lowSidePow,
                                          g.highSidePow, g.lPow, g.rPow, g.lrCross, g.onsetDb };
        std::copy (v, v + floatsPerFrame, f + i * floatsPerFrame);
    }
    t.setProperty ("grid", mb.toBase64Encoding(), nullptr);

    for (auto& s : music.sections)
    {
        juce::ValueTree st ("SECTION");
        st.setProperty ("start", s.startTime, nullptr);
        st.setProperty ("end", s.endTime, nullptr);
        st.setProperty ("type", (int) s.type, nullptr);
        st.setProperty ("alt", (int) s.alternative, nullptr);
        st.setProperty ("conf", s.confidence, nullptr);
        st.setProperty ("confirmed", s.confirmed, nullptr);
        t.appendChild (st, nullptr);
    }
    for (auto& [key, g] : generations)
    {
        juce::ValueTree gt ("GEN");
        gt.setProperty ("key", key, nullptr);
        gt.setProperty ("n", g, nullptr);
        t.appendChild (gt, nullptr);
    }
    for (auto& e : edits)
    {
        juce::ValueTree et ("EDIT");
        et.setProperty ("key", e.key, nullptr);
        et.setProperty ("amount", e.amount, nullptr);
        et.setProperty ("keep", e.keepOriginal, nullptr);
        et.setProperty ("letter", juce::String::charToString ((juce::juce_wchar) e.variant.letter), nullptr);
        et.setProperty ("name", e.variant.name, nullptr);
        et.setProperty ("desc", e.variant.description, nullptr);
        et.setProperty ("tag", e.variant.tag, nullptr);
        et.setProperty ("tsec", e.variant.target.section, nullptr);
        et.setProperty ("tanchor", (int) e.variant.target.anchor, nullptr);
        et.setProperty ("tlen", e.variant.target.seconds, nullptr);
        writeMacros (et, "from", e.variant.from);
        writeMacros (et, "to", e.variant.to);
        t.appendChild (et, nullptr);
    }
    return t;
}

void TrackEngine::fromValueTree (const juce::ValueTree& t)
{
    const int v = version;
    *this = TrackEngine();
    version = v + 1;
    if (! t.isValid() || ! t.hasProperty ("grid")) return;

    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (t["grid"].toString())) { changed(); return; }
    const size_t n = mb.getSize() / (floatsPerFrame * sizeof (float));
    const auto* f = static_cast<const float*> (mb.getData());
    grid.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        auto& g = grid[i];
        const float* x = f + i * floatsPerFrame;
        g.seconds = (float) MusicState::frameSec;
        g.kPower = x[0]; g.peak = x[1]; g.midPow = x[2]; g.sidePow = x[3]; g.lowPow = x[4]; g.highPow = x[5];
        g.lowSidePow = x[6]; g.highSidePow = x[7]; g.lPow = x[8]; g.rPow = x[9]; g.lrCross = x[10]; g.onsetDb = x[11];
    }

    music = SectionDetector().analyze (grid, (double) t["start"]);   // contour + defaults
    std::vector<Section> saved;
    for (auto st : t)
    {
        if (st.hasType ("SECTION"))
        {
            Section s;
            s.startTime = st["start"]; s.endTime = st["end"];
            s.type = (SectionType) (int) st["type"];
            s.alternative = (SectionType) (int) st["alt"];
            s.confidence = st["conf"];
            s.confirmed = st["confirmed"];
            saved.push_back (s);
        }
        else if (st.hasType ("GEN"))
        {
            generations[(int) st["key"]] = (int) st["n"];
        }
        else if (st.hasType ("EDIT"))
        {
            Edit e;
            e.key = st["key"];
            e.amount = st["amount"];
            e.keepOriginal = st["keep"];
            e.variant.letter = (char) st["letter"].toString()[0];
            e.variant.name = st["name"].toString();
            e.variant.description = st["desc"].toString();
            e.variant.tag = st["tag"].toString();
            e.variant.target.section = st["tsec"];
            e.variant.target.anchor = (TargetSpec::Anchor) (int) st["tanchor"];
            e.variant.target.seconds = st["tlen"];
            e.variant.from = readMacros (st, "from");
            e.variant.to = readMacros (st, "to");
            edits.push_back (e);
        }
    }
    if (! saved.empty()) music.sections = saved;
    analyseStructure();
}
