#include "ProjectAnalyzer.h"
#include "../Compose/ComposeTypes.h"
#include <map>
#include <set>

const char* secKindName (SecKind k)
{
    static const char* n[] = { "INTRO", "BUILD", "DROP", "BREAK", "BUILD 2", "DROP 2", "OUTRO", "MONTEE", "VIDE", "?" };
    return n[(int) k];
}

juce::String ExistingSection::label() const
{
    return juce::String (secKindName (kind)) + " " + juce::String (startBar + 1) + "-" + juce::String (startBar + bars);
}

juce::String ProjectAnalysis::keyName() const
{
    return pitchClassName (keyRoot) + (minor ? " MIN" : " MAJ");
}

int ProjectAnalysis::countRole (TrackRole r) const noexcept
{
    int n = 0;
    for (auto& t : tracks) if (t.usable && t.role == r) ++n;
    return n;
}

namespace ProjectAnalyzer
{
float energyWeight (TrackRole r)
{
    switch (r)
    {
        case TrackRole::Drums:  return 3.0f;
        case TrackRole::Bass:   return 2.5f;
        case TrackRole::Perc:   return 1.4f;
        case TrackRole::Chords: return 1.2f;
        case TrackRole::Lead:   return 1.4f;
        case TrackRole::Vocal:  return 1.2f;
        case TrackRole::Pad:    return 0.7f;
        case TrackRole::Fx:     return 0.4f;
        case TrackRole::Audio:  return 1.0f;
        case TrackRole::Other:  return 0.8f;
    }
    return 1.0f;
}

static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };

int scaleStep (int pitch, int steps, int keyRoot, bool minor)
{
    const int* sc = minor ? minorScale : majorScale;
    const int rel = ((pitch - keyRoot) % 12 + 12) % 12;
    int degree = 0;
    for (int d = 0; d < 7; ++d) if (sc[d] <= rel) degree = d;
    const int base = pitch - rel + sc[degree];             // snapped to the scale
    const int octave = (int) std::floor ((degree + steps) / 7.0);
    const int nd = ((degree + steps) % 7 + 7) % 7;
    return juce::jlimit (0, 127, base - sc[degree] + sc[nd] + 12 * octave);
}

// --------------------------------------------------------------------------------------
static juce::StringArray tokens (const juce::String& s)
{
    juce::StringArray t;
    juce::String cur;
    for (auto c : s.toLowerCase())
    {
        if (juce::CharacterFunctions::isLetterOrDigit (c)) cur += c;
        else { if (cur.isNotEmpty()) t.add (cur); cur.clear(); }
    }
    if (cur.isNotEmpty()) t.add (cur);
    return t;
}

static bool nameHas (const juce::StringArray& tok, std::initializer_list<const char*> words)
{
    for (auto& t : tok)
        for (auto* w : words)
        {
            const auto ww = utf8 (w);
            if (t == ww || (ww.length() >= 3 && t.startsWith (ww))) return true;
        }
    return false;
}

static void classify (LiveTrack& t, TrackProfile& prof, double bpb)
{
    std::array<float, numTrackRoles> score {};
    juce::StringArray why;
    auto add = [&] (TrackRole r, float s) { score[(size_t) r] += s; };
    const auto tok = tokens (t.name + " " + t.instrumentName());

    struct K { TrackRole r; std::initializer_list<const char*> w; };
    const K keys[] = {
        { TrackRole::Drums,  { "drum", "drums", "kit", "beat", "batterie", "kick", "bd", "breakbeat", "groove" } },
        { TrackRole::Perc,   { "perc", "percussion", "hat", "hats", "hh", "hihat", "shaker", "clap", "snare", "snap", "rim", "tom", "cymbal", "ride", "conga", "bongo", "tamb", "top", "tops" } },
        { TrackRole::Bass,   { "bass", "basse", "sub", "low", "808", "reese" } },
        { TrackRole::Chords, { "chord", "chords", "accord", "accords", "keys", "key", "piano", "stab", "rhodes", "organ", "orgue", "epiano", "guitar", "guitare" } },
        { TrackRole::Lead,   { "lead", "melody", "melodie", "mélodie", "arp", "pluck", "oboe", "hautbois", "flute", "flûte", "sax", "violin", "violon", "hook", "bell", "marimba", "trumpet", "whistle", "idea" } },
        { TrackRole::Pad,    { "pad", "nappe", "string", "strings", "cordes", "atmo", "ambient", "texture", "choir", "drone" } },
        { TrackRole::Fx,     { "fx", "sfx", "riser", "sweep", "impact", "noise", "uplifter", "downlifter", "whoosh", "reverse" } },
        { TrackRole::Vocal,  { "vocal", "vocals", "vox", "voice", "voix", "acapella", "chant", "chop" } },
    };
    for (auto& k : keys)
        if (nameHas (tok, k.w)) { add (k.r, 2.5f); why.add (utf8 ("nom")); }

    if (t.hasDrumRack())
    {
        bool kickPad = false;
        for (auto& d : t.devices)
            for (auto& p : d.pads)
                if (p.first == 35 || p.first == 36 || p.second.containsIgnoreCase ("kick") || p.second.containsIgnoreCase ("bd")) kickPad = true;
        if (score[(size_t) TrackRole::Perc] > score[(size_t) TrackRole::Drums] || ! kickPad) add (TrackRole::Perc, 3.0f);
        else add (TrackRole::Drums, 3.0f);
        why.add ("Drum Rack");
    }

    // Note statistics
    std::vector<int> pitches;
    double totalLen = 0.0, beats = 0.0;
    int onsets = 0, notes = 0;
    auto take = [&] (const LiveClip& c)
    {
        const auto content = LiveNotes::loopContent (c);
        beats += c.loopLength();
        double lastStart = -1.0;
        for (auto& n : content)
        {
            pitches.push_back (n.pitch);
            totalLen += n.length;
            ++notes;
            if (std::abs (n.start - lastStart) > 0.03) { ++onsets; lastStart = n.start; }
        }
    };
    for (auto& c : t.arrangement) if (c.midi) take (c);
    for (auto& c : t.session) if (c.midi) take (c);
    if (! pitches.empty())
    {
        std::sort (pitches.begin(), pitches.end());
        prof.medianPitch = (float) pitches[pitches.size() / 2];
        prof.polyphony = (float) notes / (float) juce::jmax (1, onsets);
        prof.meanLength = (float) (totalLen / notes);
        prof.notesPerBar = (float) (notes / juce::jmax (1.0, beats / bpb));
        if (! t.hasDrumRack())
        {
            if (prof.polyphony >= 2.2f)
            {
                if (prof.meanLength >= 2.0f) { add (TrackRole::Pad, 1.5f); add (TrackRole::Chords, 1.2f); }
                else add (TrackRole::Chords, 2.0f);
            }
            else if (prof.medianPitch < 50.0f) add (TrackRole::Bass, 2.0f);
            else if (prof.medianPitch < 55.0f && prof.polyphony < 1.3f) add (TrackRole::Bass, 1.0f);
            else if (prof.meanLength >= 3.0f) add (TrackRole::Pad, 1.0f);
            else add (TrackRole::Lead, 2.0f);
            why.add ("notes");
        }
    }
    if (! t.midi && score[(size_t) TrackRole::Perc] + score[(size_t) TrackRole::Drums] + score[(size_t) TrackRole::Bass] + score[(size_t) TrackRole::Lead]
                    + score[(size_t) TrackRole::Pad] + score[(size_t) TrackRole::Chords] + score[(size_t) TrackRole::Vocal] + score[(size_t) TrackRole::Fx] <= 0.0f)
        add (TrackRole::Audio, 1.0f);

    int best = (int) TrackRole::Other;
    float bestScore = 0.0f, total = 0.0f;
    // Ties: the order below prefers the more specific reading.
    const TrackRole order[] = { TrackRole::Drums, TrackRole::Perc, TrackRole::Bass, TrackRole::Chords, TrackRole::Lead, TrackRole::Pad,
                                TrackRole::Vocal, TrackRole::Fx, TrackRole::Audio, TrackRole::Other };
    for (auto r : order)
    {
        total += score[(size_t) r];
        if (score[(size_t) r] > bestScore + 1.0e-4f) { bestScore = score[(size_t) r]; best = (int) r; }
    }
    t.role = (TrackRole) best;
    t.roleConfidence = total > 0.0f ? bestScore / total : 0.0f;
    why.removeDuplicates (false);
    t.roleWhy = why.joinIntoString (" + ");
    prof.role = t.role;
    prof.confidence = t.roleConfidence;
    prof.why = t.roleWhy;
}

static void mapDrums (const LiveTrack& t, TrackProfile& prof, const LiveClip* material)
{
    DrumMap m;
    std::map<int, juce::String> padNames;
    for (auto& d : t.devices) for (auto& p : d.pads) padNames[p.first] = p.second.toLowerCase();
    std::map<int, int> count, onBeat, backBeat;
    if (material != nullptr)
        for (auto& n : LiveNotes::loopContent (*material))
        {
            ++count[n.pitch];
            const double f = n.start - std::floor (n.start);
            if (f < 0.04 || f > 0.96) ++onBeat[n.pitch];
            const int beat = (int) std::floor (n.start + 0.04) % 4;
            if ((beat == 1 || beat == 3) && (f < 0.04 || f > 0.96)) ++backBeat[n.pitch];
        }
    for (auto& [pitch, c] : count)
    {
        const auto nm = padNames.count (pitch) ? padNames[pitch] : juce::String();
        auto is = [&] (std::initializer_list<const char*> w) { for (auto* s : w) if (nm.contains (s)) return true; return false; };
        if (is ({ "kick", "bd", "bass drum" }) || (nm.isEmpty() && (pitch == 35 || pitch == 36))) m.kick.push_back (pitch);
        else if (is ({ "snare", "clap", "snap", "sd", "rim" }) || (nm.isEmpty() && pitch >= 37 && pitch <= 40)) m.snare.push_back (pitch);
        else if (is ({ "hat", "hh", "shaker", "ride" }) || (nm.isEmpty() && (pitch == 42 || pitch == 44 || pitch == 46 || pitch == 51 || pitch == 70))) m.hats.push_back (pitch);
        else if (is ({ "crash", "cymbal" }) || (nm.isEmpty() && (pitch == 49 || pitch == 57))) m.crash.push_back (pitch);
        else m.perc.push_back (pitch);
    }
    // No names and no GM layout: the pitch on every beat is the kick, the one on 2 and 4 the snare.
    if (m.kick.empty() && prof.role == TrackRole::Drums)
    {
        int best = -1, bestN = 0;
        for (auto& [p, n] : onBeat) if (n > bestN && (backBeat[p] < n)) { best = p; bestN = n; }
        if (best >= 0 && bestN >= 2)
        {
            m.kick.push_back (best);
            for (auto* v : { &m.snare, &m.hats, &m.perc }) v->erase (std::remove (v->begin(), v->end(), best), v->end());
        }
    }
    if (! m.snare.empty()) m.rollPitch = m.snare.front();
    else
    {
        int best = -1, bestN = 0;
        for (auto& [p, n] : count)
            if (std::find (m.kick.begin(), m.kick.end(), p) == m.kick.end() && n > bestN) { best = p; bestN = n; }
        m.rollPitch = best >= 0 ? best : (m.kick.empty() ? -1 : m.kick.front());
    }
    prof.drums = m;
}

// --------------------------------------------------------------------------------------
static void detectKey (const LiveProject& p, ProjectAnalysis& a)
{
    static const float majP[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float minP[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    std::array<double, 12> h {};
    std::array<double, 12> bassH {};
    int used = 0;
    for (auto& t : p.tracks)
    {
        if (t.role == TrackRole::Drums || t.role == TrackRole::Perc || t.hasDrumRack() || t.mute) continue;
        bool any = false;
        auto take = [&] (const LiveClip& c)
        {
            for (auto& n : LiveNotes::loopContent (c))
            {
                const double w = std::min (n.length, 4.0) * (0.4 + n.velocity / 127.0) * (t.role == TrackRole::Bass ? 1.5 : 1.0);
                h[(size_t) (n.pitch % 12)] += w;
                if (t.role == TrackRole::Bass) bassH[(size_t) (n.pitch % 12)] += w;
                any = true;
            }
        };
        for (auto& c : t.arrangement) if (c.midi) take (c);
        for (auto& c : t.session) if (c.midi) take (c);
        if (any) ++used;
    }
    double sum = 0.0;
    for (auto v : h) sum += v;
    if (sum <= 0.0)
    {
        if (p.rootNote >= 0) { a.keyRoot = p.rootNote % 12; a.minor = p.scale.containsIgnoreCase ("minor") || p.scale.containsIgnoreCase ("aeolian"); a.keyConfidence = 0.4f; a.keySource = "Live"; }
        else { a.keyConfidence = 0.0f; a.keySource = "-"; }
        return;
    }
    auto corr = [&] (const float* prof, int root)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += h[(size_t) ((i + root) % 12)]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = h[(size_t) ((i + root) % 12)] - mx, y = prof[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxy / std::sqrt (sxx * syy + 1e-12);
    };
    struct C { int root; bool minor; double s; };
    std::vector<C> cands;
    for (int r = 0; r < 12; ++r) { cands.push_back ({ r, false, corr (majP, r) }); cands.push_back ({ r, true, corr (minP, r) }); }
    // Tonic hints: the bass's most played note, Live's scale setting.
    int bassTonic = -1;
    { double b = 0; for (int i = 0; i < 12; ++i) if (bassH[(size_t) i] > b) { b = bassH[(size_t) i]; bassTonic = i; } }
    for (auto& c : cands)
    {
        if (c.root == bassTonic) c.s += 0.04;
        if (p.rootNote >= 0 && c.root == p.rootNote % 12)
        {
            const bool liveMinor = p.scale.containsIgnoreCase ("minor") || p.scale.containsIgnoreCase ("aeolian") || p.scale.containsIgnoreCase ("dorian");
            if (liveMinor == c.minor && p.scale.isNotEmpty() && ! p.scale.containsIgnoreCase ("chromatic")) c.s += 0.05;
        }
    }
    std::sort (cands.begin(), cands.end(), [] (auto& x, auto& y) { return x.s > y.s; });
    a.keyRoot = cands[0].root;
    a.minor = cands[0].minor;
    a.keyConfidence = (float) juce::jlimit (0.0, 1.0, (cands[0].s - cands[1].s) * 6.0 + 0.35 * cands[0].s);
    a.keySource = juce::String (used) + utf8 (" piste(s) MIDI");
}

// --------------------------------------------------------------------------------------
ProjectAnalysis analyse (LiveProject& p)
{
    ProjectAnalysis a;
    if (! p.valid) return a;
    a.valid = true;
    a.bpm = p.tempo;
    a.beatsPerBar = p.beatsPerBar();
    const double bpb = a.beatsPerBar;
    const int nT = (int) p.tracks.size();
    a.tracks.resize ((size_t) nT);

    for (int i = 0; i < nT; ++i)
    {
        auto& t = p.tracks[(size_t) i];
        auto& prof = a.tracks[(size_t) i];
        prof.track = i;
        if (t.group) { t.role = TrackRole::Other; prof.role = TrackRole::Other; prof.why = "groupe"; continue; }
        classify (t, prof, bpb);
    }
    detectKey (p, a);

    // ---- activity per bar
    const double end = p.arrangementEnd();
    const int bars = (int) std::ceil (end / bpb - 1.0e-6);
    a.activity.assign ((size_t) nT, std::vector<float> ((size_t) juce::jmax (0, bars), 0.0f));
    for (int i = 0; i < nT; ++i)
    {
        auto& t = p.tracks[(size_t) i];
        if (t.mute || t.group) continue;
        for (auto& c : t.arrangement)
        {
            if (c.muted) continue;
            const int b0 = juce::jmax (0, (int) std::floor (c.start / bpb)), b1 = juce::jmin (bars, (int) std::ceil (c.end / bpb - 1.0e-6));
            for (int b = b0; b < b1; ++b)
            {
                const double s = b * bpb, e = s + bpb;
                if (c.midi)
                {
                    if (! LiveNotes::playedIn (c, s, e).empty()) a.activity[(size_t) i][(size_t) b] = 1.0f;
                }
                else
                {
                    const double cover = std::min (e, c.end) - std::max (s, c.start);
                    if (cover >= bpb * 0.5) a.activity[(size_t) i][(size_t) b] = 1.0f;
                }
            }
        }
    }

    // ---- structure: 4-bar phrases with the same set of tracks merge into sections
    int first = bars;
    for (int b = 0; b < bars && first == bars; ++b)
        for (int i = 0; i < nT; ++i) if (a.activity[(size_t) i][(size_t) b] > 0) { first = b; break; }
    a.firstBar = first < bars ? first : 0;
    a.endBar = bars;
    const int phrase = 4;
    struct Ph { int start, len; std::set<int> set; float e; };
    std::vector<Ph> phrases;
    for (int b = (first / phrase) * phrase; b < bars; b += phrase)
    {
        Ph ph { b, std::min (phrase, bars - b), {}, 0.0f };
        for (int i = 0; i < nT; ++i)
        {
            int n = 0;
            for (int k = 0; k < ph.len; ++k) if (a.activity[(size_t) i][(size_t) (b + k)] > 0) ++n;
            if (n * 2 >= ph.len && n > 0) { ph.set.insert (i); ph.e += energyWeight (p.tracks[(size_t) i].role); }
        }
        phrases.push_back (ph);
    }
    auto jacc = [] (const std::set<int>& x, const std::set<int>& y)
    {
        if (x.empty() && y.empty()) return 1.0f;
        int inter = 0;
        for (int v : x) if (y.count (v)) ++inter;
        return (float) inter / (float) (x.size() + y.size() - (size_t) inter);
    };
    std::vector<Ph> merged;
    for (auto& ph : phrases)
    {
        if (! merged.empty() && jacc (merged.back().set, ph.set) >= 0.75f && merged.back().set.empty() == ph.set.empty())
        {
            auto& m = merged.back();
            m.e = (m.e * m.len + ph.e * ph.len) / (m.len + ph.len);
            m.len += ph.len;
            for (int v : ph.set) m.set.insert (v);
        }
        else merged.push_back (ph);
    }
    float eMax = 0.0f;
    for (auto& m : merged) eMax = std::max (eMax, m.e);
    std::vector<bool> isDrop;
    for (auto& m : merged) isDrop.push_back (eMax > 0 && m.e >= 0.72f * eMax && ! m.set.empty());
    int drops = 0;
    bool lowSinceDrop = false;
    for (size_t k = 0; k < merged.size(); ++k)
    {
        auto& m = merged[k];
        ExistingSection s;
        s.startBar = m.start;
        s.bars = m.len;
        s.energy = eMax > 0 ? m.e / eMax : 0.0f;
        s.tracks.assign (m.set.begin(), m.set.end());
        const bool nextDrop = k + 1 < merged.size() && isDrop[k + 1];
        if (m.set.empty()) s.kind = SecKind::Gap;
        else if (isDrop[k])
        {
            if (drops == 0 || ! lowSinceDrop) s.kind = drops == 0 ? SecKind::Drop : a.sections.back().kind;
            else s.kind = SecKind::Drop2;
            if (drops == 0 || lowSinceDrop) ++drops;
            lowSinceDrop = false;
        }
        else if (drops == 0)
        {
            const bool firstReal = a.sections.empty() || a.sections.back().kind == SecKind::Gap;
            if (nextDrop && s.energy >= 0.35f && ! firstReal) s.kind = SecKind::Build;
            else if (nextDrop && s.energy >= 0.5f) s.kind = SecKind::Build;
            else s.kind = SecKind::Intro;
        }
        else
        {
            lowSinceDrop = true;
            if (nextDrop) s.kind = s.energy >= 0.4f ? SecKind::Build2 : SecKind::Break;
            else if (k + 1 == merged.size()) s.kind = drops >= 2 ? SecKind::Outro : SecKind::Break;
            else s.kind = SecKind::Break;
        }
        a.sections.push_back (s);
    }
    for (size_t k = 0; k < a.sections.size(); ++k)
        if (a.mainSection < 0 || a.sections[k].energy > a.sections[(size_t) a.mainSection].energy + 1.0e-4f) a.mainSection = (int) k;

    // ---- material per track
    const int mainStart = a.mainSection >= 0 ? a.sections[(size_t) a.mainSection].startBar : 0;
    const int mainBars = a.mainSection >= 0 ? a.sections[(size_t) a.mainSection].bars : 0;
    for (int i = 0; i < nT; ++i)
    {
        auto& t = p.tracks[(size_t) i];
        auto& prof = a.tracks[(size_t) i];
        if (t.group) continue;
        const LiveClip* best = nullptr;
        double bestScore = -1.0;
        for (auto& c : t.arrangement)
        {
            if (c.muted || (c.midi && c.notes.empty())) continue;
            const double ms = mainStart * bpb, me = (mainStart + mainBars) * bpb;
            const double overlap = std::max (0.0, std::min (me, c.end) - std::max (ms, c.start));
            const double sc = overlap * 10.0 + (c.end - c.start) + (double) c.notes.size() * 0.01;
            if (sc > bestScore) { bestScore = sc; best = &c; }
        }
        if (best == nullptr)
            for (auto& c : t.session)
            {
                if (c.midi && c.notes.empty()) continue;
                const double sc = (double) c.notes.size() + c.loopLength() * 0.01;
                if (sc > bestScore) { bestScore = sc; best = &c; }
            }
        if (best != nullptr)
        {
            prof.material.valid = true;
            prof.material.session = best->session;
            prof.material.index = best->index;
            prof.material.start = best->start;
            prof.materialLoop = best->midi ? best->loopLength() : best->duration();
            prof.usable = ! t.mute && ! t.frozen;
        }
        if (prof.role == TrackRole::Drums || prof.role == TrackRole::Perc || t.hasDrumRack())
            mapDrums (t, prof, best != nullptr && best->midi ? best : nullptr);
    }
    a.sessionSketch = p.countClips (true) == 0 && p.countClips (false) > 0;

    // ---- what is there, what is missing
    std::set<SecKind> present;
    for (auto& s : a.sections) present.insert (s.kind == SecKind::Build2 ? SecKind::Build : s.kind);
    for (auto k : { SecKind::Intro, SecKind::Build, SecKind::Drop, SecKind::Break, SecKind::Drop2, SecKind::Outro })
        if (! present.count (k)) a.missing.push_back (k);

    // Findings
    std::map<TrackRole, juce::StringArray> byRole;
    for (int i = 0; i < nT; ++i)
        if (! p.tracks[(size_t) i].group) byRole[p.tracks[(size_t) i].role].add (p.tracks[(size_t) i].name);
    juce::StringArray roles;
    for (auto& [r, names] : byRole) roles.add (trackRoleLabel (r) + " : " + names.joinIntoString (", "));
    a.findings.add (juce::String (nT) + utf8 (" pistes lues, ") + juce::String (p.countClips (false)) + " clips, " + juce::String (p.countNotes()) + " notes.");
    for (auto& r : roles) a.findings.add ("  " + r);
    a.findings.add (utf8 ("Tempo ") + juce::String (p.tempo, 2) + " BPM (Live), " + juce::String (p.sigNum) + "/" + juce::String (p.sigDen)
                    + utf8 (". Tonalité ") + a.keyName() + utf8 (" (calculée sur les notes de ") + a.keySource
                    + (a.keyConfidence < 0.35f ? utf8 (", peu sûre") : juce::String()) + ").");
    if (! a.sections.empty())
    {
        juce::StringArray st;
        for (auto& s : a.sections)
            if (s.kind != SecKind::Gap) st.add (s.label() + " (" + juce::String ((int) s.tracks.size()) + " pistes)");
        a.findings.add (utf8 ("Structure actuelle : ") + st.joinIntoString (utf8 (" → ")));
    }
    else if (a.sessionSketch)
        a.findings.add (utf8 ("Arrangement vide : ta matière est dans la vue Session (FINISH construit à partir des clips de Session)."));
    else
        a.findings.add (utf8 ("Aucun clip trouvé dans le set."));

    if (a.mainSection >= 0)
    {
        auto& d = a.sections[(size_t) a.mainSection];
        if (d.bars < 16)
            a.findings.add (utf8 ("Le moment le plus fort (") + d.label() + utf8 (") ne dure que ") + juce::String (d.bars) + utf8 (" mesures."));
        if (a.mainSection > 0)
        {
            auto& prev = a.sections[(size_t) a.mainSection - 1];
            if (prev.kind != SecKind::Build && prev.kind != SecKind::Build2 && prev.energy < 0.45f)
            {
                a.findings.add (utf8 ("Pas de montée avant le drop : on passe de ") + juce::String ((int) prev.tracks.size()) + utf8 (" à ")
                                + juce::String ((int) d.tracks.size()) + utf8 (" pistes d'un coup."));
                a.manual.add (utf8 ("Pour un build AVANT ton drop : sélectionne la mesure ") + juce::String (d.startBar + 1)
                              + utf8 (" dans l'Arrangement, Cmd/Ctrl+I pour insérer 8 mesures, puis relance FINISH. FINISH ne déplace jamais tes clips."));
            }
        }
        if (a.mainSection == (int) a.sections.size() - 1)
            a.findings.add (utf8 ("Rien après le drop : pas de break, de deuxième drop ni d'outro."));
    }
    for (int i = 0; i < nT; ++i)
    {
        auto& t = p.tracks[(size_t) i];
        if ((t.role == TrackRole::Lead || t.role == TrackRole::Vocal) && bars >= 16)
        {
            int on = 0;
            for (int b = a.firstBar; b < bars; ++b) if (a.activity[(size_t) i][(size_t) b] > 0) ++on;
            if (on >= (bars - a.firstBar) * 9 / 10)
                a.findings.add (t.name + utf8 (" joue du début à la fin : aucun moment ne la met en valeur par son absence."));
        }
        int unused = 0;
        juce::StringArray unusedNames;
        for (auto& c : t.session)
        {
            bool inArr = false;
            for (auto& ac : t.arrangement) if (ac.name == c.name) inArr = true;
            if (! inArr) { ++unused; unusedNames.add ("'" + c.name + "'"); }
        }
        if (unused > 0 && ! a.sessionSketch)
            a.findings.add (t.name + " : " + juce::String (unused) + utf8 (" idée(s) en vue Session pas encore utilisée(s) ") + unusedNames.joinIntoString (", ") + ".");
        if (t.frozen) a.manual.add (t.name + utf8 (" est gelée : FINISH ne peut pas y écrire (dégèle-la si tu veux qu'elle continue)."));
    }
    int audio = 0;
    for (auto& t : p.tracks) if (! t.midi && ! t.group && t.hasContent()) ++audio;
    if (audio > 0)
        a.manual.add (juce::String (audio) + utf8 (" piste(s) audio : FINISH peut recopier tes clips audio, pas les modifier note par note."));

    juce::StringArray miss;
    for (auto k : a.missing) miss.add (secKindName (k));
    a.summary = juce::String (nT) + " pistes, " + a.keyName() + ", " + juce::String (p.tempo, 1) + " BPM"
              + (miss.isEmpty() ? utf8 (" — structure complète") : utf8 (" — manque : ") + miss.joinIntoString (", "));
    return a;
}

juce::String report (const ProjectAnalysis& a, const LiveProject& p)
{
    juce::ignoreUnused (p);
    juce::String s;
    s << utf8 ("CE QUE J'AI LU DANS TON PROJET\n");
    for (auto& f : a.findings) s << "- " << f << "\n";
    if (! a.missing.empty())
    {
        juce::StringArray miss;
        for (auto k : a.missing) miss.add (secKindName (k));
        s << utf8 ("\nCE QUI MANQUE : ") << miss.joinIntoString (", ") << "\n";
    }
    if (! a.manual.isEmpty())
    {
        s << utf8 ("\nÀ FAIRE TOI-MÊME\n");
        for (auto& m : a.manual) s << "- " << m << "\n";
    }
    return s;
}

static juce::String fileName (const juce::String& path)
{
    return path.isEmpty() ? juce::String() : juce::File::createFileWithoutCheckingPath (path.replaceCharacter ('\\', '/')).getFileName();
}

static juce::String deviceLabel (const LiveDevice& d)
{
    juce::String s = utf8 ("« ") + d.name + utf8 (" »");
    const auto kind = d.plugin ? d.kindLabel() : (d.displayName.isNotEmpty() ? d.displayName : d.className);
    if (kind.isNotEmpty() && kind != d.name) s << " (" << kind << ")";
    if (! d.on) s << utf8 (" [éteint]");
    return s;
}

juce::String soundInventory (const ProjectAnalysis& a, const LiveProject& p)
{
    juce::String s;
    s << utf8 ("LES SONS DE TON PROJET — FINISH les réutilise tels quels : il écrit sur ces pistes, il ne charge et ne remplace aucun instrument.\n\n");
    juce::StringArray plugins, automationNotes;
    for (size_t i = 0; i < p.tracks.size(); ++i)
    {
        const auto& t = p.tracks[i];
        if (t.group) continue;
        s << "- " << t.name << " [" << trackRoleLabel (t.role) << (t.midi ? "" : ", audio") << "]";
        if (! a.tracks.empty() && ! a.tracks[i].usable) s << utf8 ("  (non utilisée : ") << (t.mute ? "muette" : (t.frozen ? utf8 ("gelée") : "sans clip")) << ")";
        s << "\n";
        juce::StringArray inst, fx;
        for (auto& d : t.devices)
        {
            if (d.plugin) plugins.addIfNotAlreadyThere (d.name + " (" + d.kindLabel() + ")");
            if (d.isInstrument())
            {
                juce::String line = deviceLabel (d);
                if (d.isDrumRack() && ! d.pads.empty())
                {
                    juce::StringArray pads;
                    for (size_t k = 0; k < d.pads.size() && k < 8; ++k)
                        pads.add (d.pads[k].second + (k < d.padSamples.size() && d.padSamples[k].isNotEmpty() ? " = " + fileName (d.padSamples[k]) : juce::String()));
                    line << " : " << pads.joinIntoString (", ") << (d.pads.size() > 8 ? ", ..." : "");
                }
                if (d.sample.isNotEmpty()) line << " : sample " << fileName (d.sample);
                for (auto& ch : d.chains)
                    for (auto& x : ch.devices)
                    {
                        if (x.plugin) plugins.addIfNotAlreadyThere (x.name + " (" + x.kindLabel() + ")");
                        if (x.sample.isNotEmpty()) line << " / " << fileName (x.sample);
                    }
                inst.add (line);
            }
            else fx.add (deviceLabel (d));
            for (auto& au : d.automated) automationNotes.add (t.name + " : " + d.name + " / " + au);
        }
        for (auto& m : t.mixerAutomation) automationNotes.add (t.name + " : " + m);
        if (! inst.isEmpty()) s << "    instrument : " << inst.joinIntoString (", ") << "\n";
        if (! fx.isEmpty()) s << "    effets : " << fx.joinIntoString (utf8 (" → ")) << "\n";
        if (! t.midi)
        {
            juce::StringArray files;
            for (auto& c : t.arrangement) if (c.file.isNotEmpty()) files.addIfNotAlreadyThere (fileName (c.file));
            for (auto& c : t.session) if (c.file.isNotEmpty()) files.addIfNotAlreadyThere (fileName (c.file));
            if (! files.isEmpty()) s << "    samples : " << files.joinIntoString (", ") << utf8 ("  → copiés à l'identique (warp, gain, transposition)\n");
        }
        else if (t.hasContent())
            s << utf8 ("    → nouvelles parties écrites sur cette piste ; les variations partent d'une copie de ton clip (enveloppes et réglages du clip conservés)\n");
    }
    if (! plugins.isEmpty())
        s << utf8 ("\nPlug-ins tiers : ") << plugins.joinIntoString (", ")
          << utf8 ("\n  Leurs presets et réglages restent dans tes pistes : FINISH n'a pas besoin de les recréer, il joue à travers eux.\n");
    if (! automationNotes.isEmpty())
    {
        s << utf8 ("\nAUTOMATION DANS L'ARRANGEMENT (l'API de Live ne permet pas de la copier) :\n");
        for (auto& n : automationNotes) s << "  - " << n << "\n";
        s << utf8 ("  Sur les nouvelles mesures elle garde sa dernière valeur. FINISH te le rappelle après l'envoi : copie la lane (Cmd/Ctrl+C, Cmd/Ctrl+V).\n");
    }
    s << utf8 ("\nSons qui ne viennent PAS de ton projet : aucun par défaut. La piste FINISH FX (risers, impacts) n'est ajoutée que si tu actives « SONS FINISH ».");
    return s;
}
}
