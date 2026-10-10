#include "ProjectPlanner.h"
#include "../Compose/ArrangementExport.h"
#include <map>

const char* levelName (Level l)
{
    static const char* n[] = { "COMPLETER", "ENRICHIR", "AMELIORER", "FINALISER" };
    return n[(int) l];
}

juce::String levelHelp (Level l)
{
    switch (l)
    {
        case Level::Complete: return utf8 ("Ajoute seulement les parties qui manquent, avec des copies de tes clips (et des parties retirées), sans aucune note nouvelle.");
        case Level::Enrich:   return utf8 ("Ajoute les parties manquantes et fait évoluer tes motifs : roulements, montées, réponse mélodique, basse simplifiée, accords tenus.");
        case Level::Improve:  return utf8 ("Comme Enrichir + transitions : roulements posés sur tes sections existantes (sur une copie de ta piste batterie) et piste FX (risers, impacts).");
        case Level::Finalise: return utf8 ("Comme Améliorer + mastering automatique d'après l'analyse de ton mix.");
    }
    return {};
}

juce::String txLabel (Tx t)
{
    static const char* n[] = { "copie", "kick seul", "sans kick", "sans caisse claire", "charleys seuls", "allégé", "roulement", "montée",
                               "coupure", "fondamentales", "tenu", "stabs", "teaser", "épuré", "réponse", "ralenti", "+1 octave", "-1 octave",
                               "fondu entrant", "fondu sortant" };
    return utf8 (n[(int) t]);
}

juce::String PlanCell::summary() const
{
    if (off) return utf8 ("désactivé");
    if (parts.empty()) return "-";
    juce::StringArray s;
    for (auto& p : parts)
        for (auto t : p.tx)
            if (! (t == Tx::Copy && p.tx.size() > 1)) s.addIfNotAlreadyThere (txLabel (t));
    return s.joinIntoString (" + ");
}

int ProjectPlan::newSectionCount() const noexcept
{
    int n = 0;
    for (auto& s : sections) if (! s.existing) ++n;
    return n;
}

int ProjectPlan::lastBar() const noexcept
{
    int b = 0;
    for (auto& s : sections) b = std::max (b, s.startBar + s.bars);
    return b;
}

int ProjectPlan::countWrites() const noexcept
{
    int n = 0;
    for (auto& s : sections)
        if (! s.existing)
            for (auto& c : s.cells)
                if (! c.empty())
                    for (auto& part : c.parts) if (part.exactCopy() || ! part.notes.empty()) ++n;
    return n + (int) layers.size();
}

namespace ProjectPlanner
{
// --------------------------------------------------------------------------------------
static const LiveClip* findClip (const LiveTrack& t, const ClipRef& r)
{
    if (! r.valid) return nullptr;
    if (r.session)
    {
        for (auto& c : t.session) if (c.index == r.index) return &c;
        return nullptr;
    }
    for (auto& c : t.arrangement) if (std::abs (c.start - r.start) < 1.0e-4) return &c;
    return nullptr;
}

static double copyUnit (const LiveClip& c) { return c.session ? c.loopLength() : c.duration(); }

static bool contains (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); }

static void clampNotes (std::vector<LiveNote>& n, double partBeats)
{
    std::vector<LiveNote> out;
    for (auto x : n)
    {
        if (x.start < -1.0e-6 || x.start >= partBeats - 1.0e-6) continue;
        x.start = std::max (0.0, x.start);
        x.length = juce::jlimit (0.02, partBeats - x.start, x.length);
        x.velocity = juce::jlimit (1, 127, x.velocity);
        x.pitch = juce::jlimit (0, 127, x.pitch);
        out.push_back (x);
    }
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    // No duplicates (same pitch, same start)
    out.erase (std::unique (out.begin(), out.end(), [] (auto& a, auto& b) { return a.pitch == b.pitch && std::abs (a.start - b.start) < 1.0e-4; }), out.end());
    n.swap (out);
}

static void addRoll (std::vector<LiveNote>& n, int pitch, double from, double to, double startStep, double endStep, int v0, int v1)
{
    if (pitch < 0 || to <= from) return;
    double t = from;
    while (t < to - 1.0e-6)
    {
        const double u = (t - from) / (to - from);
        double step = startStep * std::pow (endStep / startStep, u);
        // snap the step to a musical value
        for (double s : { 1.0, 0.5, 0.25, 0.125 }) if (step >= s * 0.75) { step = s; break; }
        if (step < 0.125) step = 0.125;
        n.push_back ({ pitch, t, std::min (step * 0.9, 0.25), juce::roundToInt (v0 + (v1 - v0) * u) });
        t += step;
    }
}

std::vector<LiveNote> transform (std::vector<LiveNote> m, const std::vector<Tx>& txs, double L, const TrackProfile& prof,
                                 double loop, double bpb, int keyRoot, bool minor)
{
    const auto& d = prof.drums;
    auto isKick = [&] (int p) { return contains (d.kick, p); };
    auto isSnare = [&] (int p) { return contains (d.snare, p); };
    auto isHat = [&] (int p) { return contains (d.hats, p); };
    auto keep = [&] (auto pred) { m.erase (std::remove_if (m.begin(), m.end(), [&] (const LiveNote& n) { return ! pred (n); }), m.end()); };
    const bool drums = prof.role == TrackRole::Drums || prof.role == TrackRole::Perc;
    loop = juce::jmax (0.25, loop);

    for (auto tx : txs)
    {
        switch (tx)
        {
            case Tx::Copy: break;
            case Tx::KickOnly:
                if (d.hasKick()) keep ([&] (auto& n) { return isKick (n.pitch); });
                else
                {
                    int low = 128;
                    for (auto& n : m) low = std::min (low, n.pitch);
                    keep ([&] (auto& n) { return n.pitch == low && std::abs (n.start - std::round (n.start)) < 0.03; });
                }
                break;
            case Tx::NoKick:  keep ([&] (auto& n) { return ! isKick (n.pitch); }); break;
            case Tx::NoSnare: keep ([&] (auto& n) { return ! isSnare (n.pitch); }); break;
            case Tx::HatsOnly:
                if (! d.hats.empty()) keep ([&] (auto& n) { return isHat (n.pitch); });
                else keep ([&] (auto& n) { return ! isKick (n.pitch) && ! isSnare (n.pitch); });
                break;
            case Tx::Thin:
            {
                int vmax = 1;
                for (auto& n : m) vmax = std::max (vmax, n.velocity);
                if (drums)
                    keep ([&] (auto& n)
                    {
                        if (isKick (n.pitch)) return true;
                        const double f = std::fmod (n.start, 0.5);
                        return n.velocity >= vmax * 0.6 && (f < 0.03 || f > 0.47);
                    });
                else
                    keep ([&] (auto& n) { const double f = std::fmod (n.start, 1.0); return f < 0.03 || std::abs (f - 0.5) < 0.03 || n.length >= 1.0; });
                break;
            }
            case Tx::Fill:
            {
                if (d.rollPitch < 0 || L < 2.0) break;
                const double from = L - 2.0;
                keep ([&] (auto& n) { return n.start < from - 1.0e-6 || (isKick (n.pitch) && std::abs (n.start - std::round (n.start)) < 0.03); });
                addRoll (m, d.rollPitch, from, from + 1.0, 0.5, 0.5, 78, 92);
                addRoll (m, d.rollPitch, from + 1.0, L, 0.25, 0.25, 96, 122);
                break;
            }
            case Tx::BuildRoll:
            {
                keep ([&] (auto& n) { return isKick (n.pitch) && n.start < L - 0.5; });
                if (d.rollPitch >= 0)
                {
                    const double a = 0.0, b = L * 0.5, c = L * 0.75, e = L * 0.875;
                    addRoll (m, d.rollPitch, a, b, 1.0, 1.0, 52, 72);
                    addRoll (m, d.rollPitch, b, c, 0.5, 0.5, 72, 92);
                    addRoll (m, d.rollPitch, c, e, 0.25, 0.25, 92, 108);
                    addRoll (m, d.rollPitch, e, L - 0.5, 0.125, 0.125, 108, 124);
                }
                break;
            }
            case Tx::CutEnd: keep ([&] (auto& n) { return n.start < L - 2.0 - 1.0e-6; }); for (auto& n : m) n.length = std::min (n.length, L - 2.0 - n.start); break;
            case Tx::Roots:
            {
                std::vector<LiveNote> out;
                const double win = bpb * 0.5;
                for (double w = 0.0; w < L - 1.0e-6; w += win)
                {
                    const LiveNote* best = nullptr;
                    for (auto& n : m)
                        if (n.start >= w - 1.0e-6 && n.start < w + win - 1.0e-6)
                            if (best == nullptr || n.start < best->start - 0.02 || (std::abs (n.start - best->start) <= 0.02 && n.pitch < best->pitch)) best = &n;
                    if (best != nullptr) out.push_back (*best);
                }
                m.swap (out);
                break;
            }
            case Tx::Sustain:
            {
                std::vector<LiveNote> out;
                const bool mono = prof.role == TrackRole::Bass;
                for (double b = 0.0; b < L - 1.0e-6; b += bpb)
                {
                    double first = 1.0e9;
                    for (auto& n : m) if (n.start >= b - 1.0e-6 && n.start < b + bpb - 1.0e-6) first = std::min (first, n.start);
                    if (first > 1.0e8) continue;
                    int low = 128;
                    for (auto& n : m)
                        if (n.start >= first - 1.0e-6 && n.start < first + 0.06)
                        {
                            if (mono) low = std::min (low, n.pitch);
                            else out.push_back ({ n.pitch, b, bpb * 0.97, std::min (127, n.velocity) });
                        }
                    if (mono && low < 128) out.push_back ({ low, b, bpb * 0.97, 100 });
                }
                m.swap (out);
                break;
            }
            case Tx::Stabs:
            {
                std::vector<LiveNote> out;
                for (double b = 0.0; b < L - 1.0e-6; b += bpb)
                {
                    double first = 1.0e9;
                    for (auto& n : m) if (n.start >= b - 1.0e-6 && n.start < b + bpb - 1.0e-6) first = std::min (first, n.start);
                    if (first > 1.0e8) continue;
                    for (auto& n : m)
                        if (n.start >= first - 1.0e-6 && n.start < first + 0.06)
                            for (double k = 0.5; k < bpb; k += 1.0)
                                out.push_back ({ n.pitch, b + k, 0.22, juce::roundToInt (n.velocity * 0.92) });
                }
                m.swap (out);
                break;
            }
            case Tx::Tease:
                keep ([&] (auto& n) { return std::fmod (n.start, loop) < loop * 0.5 - 1.0e-6; });
                for (auto& n : m) n.velocity = juce::roundToInt (n.velocity * 0.9);
                break;
            case Tx::Sparse:
                keep ([&] (auto& n) { const double f = std::fmod (n.start, 1.0); return f < 0.03 || f > 0.97 || n.length >= 1.0; });
                break;
            case Tx::Answer:
            {
                // Call and response from the user's own motif: the 2nd half of each loop answers the 1st half a third higher.
                std::vector<LiveNote> out;
                const double half = loop * 0.5;
                int maxP = 0;
                for (auto& n : m) maxP = std::max (maxP, n.pitch);
                const int steps = maxP + 4 <= 96 ? 2 : -2;
                for (auto& n : m)
                {
                    const double pos = std::fmod (n.start, loop);
                    if (pos < half - 1.0e-6)
                    {
                        out.push_back (n);
                        auto r = n;
                        r.start = n.start + half;
                        r.length = std::min (n.length, half - pos);
                        r.pitch = ProjectAnalyzer::scaleStep (n.pitch, steps, keyRoot, minor);
                        r.velocity = juce::roundToInt (n.velocity * 0.95);
                        out.push_back (r);
                    }
                }
                m.swap (out);
                break;
            }
            case Tx::Augment:
            {
                // The first half of the motif at half speed.
                std::vector<LiveNote> out;
                const double half = loop * 0.5;
                for (double off = 0.0; off < L - 1.0e-6; off += loop)
                    for (auto& n : m)
                        if (n.start < half - 1.0e-6)   // m is tiled from the loop start: the first cycle is the motif
                        {
                            auto r = n;
                            r.start = off + n.start * 2.0;
                            r.length = n.length * 2.0;
                            out.push_back (r);
                        }
                m.swap (out);
                break;
            }
            case Tx::OctUp:   for (auto& n : m) if (n.pitch + 12 <= 108) n.pitch += 12; break;
            case Tx::OctDown: for (auto& n : m) if (n.pitch - 12 >= 24) n.pitch -= 12; break;
            case Tx::FadeIn:  for (auto& n : m) n.velocity = juce::roundToInt (n.velocity * (0.5 + 0.5 * n.start / L)); break;
            case Tx::FadeOut: for (auto& n : m) n.velocity = juce::roundToInt (n.velocity * (1.0 - 0.6 * n.start / L)); break;
        }
    }
    clampNotes (m, L);
    return m;
}

// --------------------------------------------------------------------------------------
// Recipes: what each of the user's tracks plays in a new section, by section kind and track role.
struct Ctx { bool enrich, improve; int v; const SectionMood& mood; };

static std::vector<PlanPart> recipe (SecKind kind, int N, TrackRole role, bool inMain, const Ctx& c)
{
    std::vector<PlanPart> parts;
    const int h = juce::jmax (1, N / 2), q = juce::jmax (1, N / 4);
    auto P = [&] (int s, int b, std::vector<Tx> t) { if (b > 0 && s < N) parts.push_back ({ s, std::min (b, N - s), std::move (t), {} }); };
    const bool D = role == TrackRole::Drums, PC = role == TrackRole::Perc, B = role == TrackRole::Bass,
               CH = role == TrackRole::Chords, PD = role == TrackRole::Pad, L = role == TrackRole::Lead || role == TrackRole::Vocal,
               FXr = role == TrackRole::Fx, OTHER = role == TrackRole::Audio || role == TrackRole::Other;
    const int v = c.v % 3;
    auto dropDrums = [&] (int every)
    {
        for (int s = 0; s < N; s += every) P (s, every, c.enrich ? std::vector<Tx> { Tx::Copy, Tx::Fill } : std::vector<Tx> { Tx::Copy });
    };

    switch (kind)
    {
        case SecKind::Intro:
            if (D)
            {
                if (! c.enrich) { P (0, h, { Tx::KickOnly }); P (h, N - h, { Tx::NoSnare }); }
                else if (v == 0) { P (0, h, { Tx::KickOnly }); P (h, N - h, { Tx::NoSnare, Tx::Fill }); }
                else if (v == 1) { P (0, h, { Tx::HatsOnly }); P (h, N - h, { Tx::Copy, Tx::Fill }); }
                else P (h, N - h, { Tx::KickOnly, Tx::Fill });
            }
            else if (PC) P (h, N - h, c.enrich ? std::vector<Tx> { Tx::Thin } : std::vector<Tx> { Tx::Copy });
            else if (B) { if (v != 2) P (h, N - h, c.enrich ? std::vector<Tx> { Tx::Roots } : std::vector<Tx> { Tx::Copy }); }
            else if (CH) { if (c.enrich) P (0, N, { Tx::Sustain, Tx::FadeIn }); else P (h, N - h, { Tx::Copy }); }
            else if (PD) P (0, N, c.enrich ? std::vector<Tx> { Tx::FadeIn } : std::vector<Tx> { Tx::Copy });
            else if (L) { if (c.enrich) { if (v == 1) P (h, N - h, { Tx::Sparse }); else P (N - q, q, { Tx::Tease }); } }
            break;

        case SecKind::Build:
        case SecKind::Build2:
        {
            const int vv = kind == SecKind::Build2 ? (v + 1) % 3 : v;
            if (D)
            {
                if (! c.enrich) P (0, N, { Tx::KickOnly });
                else if (vv == 0) P (0, N, { Tx::BuildRoll });
                else if (vv == 1) { P (0, N - 1, { Tx::NoSnare }); P (N - 1, 1, { Tx::KickOnly, Tx::Fill, Tx::CutEnd }); }
                else { P (0, h, { Tx::HatsOnly }); P (h, N - h, { Tx::BuildRoll }); }
            }
            else if (PC) P (0, N, c.enrich ? std::vector<Tx> { Tx::Copy, Tx::CutEnd } : std::vector<Tx> { Tx::Copy });
            else if (B)
            {
                if (! c.enrich) P (0, N, { Tx::Copy });
                else if (vv == 0) P (0, N, { Tx::Roots, Tx::CutEnd });
                else if (vv == 1) P (0, N, { Tx::OctUp, Tx::CutEnd });
                else P (0, h, { Tx::Roots });
            }
            else if (CH) P (0, N, ! c.enrich ? std::vector<Tx> { Tx::Copy } : (vv == 0 ? std::vector<Tx> { Tx::Stabs, Tx::FadeIn } : (vv == 1 ? std::vector<Tx> { Tx::Sustain } : std::vector<Tx> { Tx::Copy })));
            else if (PD) P (0, N, c.enrich ? std::vector<Tx> { Tx::FadeIn } : std::vector<Tx> { Tx::Copy });
            else if (L)
            {
                if (! c.enrich) P (0, N, { Tx::Copy });
                else if (vv == 1) P (0, N, { Tx::Tease });
                else if (vv == 2 || kind == SecKind::Build2) { P (0, h, { Tx::Copy }); P (h, N - h, { Tx::OctUp }); }
                else P (0, N, { Tx::Copy });
            }
            break;
        }

        case SecKind::Drop:
            if (! inMain) break;
            if (D) dropDrums (8);
            else P (0, N, { Tx::Copy });
            break;

        case SecKind::Break:
            if (D) { if (c.enrich && v == 0) P (h, N - h, { Tx::HatsOnly }); else if (c.enrich && v == 2) P (h, N - h, { Tx::KickOnly, Tx::FadeIn }); }
            else if (PC) { if (c.enrich && v == 1) P (h, N - h, { Tx::Thin }); }
            else if (B) { if (c.enrich && v == 0) P (h, N - h, { Tx::Sustain }); else if (c.enrich && v == 2) P (0, N, { Tx::Sustain, Tx::FadeIn }); }
            else if (CH) P (0, N, c.enrich ? std::vector<Tx> { Tx::Sustain } : std::vector<Tx> { Tx::Copy });
            else if (PD) P (0, N, { Tx::Copy });
            else if (L)
            {
                if (! c.enrich) P (0, N, { Tx::Copy });
                else if (v == 0) P (0, N, { Tx::Augment });
                else if (v == 1) P (0, N, { Tx::Sparse });
                else P (h, N - h, { Tx::Copy });
            }
            break;

        case SecKind::Drop2:
            if (! inMain) break;
            if (D) dropDrums (c.mood.intense > 0 ? 4 : 8);
            else if (L)
            {
                if (! c.enrich) P (0, N, { Tx::Copy });
                else if (v == 0) { P (0, h, { Tx::Copy }); P (h, N - h, { Tx::Answer }); }
                else if (v == 1) { P (0, h, { Tx::Copy }); P (h, N - h, { c.mood.darker > 0 ? Tx::Copy : Tx::OctUp }); }
                else P (0, N, { Tx::Answer });
            }
            else P (0, N, { Tx::Copy });
            break;

        case SecKind::Outro:
            if (D) { P (0, h, { Tx::Copy }); P (h, c.enrich ? N - h : q, c.enrich ? std::vector<Tx> { Tx::KickOnly, Tx::FadeOut } : std::vector<Tx> { Tx::KickOnly }); }
            else if (PC || B) P (0, h, { Tx::Copy });
            else if (CH) { P (0, h, { Tx::Copy }); if (c.enrich) P (h, N - h, { Tx::Sustain, Tx::FadeOut }); }
            else if (PD) P (0, N, c.enrich ? std::vector<Tx> { Tx::FadeOut } : std::vector<Tx> { Tx::Copy });
            else if (L) { P (0, q, { Tx::Copy }); if (c.enrich) P (q, q, { Tx::Tease }); }
            break;

        case SecKind::Rise:
            // Energy rising over N bars: the tracks come in one after the other, the drums build up at the end.
            if (PD || L) P (0, N, c.enrich ? std::vector<Tx> { Tx::FadeIn } : std::vector<Tx> { Tx::Copy });
            else if (CH || PC) P (q, N - q, c.enrich ? std::vector<Tx> { Tx::FadeIn } : std::vector<Tx> { Tx::Copy });
            else if (B) P (h, N - h, c.enrich ? std::vector<Tx> { Tx::Roots, Tx::CutEnd } : std::vector<Tx> { Tx::Copy });
            else if (D) { P (h, N - h - juce::jmin (q, N - h), { Tx::KickOnly }); P (N - juce::jmin (q, N - h), juce::jmin (q, N - h), c.enrich ? std::vector<Tx> { Tx::BuildRoll } : std::vector<Tx> { Tx::KickOnly }); }
            break;

        default: break;
    }
    juce::ignoreUnused (FXr, OTHER);
    // Audio loops and unknown tracks follow the drums in drops and the pad elsewhere: exact copies only.
    if ((OTHER || FXr) && parts.empty())
    {
        if (kind == SecKind::Drop || kind == SecKind::Drop2) { if (inMain) P (0, N, { Tx::Copy }); }
        else if (kind == SecKind::Build || kind == SecKind::Build2 || kind == SecKind::Rise) P (h, N - h, { Tx::Copy });
        else if (kind == SecKind::Outro) P (0, h, { Tx::Copy });
    }

    // Moods from the command
    for (auto& p : parts)
    {
        if (c.mood.darker > 0 && (L || CH))
            for (auto& t : p.tx) if (t == Tx::OctUp) t = Tx::Copy;
        if (c.mood.minimal > 0)
            for (auto& t : p.tx) if (t == Tx::Answer || t == Tx::Stabs) t = Tx::Copy;
        if (c.mood.brighter > 0 && L && (kind == SecKind::Drop2) && p.startBar > 0) p.tx.push_back (Tx::OctUp);
    }
    if (c.mood.darker > 0 && D && (kind == SecKind::Intro || kind == SecKind::Outro || kind == SecKind::Break))
        for (auto& p : parts) p.tx.push_back (Tx::Thin);
    return parts;
}

static juce::String recipeText (SecKind k, bool enrich, int v)
{
    switch (k)
    {
        case SecKind::Intro: return enrich ? utf8 ("Entrée avec tes pistes : kick seul puis le groove, ta basse simplifiée, accords tenus, ton motif annoncé à la fin.")
                                           : utf8 ("Entrée avec tes pistes : kick seul, puis groove sans caisse claire, ta basse et tes accords en 2e moitié.");
        case SecKind::Build: case SecKind::Build2:
            return enrich ? (v % 3 == 1 ? utf8 ("Montée : ton groove sans caisse claire, roulement sur la dernière mesure, coupure avant le drop.")
                                        : utf8 ("Montée : roulement sur ta caisse claire qui accélère, basse réduite aux fondamentales, coupure avant le drop."))
                          : utf8 ("Montée : ton kick seul et tes autres pistes, sans notes ajoutées.");
        case SecKind::Drop: return utf8 ("Ton drop, recopié tel quel") + (enrich ? utf8 (", roulement toutes les 8 mesures.") : juce::String ("."));
        case SecKind::Break: return enrich ? (v % 3 == 0 ? utf8 ("Respiration : ton motif ralenti, accords tenus, basse tenue en 2e moitié, charleys seuls.")
                                                          : utf8 ("Respiration : ta mélodie épurée sur tes accords tenus, sans batterie."))
                                           : utf8 ("Respiration : ta mélodie et tes accords, sans batterie ni basse.");
        case SecKind::Drop2: return enrich ? (v % 3 == 1 ? utf8 ("Ton drop, puis ta mélodie une octave au-dessus en 2e moitié, roulements.")
                                                          : utf8 ("Ton drop développé : ta mélodie reçoit une réponse (son propre motif, une tierce au-dessus), roulements."))
                                           : utf8 ("Ton drop, recopié pour la fin du morceau.");
        case SecKind::Outro: return utf8 ("Sortie : tout ton drop puis les pistes s'en vont, le kick finit seul.");
        case SecKind::Rise:  return utf8 ("Montée d'énergie progressive : tes pistes entrent l'une après l'autre, roulement à la fin.");
        default: return {};
    }
}

static std::vector<FxEvent> fxFor (SecKind k, int bars, double bpb)
{
    std::vector<FxEvent> fx;
    const double len = bars * bpb;
    switch (k)
    {
        case SecKind::Build: case SecKind::Build2: case SecKind::Rise: fx.push_back ({ FxKind::Riser, 0.0, len, 0.8f }); break;
        case SecKind::Break: fx.push_back ({ FxKind::Impact, 0.0, 4.0, 0.8f }); fx.push_back ({ FxKind::Downlifter, 0.0, std::min (8.0, len), 0.6f }); break;
        case SecKind::Drop: case SecKind::Drop2: fx.push_back ({ FxKind::Impact, 0.0, 4.0, 0.9f }); break;
        case SecKind::Intro: if (bars >= 2) fx.push_back ({ FxKind::ReverseCymbal, len - bpb, bpb, 0.7f }); break;
        default: break;
    }
    return fx;
}

static int defaultBars (SecKind k, int mainBars)
{
    switch (k)
    {
        case SecKind::Build: case SecKind::Build2: return 8;
        case SecKind::Drop:  return juce::jmax (16, mainBars - mainBars % 4);
        default: return 16;
    }
}

// --------------------------------------------------------------------------------------
void renderNotes (ProjectPlan& plan, const LiveProject& p, const ProjectAnalysis& a)
{
    const double bpb = a.beatsPerBar;
    for (auto& s : plan.sections)
    {
        if (s.existing) continue;
        for (auto& cell : s.cells)
        {
            if (cell.track < 0 || cell.track >= (int) p.tracks.size()) continue;
            auto& t = p.tracks[(size_t) cell.track];
            auto& prof = a.tracks[(size_t) cell.track];
            const auto* clip = findClip (t, prof.material);
            for (auto& part : cell.parts)
            {
                part.notes.clear();
                if (cell.locked) part.tx = { Tx::Copy };
                if (clip == nullptr || ! clip->midi) continue;
                const double L = part.bars * bpb;
                part.notes = transform (LiveNotes::tiled (*clip, L), part.tx, L, prof, clip->loopLength(), bpb, a.keyRoot, a.minor);
            }
        }
    }
}

static SecKind nextAfter (SecKind k)
{
    switch (k)
    {
        case SecKind::Intro: return SecKind::Build;
        case SecKind::Build: return SecKind::Drop;
        case SecKind::Drop: return SecKind::Break;
        case SecKind::Break: return SecKind::Build2;
        case SecKind::Build2: return SecKind::Drop2;
        case SecKind::Drop2: return SecKind::Outro;
        default: return SecKind::Unknown;
    }
}

ProjectPlan makePlan (const LiveProject& p, const ProjectAnalysis& a, const PlanOptions& o)
{
    ProjectPlan plan;
    if (! a.valid) return plan;
    plan.level = o.level;
    plan.bpm = a.bpm;
    plan.beatsPerBar = a.beatsPerBar;
    const double bpb = a.beatsPerBar;
    const bool enrich = o.level >= Level::Enrich, improve = o.level >= Level::Improve;
    const int nT = (int) p.tracks.size();

    // Existing sections (display)
    for (auto& e : a.sections)
    {
        if (e.kind == SecKind::Gap) continue;
        PlanSection s;
        s.kind = e.kind; s.name = secKindName (e.kind); s.startBar = e.startBar; s.bars = e.bars; s.existing = true; s.energy = e.energy;
        s.cells.resize ((size_t) nT);
        for (int i = 0; i < nT; ++i) s.cells[(size_t) i].track = i;
        for (int i : e.tracks) s.cells[(size_t) i].parts.push_back ({ 0, e.bars, { Tx::Copy }, {} });
        plan.sections.push_back (s);
    }

    int usable = 0;
    for (auto& t : a.tracks) if (t.usable) ++usable;
    if (usable == 0)
    {
        plan.explain.add (utf8 ("Aucune piste avec des clips exploitables : rien à continuer."));
        return plan;
    }

    // Which sections to write, and where
    std::vector<SecKind> todo;
    SecKind last = SecKind::Unknown;
    SecKind firstKind = SecKind::Unknown;
    for (auto& e : a.sections) if (e.kind != SecKind::Gap) { last = e.kind; if (firstKind == SecKind::Unknown) firstKind = e.kind; }
    const int mainBars = a.mainSection >= 0 ? a.sections[(size_t) a.mainSection].bars : 16;
    int start = (a.endBar + 3) / 4 * 4;

    if (o.rangeBars > 0)
    {
        todo.push_back (SecKind::Rise);
        plan.explain.add (utf8 ("Montée d'énergie sur ") + juce::String (o.rangeBars) + utf8 (" mesures, à la suite de ton morceau (mesure ") + juce::String (start + 1) + ").");
    }
    else if (! o.onlySections.empty())
    {
        todo = o.onlySections;
    }
    else
    {
        bool full = o.placement == PlanOptions::Placement::FullVersion;
        if (o.placement == PlanOptions::Placement::Auto && (firstKind == SecKind::Drop || last == SecKind::Unknown || a.sessionSketch)) full = true;
        if (full)
        {
            plan.fullVersion = true;
            todo = { SecKind::Intro, SecKind::Build, SecKind::Drop, SecKind::Break, SecKind::Build2, SecKind::Drop2, SecKind::Outro };
            if (a.endBar > 0)
            {
                start = (a.endBar + 3) / 4 * 4 + 4;
                plan.explain.add (utf8 ("Ton morceau commence directement sur son moment le plus fort : FINISH ne peut pas insérer une intro avant sans déplacer tes clips. ")
                                  + utf8 ("Il construit donc une version complète à la suite, à partir de la mesure ") + juce::String (start + 1)
                                  + utf8 (", entièrement avec tes pistes. Ton esquisse reste intacte au début."));
            }
            else
            {
                start = 0;
                plan.explain.add (utf8 ("Arrangement vide : FINISH construit le morceau complet dans l'Arrangement à partir de tes clips de la vue Session."));
            }
        }
        else
        {
            for (auto k = nextAfter (last == SecKind::Gap ? SecKind::Unknown : last); k != SecKind::Unknown; k = nextAfter (k))
                todo.push_back (k);
            if (todo.empty())
            {
                plan.explain.add (utf8 ("Ta structure est déjà complète (jusqu'à l'outro) : rien à ajouter à la suite. ")
                                  + utf8 ("Demande par exemple « propose trois versions de mon build-up » ou une montée sur 16 mesures."));
            }
            else
                plan.explain.add (utf8 ("Ton morceau s'arrête après ") + juce::String (secKindName (last)) + utf8 (" (mesure ") + juce::String (a.endBar)
                                  + utf8 (") : FINISH le continue à partir de la mesure ") + juce::String (start + 1) + utf8 (", sur tes propres pistes."));
        }
    }
    plan.firstNewBar = start;

    std::map<SecKind, int> seen;
    for (auto& e : a.sections) ++seen[e.kind];
    const auto* mainSec = a.mainSection >= 0 ? &a.sections[(size_t) a.mainSection] : nullptr;
    int bar = start;
    for (auto kind : todo)
    {
        PlanSection s;
        s.kind = kind;
        const int count = ++seen[kind];
        s.name = secKindName (kind);
        if (count > 1 && kind != SecKind::Build2 && kind != SecKind::Drop2) s.name << " " << count;
        s.bars = o.rangeBars > 0 ? juce::jmax (4, o.rangeBars) : defaultBars (kind, mainBars);
        s.startBar = bar;
        s.variant = (o.variants[(size_t) kind] + (int) (o.seed % 3)) % 3;
        s.recipe = recipeText (kind, enrich, s.variant);
        s.cells.resize ((size_t) nT);
        Ctx ctx { enrich, improve, s.variant, o.mood };
        float e = 0.0f;
        for (int i = 0; i < nT; ++i)
        {
            auto& cell = s.cells[(size_t) i];
            cell.track = i;
            const auto& prof = a.tracks[(size_t) i];
            const auto& t = p.tracks[(size_t) i];
            if (! prof.usable) continue;
            // In drops only the tracks that play in the user's strongest moment (all of them in a Session sketch).
            const bool inMain = mainSec == nullptr || contains (mainSec->tracks, i) || a.sessionSketch;
            cell.parts = recipe (kind, s.bars, prof.role, inMain, ctx);
            cell.locked = o.lockedTracks.count (i) > 0 || o.lockedRoles.count (prof.role) > 0
                       || (! o.onlyRoles.empty() && o.onlyRoles.count (prof.role) == 0);
            if (! t.midi) for (auto& part : cell.parts) part.tx = { Tx::Copy };
            if (! cell.parts.empty()) e += ProjectAnalyzer::energyWeight (prof.role);
        }
        s.energy = e;
        if (improve) s.fx = fxFor (kind, s.bars, bpb);
        plan.sections.push_back (s);
        bar += s.bars;
    }
    float eMax = 0.0f;
    for (auto& s : plan.sections) if (! s.existing) eMax = std::max (eMax, s.energy);
    if (mainSec != nullptr)
    {
        float me = 0.0f;
        for (int i : mainSec->tracks) me += ProjectAnalyzer::energyWeight (p.tracks[(size_t) i].role);
        eMax = std::max (eMax, me);
    }
    for (auto& s : plan.sections) if (! s.existing) s.energy = eMax > 0 ? s.energy / eMax : 0.0f;

    // Transition layers on the user's existing sections (a copy of their drum track, empty, with a fill)
    if (improve && o.rangeBars == 0 && o.onlySections.empty())
    {
        int drumTrack = -1;
        for (auto& prof : a.tracks)
            if (prof.usable && prof.role == TrackRole::Drums && prof.drums.rollPitch >= 0 && p.tracks[(size_t) prof.track].midi) { drumTrack = prof.track; break; }
        if (drumTrack >= 0)
            for (size_t k = 0; k + 1 < a.sections.size(); ++k)
            {
                const auto nk = a.sections[k + 1].kind;
                if ((nk == SecKind::Drop || nk == SecKind::Drop2) && a.sections[k].kind != SecKind::Gap)
                {
                    PlanLayer layer;
                    layer.track = drumTrack;
                    layer.startBar = a.sections[k].startBar + a.sections[k].bars - 1;
                    layer.bars = 1;
                    addRoll (layer.notes, a.tracks[(size_t) drumTrack].drums.rollPitch, bpb - 2.0, bpb - 1.0, 0.5, 0.5, 74, 90);
                    addRoll (layer.notes, a.tracks[(size_t) drumTrack].drums.rollPitch, bpb - 1.0, bpb, 0.25, 0.25, 94, 122);
                    clampNotes (layer.notes, bpb);
                    layer.what = utf8 ("roulement avant ton ") + juce::String (secKindName (nk)) + utf8 (" (mesure ") + juce::String (layer.startBar + 1) + ")";
                    plan.layers.push_back (layer);
                }
            }
    }

    renderNotes (plan, p, a);

    // Explanations
    juce::StringArray locked;
    for (int i = 0; i < nT; ++i)
        if (a.tracks[(size_t) i].usable)
        {
            bool l = o.lockedTracks.count (i) > 0 || o.lockedRoles.count (a.tracks[(size_t) i].role) > 0 || (! o.onlyRoles.empty() && o.onlyRoles.count (a.tracks[(size_t) i].role) == 0);
            if (l) locked.add (p.tracks[(size_t) i].name);
        }
    if (! locked.isEmpty()) plan.explain.add (utf8 ("Pistes non modifiées (copies exactes seulement) : ") + locked.joinIntoString (", ") + ".");
    plan.explain.add (juce::String (levelName (o.level)) + " : " + levelHelp (o.level));
    plan.explain.add (utf8 ("Tout est écrit sur TES pistes, avec tes instruments et tes effets. Aucun clip existant n'est modifié ; une place déjà occupée est sautée."));
    if (! plan.layers.empty())
    {
        juce::StringArray w;
        for (auto& l : plan.layers) w.add (l.what);
        plan.explain.add (utf8 ("Transitions sur tes sections existantes, sur une copie vide de ta piste ") + p.tracks[(size_t) plan.layers[0].track].name + " : " + w.joinIntoString (", ") + ".");
    }
    for (auto& m : a.manual) plan.manual.add (m);
    plan.valid = true;
    return plan;
}

// --------------------------------------------------------------------------------------
static juce::var notesVar (const std::vector<LiveNote>& notes)
{
    juce::Array<juce::var> arr;
    for (auto& n : notes)
    {
        juce::Array<juce::var> one;
        one.add (n.pitch);
        one.add (std::round (n.start * 100000.0) / 100000.0);
        one.add (std::round (n.length * 100000.0) / 100000.0);
        one.add (n.velocity);
        arr.add (juce::var (one));
    }
    return juce::var (arr);
}

juce::var buildApplyRequest (const ProjectPlan& plan, const LiveProject& p, const ProjectAnalysis& a, const juce::String& id,
                             const juce::File& fxFolder, double sampleRate, juce::StringArray& notes)
{
    const double bpb = plan.beatsPerBar;
    juce::Array<juce::var> ops;
    auto obj = [] { return juce::var (new juce::DynamicObject()); };

    for (auto& s : plan.sections)
    {
        if (s.existing) continue;
        auto cue = obj();
        cue.getDynamicObject()->setProperty ("op", "cue");
        cue.getDynamicObject()->setProperty ("time", s.startBar * bpb);
        cue.getDynamicObject()->setProperty ("name", "FINISH " + s.name);
        ops.add (cue);
    }

    for (auto& s : plan.sections)
    {
        if (s.existing) continue;
        for (auto& cell : s.cells)
        {
            if (cell.empty()) continue;
            auto& t = p.tracks[(size_t) cell.track];
            auto& prof = a.tracks[(size_t) cell.track];
            const auto* clip = findClip (t, prof.material);
            if (clip == nullptr) continue;
            for (auto& part : cell.parts)
            {
                const double startBeat = (s.startBar + part.startBar) * bpb, len = part.bars * bpb;
                auto op = obj();
                auto* o = op.getDynamicObject();
                o->setProperty ("op", "clip");
                o->setProperty ("track", cell.track);
                o->setProperty ("trackName", t.name);
                o->setProperty ("start", startBeat);
                o->setProperty ("name", "FINISH " + s.name);
                const double unit = copyUnit (*clip);
                auto source = obj();
                source.getDynamicObject()->setProperty ("where", clip->session ? "session" : "arrangement");
                source.getDynamicObject()->setProperty ("index", clip->index);
                source.getDynamicObject()->setProperty ("start", clip->start);
                if (! clip->midi)
                {
                    const int tiles = (int) std::floor (len / unit + 1.0e-6);
                    if (tiles < 1)
                    {
                        notes.add (t.name + utf8 (" : ton clip audio (") + juce::String (unit / bpb, 1) + utf8 (" mesures) est plus long que la partie ")
                                   + s.name + utf8 (" : place-le toi-même si tu le veux."));
                        continue;
                    }
                    o->setProperty ("length", tiles * unit);
                    o->setProperty ("unit", unit);
                    o->setProperty ("source", source);
                }
                else if (part.exactCopy() && ! cell.off && std::fmod (len + 1.0e-6, unit) < 1.0e-3)
                {
                    o->setProperty ("length", len);
                    o->setProperty ("unit", unit);
                    o->setProperty ("source", source);
                    o->setProperty ("fallbackNotes", notesVar (part.notes));
                }
                else
                {
                    if (part.notes.empty()) continue;
                    o->setProperty ("length", len);
                    o->setProperty ("notes", notesVar (part.notes));
                }
                ops.add (op);
            }
        }
    }

    // Layers on the user's existing sections: one empty copy of the track per track involved.
    std::map<int, juce::String> alt;
    for (auto& l : plan.layers)
    {
        if (! alt.count (l.track))
        {
            alt[l.track] = "@alt" + juce::String (l.track);
            auto op = obj();
            op.getDynamicObject()->setProperty ("op", "altTrack");
            op.getDynamicObject()->setProperty ("ref", alt[l.track]);
            op.getDynamicObject()->setProperty ("track", l.track);
            op.getDynamicObject()->setProperty ("trackName", p.tracks[(size_t) l.track].name);
            op.getDynamicObject()->setProperty ("name", p.tracks[(size_t) l.track].name + " FILLS");
            ops.add (op);
        }
        auto op = obj();
        auto* o = op.getDynamicObject();
        o->setProperty ("op", "clip");
        o->setProperty ("track", alt[l.track]);
        o->setProperty ("start", l.startBar * bpb);
        o->setProperty ("length", l.bars * bpb);
        o->setProperty ("name", "FINISH FILL");
        o->setProperty ("notes", notesVar (l.notes));
        ops.add (op);
    }

    // FX track (Improve+)
    bool anyFx = false;
    for (auto& s : plan.sections) if (! s.existing && ! s.fx.empty()) anyFx = true;
    if (anyFx)
    {
        Arrangement tmp;
        tmp.bpm = plan.bpm;
        for (auto& s : plan.sections)
        {
            if (s.existing) continue;
            GenSection g;
            g.name = s.name;
            g.bars = (int) std::round (s.bars * bpb / 4.0);
            g.startBar = s.startBar;
            for (auto f : s.fx) g.clips[(size_t) Lane::Fx].fx.push_back (f);
            tmp.sections.push_back (g);
        }
        juce::Array<juce::var> fxOps;
        for (int i = 0; i < (int) tmp.sections.size(); ++i)
        {
            if (tmp.sections[(size_t) i].clips[(size_t) Lane::Fx].fx.empty()) continue;
            const auto f = ArrangementExport::writeSectionFx (tmp, i, fxFolder, sampleRate);
            if (! f.existsAsFile()) continue;
            auto op = obj();
            auto* o = op.getDynamicObject();
            o->setProperty ("op", "clip");
            o->setProperty ("track", "@fx");
            o->setProperty ("start", tmp.sections[(size_t) i].startBar * bpb);
            o->setProperty ("length", tmp.sections[(size_t) i].bars * 4.0);
            o->setProperty ("file", f.getFullPathName());
            o->setProperty ("name", "FX " + tmp.sections[(size_t) i].name);
            fxOps.add (op);
        }
        if (! fxOps.isEmpty())
        {
            auto op = obj();
            op.getDynamicObject()->setProperty ("op", "newTrack");
            op.getDynamicObject()->setProperty ("ref", "@fx");
            op.getDynamicObject()->setProperty ("kind", "audio");
            op.getDynamicObject()->setProperty ("name", "FINISH FX");
            ops.add (op);
            ops.addArray (fxOps);
        }
    }

    auto req = obj();
    auto* r = req.getDynamicObject();
    r->setProperty ("type", "apply");
    r->setProperty ("id", id);
    r->setProperty ("version", 3);
    r->setProperty ("bpm", plan.bpm);
    r->setProperty ("ops", ops);
    return req;
}

// --------------------------------------------------------------------------------------
static Lane laneFor (TrackRole r)
{
    switch (r)
    {
        case TrackRole::Drums: case TrackRole::Perc: return Lane::Drums;
        case TrackRole::Bass: return Lane::Bass;
        case TrackRole::Chords: return Lane::Chords;
        case TrackRole::Pad: return Lane::Pad;
        default: return Lane::Lead;
    }
}

Arrangement toPreview (const ProjectPlan& plan, const LiveProject& p, const ProjectAnalysis& a, bool newOnly)
{
    Arrangement arr;
    arr.bpm = plan.bpm;
    const double bpb = plan.beatsPerBar;
    for (auto& s : plan.sections)
    {
        if (newOnly && s.existing) continue;
        GenSection g;
        g.role = s.kind == SecKind::Intro ? Role::Intro : s.kind == SecKind::Break ? Role::Break : s.kind == SecKind::Outro ? Role::Outro
               : (s.kind == SecKind::Drop || s.kind == SecKind::Drop2) ? Role::Drop2 : Role::Build;
        g.name = s.name;
        g.bars = juce::jmax (1, (int) std::round (s.bars * bpb / 4.0));
        g.recipe = s.recipe;
        for (auto& cell : s.cells)
        {
            if (cell.empty() || cell.track < 0) continue;
            const auto lane = laneFor (a.tracks[(size_t) cell.track].role);
            if (! p.tracks[(size_t) cell.track].midi) continue;
            for (auto& part : cell.parts)
                for (auto& n : part.notes)
                    g.clips[(size_t) lane].notes.push_back ({ part.startBar * bpb + n.start, n.length, n.pitch, (float) n.velocity / 127.0f });
        }
        for (auto f : s.fx) g.clips[(size_t) Lane::Fx].fx.push_back (f);
        for (auto& c : g.clips)
            std::sort (c.notes.begin(), c.notes.end(), [] (auto& x, auto& y) { return x.beat < y.beat; });
        arr.sections.push_back (g);
    }
    arr.layout();
    arr.summary = utf8 ("Aperçu approximatif avec les sons de FINISH. Le vrai son : tes instruments, dans Live.");
    return arr;
}
}
