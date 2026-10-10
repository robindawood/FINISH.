#include "Composer.h"

namespace
{
    juce::uint32 nextSeed (juce::uint32& s) { s = s * 1664525u + 1013904223u; return s; }

    juce::String moodText (const SectionMood& m)
    {
        juce::StringArray t;
        if (m.darker > 0) t.add ("sombre");
        if (m.brighter > 0) t.add ("lumineux");
        if (m.melodic > 0) t.add (utf8 ("mélodique"));
        if (m.atmos > 0) t.add (utf8 ("atmosphérique"));
        if (m.intense > 0) t.add ("intense");
        if (m.progressive > 0) t.add ("progressif");
        if (m.minimal > 0) t.add ("minimal");
        return t.joinIntoString (", ");
    }

    void merge (SectionMood& a, const SectionMood& b)
    {
        a.darker = std::max (a.darker, b.darker); a.brighter = std::max (a.brighter, b.brighter);
        a.melodic = std::max (a.melodic, b.melodic); a.atmos = std::max (a.atmos, b.atmos);
        a.intense = std::max (a.intense, b.intense); a.progressive = std::max (a.progressive, b.progressive);
        a.minimal = std::max (a.minimal, b.minimal);
        if (b.darker > 0) a.brighter = 0;
        if (b.brighter > 0) a.darker = 0;
    }

    juce::String barsText (const GenSection& s)
    {
        return utf8 ("mesures ") + juce::String (s.startBar + 1) + utf8 ("–") + juce::String (s.startBar + s.bars);
    }
}

void Composer::setDNA (const MusicalDNA& dna, int userBarOfMaterial, juce::uint32 seed)
{
    if (dnaState.valid) pushUndo();
    dnaState = dna;
    userBar = std::max (0, userBarOfMaterial);
    seedCounter = seed == 0 ? 1u : seed;
    for (auto& p : proposals) p = {};
    generateProposals (nextSeed (seedCounter));
}

void Composer::generateProposals (juce::uint32 seed)
{
    if (! dnaState.valid) return;
    const auto old = proposals;
    for (int f = 0; f < numFlavours; ++f)
    {
        auto fresh = ArrangementGenerator::generate (dnaState, (Flavour) f, creativityValue, seed + (juce::uint32) f * 7919u);
        // Locked sections survive: same role, same occurrence.
        const auto& prev = old[(size_t) f];
        std::array<int, numRoles> seenPrev {};
        for (auto& ps : prev.sections)
        {
            const int occ = seenPrev[(size_t) ps.role]++;
            if (! ps.locked) continue;
            int seen = 0;
            for (auto& ns : fresh.sections)
                if (ns.role == ps.role && seen++ == occ) { ns = ps; break; }
        }
        ArrangementGenerator::refreshTransitions (fresh);
        proposals[(size_t) f] = fresh;
    }
    touched();
}

void Composer::selectFlavour (Flavour f)
{
    current = juce::jlimit (0, numFlavours - 1, (int) f);
    touched();
}

void Composer::rebuildUnlocked (Arrangement& arr, bool newSeeds)
{
    for (int i = 0; i < (int) arr.sections.size(); ++i)
        if (! arr.sections[(size_t) i].locked)
            ArrangementGenerator::buildSection (arr, i, dnaState, newSeeds ? nextSeed (seedCounter) : arr.sections[(size_t) i].seed);
}

void Composer::setCreativity (float c)
{
    c = juce::jlimit (0.0f, 1.0f, c);
    if (std::abs (c - creativityValue) < 1.0e-4f || ! hasArrangement()) { creativityValue = c; return; }
    pushUndo();
    creativityValue = c;
    auto& arr = proposals[(size_t) current];
    arr.creativity = c;
    rebuildUnlocked (arr, false);
    touched();
}

void Composer::regenerateSection (int index)
{
    auto& arr = proposals[(size_t) current];
    if (index < 0 || index >= (int) arr.sections.size()) return;
    pushUndo();
    arr.sections[(size_t) index].locked = false;
    ArrangementGenerator::buildSection (arr, index, dnaState, nextSeed (seedCounter));
    touched();
}

void Composer::applyToSection (int index, const SectionMood& mood, int bars)
{
    auto& arr = proposals[(size_t) current];
    if (index < 0 || index >= (int) arr.sections.size()) return;
    pushUndo();
    auto& s = arr.sections[(size_t) index];
    s.locked = false;
    merge (s.mood, mood);
    if (bars > 0 && ! s.isReference) s.bars = bars;
    ArrangementGenerator::buildSection (arr, index, dnaState, nextSeed (seedCounter));
    touched();
}

void Composer::toggleLock (int index)
{
    auto& arr = proposals[(size_t) current];
    if (index < 0 || index >= (int) arr.sections.size()) return;
    arr.sections[(size_t) index].locked = ! arr.sections[(size_t) index].locked;
    touched();
}

void Composer::overrideKey (int root, bool minor)
{
    if (! dnaState.valid) return;
    pushUndo();
    const int shift = ((root - dnaState.keyRoot) % 12 + 12) % 12;
    dnaState.keyRoot = ((root % 12) + 12) % 12;
    // Relative switch keeps the notes; a real transposition moves the material with it.
    const bool relative = (minor && ! dnaState.minor && shift == 9) || (! minor && dnaState.minor && shift == 3);
    if (! relative && shift != 0)
    {
        const int s = shift > 6 ? shift - 12 : shift;
        for (auto& r : dnaState.chordRoots) r = (r + shift) % 12;
        for (auto& n : dnaState.bassLine) n.pitch += s;
        for (auto& n : dnaState.melody) n.pitch += s;
    }
    dnaState.minor = minor;
    dnaState.keyOverridden = true;
    dnaState.keyConfidence = 1.0f;
    for (auto& p : proposals) rebuildUnlocked (p, false);
    touched();
}

void Composer::overrideStyle (Style s)
{
    if (! dnaState.valid) return;
    pushUndo();
    dnaState.style = s;
    dnaState.styleOverridden = true;
    dnaState.styleConfidence = 1.0f;
    for (auto& p : proposals) rebuildUnlocked (p, false);
    touched();
}

void Composer::overrideTempo (double bpm)
{
    if (! dnaState.valid || bpm < 40.0 || bpm > 250.0) return;
    pushUndo();
    dnaState.bpm = bpm;
    dnaState.tempoFromHost = true;
    for (auto& p : proposals) p.bpm = bpm;
    touched();
}

void Composer::pushUndo()
{
    undoStack.push_back ({ proposals, current });
    if (undoStack.size() > 30) undoStack.erase (undoStack.begin());
}

bool Composer::undo()
{
    if (undoStack.empty()) return false;
    proposals = undoStack.back().first;
    current = undoStack.back().second;
    undoStack.pop_back();
    touched();
    return true;
}

int Composer::findSection (Role role, int beforeDrop) const
{
    const auto& secs = proposals[(size_t) current].sections;
    auto isDrop = [] (Role r) { return r == Role::Drop || r == Role::Drop2; };
    if (role == Role::Build && beforeDrop > 0)
    {
        // the section right before the first drop / drop 2
        for (int i = 1; i < (int) secs.size(); ++i)
            if ((beforeDrop == 1 ? secs[(size_t) i].role == Role::Drop : secs[(size_t) i].role == Role::Drop2) && secs[(size_t) i - 1].role == Role::Build)
                return i - 1;
        return -1;
    }
    for (int i = 0; i < (int) secs.size(); ++i)
        if (secs[(size_t) i].role == role) return i;
    juce::ignoreUnused (isDrop);
    return -1;
}

int Composer::insertPositionFor (Role role, int beforeDrop) const
{
    const auto& secs = proposals[(size_t) current].sections;
    auto indexOf = [&] (Role r) { for (int i = 0; i < (int) secs.size(); ++i) if (secs[(size_t) i].role == r) return i; return -1; };
    switch (role)
    {
        case Role::Intro: return 0;
        case Role::Outro: return (int) secs.size();
        case Role::Build:
        {
            const int d = indexOf (beforeDrop == 2 ? Role::Drop2 : Role::Drop);
            return d >= 0 ? d : (int) secs.size();
        }
        case Role::Bridge:
        {
            // after the break, otherwise before the build that leads to drop 2
            const int br = indexOf (Role::Break);
            if (br >= 0) return br + 1;
            const int d2 = indexOf (Role::Drop2);
            return d2 > 0 ? d2 - 1 : (int) secs.size();
        }
        case Role::Break:
        {
            const int d = indexOf (Role::Drop);
            return d >= 0 ? d + 1 : (int) secs.size();
        }
        case Role::Drop2:
        {
            const int o = indexOf (Role::Outro);
            return o >= 0 ? o : (int) secs.size();
        }
        case Role::Drop:
        default: return (int) secs.size();
    }
}

Composer::CommandResult Composer::apply (const ComposeCommand& cmd)
{
    CommandResult res;
    res.masterTarget = cmd.masterTarget;
    res.soundRequest = cmd.soundRequest;
    res.soundFocus = cmd.soundFocus;
    res.finishTrack = cmd.finishTrack;
    const bool musical = ! cmd.actions.empty() || cmd.regenerateAll || cmd.flavour >= 0 || std::abs (cmd.creativityDelta) > 1.0e-6f;
    if (! musical) return res;
    if (! hasArrangement()) { res.needsMaterial = true; return res; }

    pushUndo();
    if (cmd.flavour >= 0) { current = cmd.flavour; res.done.add (juce::String ("Version ") + flavourName ((Flavour) cmd.flavour)); }
    if (std::abs (cmd.creativityDelta) > 1.0e-6f)
    {
        creativityValue = juce::jlimit (0.0f, 1.0f, creativityValue + cmd.creativityDelta);
        proposals[(size_t) current].creativity = creativityValue;
        rebuildUnlocked (proposals[(size_t) current], false);
        res.done.add (utf8 ("Créativité ") + juce::String (juce::roundToInt (creativityValue * 100.0f)) + " %");
    }
    if (cmd.regenerateAll)
    {
        for (auto& p : proposals) p.creativity = creativityValue;
        auto& arr = proposals[(size_t) current];
        rebuildUnlocked (arr, true);
        res.done.add (utf8 ("Nouvelles idées pour toutes les parties non verrouillées"));
    }

    auto& arr = proposals[(size_t) current];
    for (auto& a : cmd.actions)
    {
        int idx = findSection (a.role, a.beforeDrop);
        const bool exists = idx >= 0;
        const int bars = a.bars > 0 ? a.bars : (a.role == Role::Build || a.role == Role::Bridge ? 8 : 16);
        if (! exists && a.role != Role::Drop)
        {
            idx = ArrangementGenerator::insertSection (arr, insertPositionFor (a.role, a.beforeDrop), a.role, bars, dnaState, a.mood, nextSeed (seedCounter));
            const auto& s = arr.sections[(size_t) idx];
            res.done.add (s.name + utf8 (" créé (") + barsText (s) + (a.mood.any() ? ", " + moodText (a.mood) : juce::String()) + ")");
            continue;
        }
        if (! exists) continue;
        auto& s = arr.sections[(size_t) idx];
        s.locked = false;
        merge (s.mood, a.mood);
        if (a.bars > 0 && ! s.isReference) s.bars = a.bars;
        ArrangementGenerator::buildSection (arr, idx, dnaState, nextSeed (seedCounter));
        const auto& sb = arr.sections[(size_t) idx];
        if (sb.isReference)
            res.done.add (sb.name + utf8 (" : ") + (sb.mood.intense > 0 ? utf8 ("calque d'accents + punch") : utf8 ("inchangé (c'est le tien)")));
        else
            res.done.add (sb.name + utf8 (" refait (") + barsText (sb) + (sb.mood.any() ? ", " + moodText (sb.mood) : juce::String()) + ")");
    }
    touched();
    return res;
}

int Composer::suggestedStartBar() const
{
    const auto& arr = getArrangement();
    for (auto& s : arr.sections)
        if (s.role == Role::Drop) return std::max (0, userBar - s.startBar);
    return 0;
}

ArrangementExport::Result Composer::exportAll (double sampleRate)
{
    if (! hasArrangement()) { ArrangementExport::Result r; r.error = utf8 ("Pas encore d'arrangement."); return r; }
    if (lastExport.ok && lastExportVersion == version && lastExport.folder.isDirectory()) return lastExport;
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %Hh%M");
    auto folder = ArrangementExport::defaultRoot().getChildFile (stamp + " " + flavourName (getFlavour()) + " "
                                                                 + juce::String (juce::roundToInt (dnaState.bpm)) + "bpm " + dnaState.keyName());
    folder = folder.getNonexistentSibling (false);
    lastExport = ArrangementExport::exportAll (getArrangement(), dnaState, folder, sampleRate);
    lastExportVersion = lastExport.ok ? version : -1;
    return lastExport;
}

bool Composer::sendToLive (double sampleRate, juce::String& message)
{
    const auto exported = exportAll (sampleRate);
    if (! exported.ok) { message = exported.error; return false; }
    juce::String report;
    ArrangementExport::installLiveScript (report);
    juce::String id, error;
    if (! ArrangementExport::sendToLive (getArrangement(), dnaState, exported, suggestedStartBar(), true, id, error))
    {
        message = error;
        return false;
    }
    liveRequest = id;
    liveOk = false;
    liveSentAt = juce::Time::getMillisecondCounterHiRes();
    liveStatus = utf8 ("Envoi à Ableton Live...");
    message = liveStatus;
    return true;
}

bool Composer::pollLive()
{
    if (liveRequest.isEmpty()) return false;
    bool ok = false; juce::String msg; juce::StringArray warnings;
    if (ArrangementExport::readLiveResponse (liveRequest, ok, msg, warnings))
    {
        liveOk = ok;
        liveStatus = (ok ? utf8 ("LIVE ✓ ") : utf8 ("LIVE ✗ ")) + msg;
        if (! warnings.isEmpty()) liveStatus << "  (" << warnings[0] << (warnings.size() > 1 ? " +" + juce::String (warnings.size() - 1) : juce::String()) << ")";
        liveRequest = {};
        return true;
    }
    const double waited = (juce::Time::getMillisecondCounterHiRes() - liveSentAt) / 1000.0;
    if (waited > 4.0)
    {
        liveRequest = {};
        liveOk = false;
        liveStatus = ArrangementExport::isBridgeAlive()
            ? utf8 ("Live ne répond pas : le set est-il ouvert ? Les fichiers sont prêts dans le dossier FINISH (glisser-déposer).")
            : utf8 ("Active FINISH une fois dans Live : Réglages > Link, Tempo & MIDI > Control Surface > FINISH (redémarre Live si absent). "
                    "En attendant, glisse les fichiers depuis le dossier FINISH.");
        // leave the request in the inbox: Live will build it as soon as the script starts
        return true;
    }
    return false;
}

// ======================================================================================
// State

namespace
{
    juce::String notesToString (const std::vector<NoteEvent>& v)
    {
        juce::StringArray a;
        for (auto& n : v) a.add (juce::String (n.beat, 4) + ":" + juce::String (n.length, 4) + ":" + juce::String (n.pitch) + ":" + juce::String (n.velocity, 3));
        return a.joinIntoString (" ");
    }
    std::vector<NoteEvent> notesFromString (const juce::String& s)
    {
        std::vector<NoteEvent> v;
        juce::StringArray a; a.addTokens (s, " ", ""); a.removeEmptyStrings();
        for (auto& t : a)
        {
            juce::StringArray p; p.addTokens (t, ":", "");
            if (p.size() == 4) v.push_back ({ p[0].getDoubleValue(), p[1].getDoubleValue(), p[2].getIntValue(), p[3].getFloatValue() });
        }
        return v;
    }
    template <size_t N> juce::String arrToString (const std::array<float, N>& a)
    {
        juce::StringArray s; for (auto v : a) s.add (juce::String (v, 3)); return s.joinIntoString (" ");
    }
    template <size_t N> void arrFromString (std::array<float, N>& a, const juce::String& s)
    {
        juce::StringArray t; t.addTokens (s, " ", ""); t.removeEmptyStrings();
        for (size_t i = 0; i < N && (int) i < t.size(); ++i) a[i] = t[(int) i].getFloatValue();
    }
}

juce::ValueTree Composer::dnaToTree (const MusicalDNA& d)
{
    juce::ValueTree v ("DNA");
    v.setProperty ("valid", d.valid, nullptr);
    v.setProperty ("bpm", d.bpm, nullptr);
    v.setProperty ("bars", d.barsAnalysed, nullptr);
    v.setProperty ("hostTempo", d.tempoFromHost, nullptr);
    v.setProperty ("key", d.keyRoot, nullptr); v.setProperty ("minor", d.minor, nullptr);
    v.setProperty ("keyConf", d.keyConfidence, nullptr);
    v.setProperty ("altKey", d.altKeyRoot, nullptr); v.setProperty ("altMinor", d.altMinor, nullptr);
    v.setProperty ("chroma", arrToString (d.chroma), nullptr);
    v.setProperty ("kick", arrToString (d.kick), nullptr);
    v.setProperty ("snare", arrToString (d.snare), nullptr);
    v.setProperty ("hats", arrToString (d.hats), nullptr);
    v.setProperty ("swing", d.swing, nullptr);
    v.setProperty ("fotf", d.fourOnFloor, nullptr); v.setProperty ("half", d.halfTime, nullptr);
    juce::StringArray cr; for (size_t i = 0; i < d.chordRoots.size(); ++i) cr.add (juce::String (d.chordRoots[i]) + (d.chordMinor[i] ? "m" : ""));
    v.setProperty ("chords", cr.joinIntoString (" "), nullptr);
    v.setProperty ("harmConf", d.harmonyConfidence, nullptr);
    v.setProperty ("bassLine", notesToString (d.bassLine), nullptr); v.setProperty ("bassBars", d.bassBars, nullptr);
    v.setProperty ("melody", notesToString (d.melody), nullptr); v.setProperty ("melodyBars", d.melodyBars, nullptr);
    v.setProperty ("melConf", d.melodyConfidence, nullptr);
    v.setProperty ("energy", d.energy, nullptr); v.setProperty ("density", d.density, nullptr);
    v.setProperty ("brightness", d.brightness, nullptr); v.setProperty ("lufs", d.loudnessLufs, nullptr);
    v.setProperty ("hasDrums", d.hasDrums, nullptr); v.setProperty ("hasBass", d.hasBass, nullptr);
    v.setProperty ("hasChords", d.hasChords, nullptr); v.setProperty ("hasMelody", d.hasMelody, nullptr);
    v.setProperty ("style", (int) d.style, nullptr); v.setProperty ("styleConf", d.styleConfidence, nullptr);
    v.setProperty ("styleOv", d.styleOverridden, nullptr); v.setProperty ("keyOv", d.keyOverridden, nullptr);
    return v;
}

MusicalDNA Composer::dnaFromTree (const juce::ValueTree& v)
{
    MusicalDNA d;
    if (! v.isValid()) return d;
    d.valid = v.getProperty ("valid", false);
    d.bpm = v.getProperty ("bpm", 120.0);
    d.barsAnalysed = v.getProperty ("bars", 0.0);
    d.tempoFromHost = v.getProperty ("hostTempo", true);
    d.keyRoot = v.getProperty ("key", 9); d.minor = v.getProperty ("minor", true);
    d.keyConfidence = v.getProperty ("keyConf", 0.0f);
    d.altKeyRoot = v.getProperty ("altKey", 0); d.altMinor = v.getProperty ("altMinor", false);
    arrFromString (d.chroma, v.getProperty ("chroma").toString());
    arrFromString (d.kick, v.getProperty ("kick").toString());
    arrFromString (d.snare, v.getProperty ("snare").toString());
    arrFromString (d.hats, v.getProperty ("hats").toString());
    d.swing = v.getProperty ("swing", 0.0f);
    d.fourOnFloor = v.getProperty ("fotf", false); d.halfTime = v.getProperty ("half", false);
    juce::StringArray cr; cr.addTokens (v.getProperty ("chords").toString(), " ", ""); cr.removeEmptyStrings();
    for (auto& c : cr) { d.chordRoots.push_back (c.getIntValue()); d.chordMinor.push_back (c.endsWith ("m")); }
    d.harmonyConfidence = v.getProperty ("harmConf", 0.0f);
    d.bassLine = notesFromString (v.getProperty ("bassLine").toString()); d.bassBars = v.getProperty ("bassBars", 0);
    d.melody = notesFromString (v.getProperty ("melody").toString()); d.melodyBars = v.getProperty ("melodyBars", 0);
    d.melodyConfidence = v.getProperty ("melConf", 0.0f);
    d.energy = v.getProperty ("energy", 0.0f); d.density = v.getProperty ("density", 0.0f);
    d.brightness = v.getProperty ("brightness", 0.0f); d.loudnessLufs = v.getProperty ("lufs", -70.0f);
    d.hasDrums = v.getProperty ("hasDrums", false); d.hasBass = v.getProperty ("hasBass", false);
    d.hasChords = v.getProperty ("hasChords", false); d.hasMelody = v.getProperty ("hasMelody", false);
    d.style = (Style) juce::jlimit (0, numStyles - 1, (int) v.getProperty ("style", 1));
    d.styleConfidence = v.getProperty ("styleConf", 0.0f);
    d.styleOverridden = v.getProperty ("styleOv", false); d.keyOverridden = v.getProperty ("keyOv", false);
    return d;
}

juce::ValueTree Composer::toState() const
{
    juce::ValueTree v ("COMPOSE");
    v.setProperty ("current", current, nullptr);
    v.setProperty ("creativity", creativityValue, nullptr);
    v.setProperty ("seed", (juce::int64) seedCounter, nullptr);
    v.setProperty ("userBar", userBar, nullptr);
    v.appendChild (dnaToTree (dnaState), nullptr);
    for (int f = 0; f < numFlavours; ++f)
    {
        const auto& a = proposals[(size_t) f];
        juce::ValueTree p ("PROPOSAL");
        p.setProperty ("flavour", f, nullptr);
        p.setProperty ("paletteSeed", (juce::int64) a.paletteSeed, nullptr);
        p.setProperty ("creativity", a.creativity, nullptr);
        for (auto& s : a.sections)
        {
            juce::ValueTree t ("SECTION");
            t.setProperty ("role", (int) s.role, nullptr);
            t.setProperty ("bars", s.bars, nullptr);
            t.setProperty ("seed", (juce::int64) s.seed, nullptr);
            t.setProperty ("locked", s.locked, nullptr);
            t.setProperty ("reference", s.isReference, nullptr);
            t.setProperty ("mood", juce::String (s.mood.darker) + " " + juce::String (s.mood.brighter) + " " + juce::String (s.mood.melodic) + " "
                                   + juce::String (s.mood.atmos) + " " + juce::String (s.mood.intense) + " " + juce::String (s.mood.progressive) + " "
                                   + juce::String (s.mood.minimal), nullptr);
            p.appendChild (t, nullptr);
        }
        v.appendChild (p, nullptr);
    }
    return v;
}

void Composer::fromState (const juce::ValueTree& v)
{
    if (! v.hasType ("COMPOSE")) return;
    dnaState = dnaFromTree (v.getChildWithName ("DNA"));
    current = juce::jlimit (0, numFlavours - 1, (int) v.getProperty ("current", 0));
    creativityValue = v.getProperty ("creativity", 0.35f);
    seedCounter = (juce::uint32) (juce::int64) v.getProperty ("seed", 1);
    userBar = v.getProperty ("userBar", 0);
    for (auto& p : proposals) p = {};
    if (! dnaState.valid) { touched(); return; }
    for (auto p : v)
    {
        if (! p.hasType ("PROPOSAL")) continue;
        const int f = juce::jlimit (0, numFlavours - 1, (int) p.getProperty ("flavour", 0));
        Arrangement a;
        a.flavour = (Flavour) f;
        a.creativity = p.getProperty ("creativity", creativityValue);
        a.paletteSeed = (juce::uint32) (juce::int64) p.getProperty ("paletteSeed", 1);
        a.bpm = dnaState.bpm;
        for (auto t : p)
        {
            GenSection s;
            s.role = (Role) juce::jlimit (0, numRoles - 1, (int) t.getProperty ("role", 0));
            s.bars = juce::jlimit (1, 64, (int) t.getProperty ("bars", 8));
            s.seed = (juce::uint32) (juce::int64) t.getProperty ("seed", 1);
            s.locked = t.getProperty ("locked", false);
            s.isReference = t.getProperty ("reference", false);
            juce::StringArray m; m.addTokens (t.getProperty ("mood").toString(), " ", "");
            if (m.size() == 7) s.mood = { m[0].getFloatValue(), m[1].getFloatValue(), m[2].getFloatValue(), m[3].getFloatValue(),
                                          m[4].getFloatValue(), m[5].getFloatValue(), m[6].getFloatValue() };
            a.sections.push_back (s);
        }
        a.layout();
        a.summary = ArrangementGenerator::flavourSummary ((Flavour) f, dnaState);
        for (int i = 0; i < (int) a.sections.size(); ++i)
            ArrangementGenerator::buildSection (a, i, dnaState, a.sections[(size_t) i].seed);
        proposals[(size_t) f] = a;
    }
    touched();
}
