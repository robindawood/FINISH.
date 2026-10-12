// FINISH 3 project mode: reading the user's Live set (fixture written by the bridge against the mock Live),
// analysing it, planning the continuation and building the bridge request.
#include <JuceHeader.h>
#include <set>
#include "Project/LiveProject.h"
#include "Project/ProjectAnalyzer.h"
#include "Project/ProjectPlanner.h"
#include "Compose/PreviewSynth.h"
#include "Project/ProjectSession.h"
#include "Project/ProjectCommand.h"

extern int failures, checks;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "  (" #cond ")\n"; } } while (0)

namespace
{
juce::File sourceDir() { return juce::File (FINISH_SOURCE_DIR); }

LiveProject loadDemo()
{
    return LiveProject::fromFile (sourceDir().getChildFile ("Tests/fixtures/project_demo.json"));
}

int trackIndex (const LiveProject& p, const juce::String& name)
{
    for (auto& t : p.tracks) if (t.name == name) return t.index;
    return -1;
}

bool inKey (int pitch, int root, bool minor)
{
    static const int maj[] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int rel = ((pitch - root) % 12 + 12) % 12;
    for (int i = 0; i < 7; ++i) if ((minor ? mnr : maj)[i] == rel) return true;
    return false;
}

const PlanSection* findNew (const ProjectPlan& plan, SecKind k)
{
    for (auto& s : plan.sections) if (! s.existing && s.kind == k) return &s;
    return nullptr;
}

void testReadAndAnalyse()
{
    std::cout << "[project] read + analyse the demo set\n";
    auto p = loadDemo();
    CHECK (p.valid, "fixture parsed");
    CHECK (p.tracks.size() == 8, "8 tracks");
    CHECK (std::abs (p.tempo - 115.0) < 1e-9, "tempo from Live");
    CHECK (p.countNotes() > 300, "notes read");
    auto a = ProjectAnalyzer::analyse (p);
    CHECK (a.valid, "analysis valid");
    auto role = [&] (const char* n) { return p.tracks[(size_t) trackIndex (p, n)].role; };
    CHECK (role ("Oboe") == TrackRole::Lead, "Oboe = lead");
    CHECK (role ("Key Synth Pad") == TrackRole::Pad || role ("Key Synth Pad") == TrackRole::Chords, "Key Synth Pad = pad/chords");
    CHECK (role ("Flute") == TrackRole::Lead, "Flute = lead");
    CHECK (role ("Vintage Low Synth") == TrackRole::Bass, "Vintage Low Synth = bass");
    CHECK (role ("Flavonoid Drums") == TrackRole::Drums, "Flavonoid Drums = drums");
    CHECK (role ("Escape Perc") == TrackRole::Perc, "Escape Perc = perc");
    CHECK (role ("Doppler Clap") == TrackRole::Perc, "Doppler Clap = perc");
    CHECK (role ("Uplifting House Hat") == TrackRole::Perc, "audio hat = perc");
    CHECK (a.keyRoot == 2 && a.minor, "key D minor (got " << a.keyName() << ")");
    std::cout << "  key " << a.keyName() << " conf " << a.keyConfidence << "\n";

    int real = 0;
    for (auto& s : a.sections) if (s.kind != SecKind::Gap) ++real;
    CHECK (real == 2, "two existing sections");
    CHECK (a.sections.size() >= 2 && a.sections[0].kind == SecKind::Intro && a.sections[0].startBar == 0 && a.sections[0].bars == 16, "intro bars 1-16");
    CHECK (a.sections.size() >= 2 && a.sections[1].kind == SecKind::Drop && a.sections[1].startBar == 16 && a.sections[1].bars == 8, "drop bars 17-24");
    CHECK (a.mainSection == 1, "main section = drop");
    const auto& drums = a.tracks[(size_t) trackIndex (p, "Flavonoid Drums")].drums;
    CHECK (drums.kick == std::vector<int> { 36 }, "kick pitch from pad names");
    CHECK (drums.rollPitch == 39, "roll on the clap");
    bool missingBreak = false, missingOutro = false;
    for (auto k : a.missing) { missingBreak |= k == SecKind::Break; missingOutro |= k == SecKind::Outro; }
    CHECK (missingBreak && missingOutro, "break and outro missing");
    const auto rep = ProjectAnalyzer::report (a, p);
    CHECK (rep.contains (utf8 ("Pas de montée")), "report mentions the missing build");
    CHECK (rep.contains ("Session"), "report mentions the unused Session idea");
    std::cout << rep;
}

void testPlanEnrich()
{
    std::cout << "[project] plan: continue the track (ENRICHIR)\n";
    auto p = loadDemo();
    auto a = ProjectAnalyzer::analyse (p);
    PlanOptions o;
    o.level = Level::Enrich;
    auto plan = ProjectPlanner::makePlan (p, a, o);
    CHECK (plan.valid, "plan valid");
    CHECK (! plan.fullVersion && plan.firstNewBar == 24, "continues after bar 24");
    std::vector<SecKind> kinds;
    for (auto& s : plan.sections) if (! s.existing) kinds.push_back (s.kind);
    CHECK ((kinds == std::vector<SecKind> { SecKind::Break, SecKind::Build2, SecKind::Drop2, SecKind::Outro }), "break, build 2, drop 2, outro");
    for (auto& l : plan.explain) std::cout << "  > " << l << "\n";

    int bad = 0, notes = 0, outside = 0;
    for (auto& s : plan.sections)
        for (auto& c : s.cells)
            for (auto& part : c.parts)
            {
                const double L = part.bars * a.beatsPerBar;
                for (auto& n : part.notes)
                {
                    ++notes;
                    if (n.start < 0 || n.start + n.length > L + 1e-6 || n.velocity < 1 || n.velocity > 127) ++outside;
                    const auto role = a.tracks[(size_t) c.track].role;
                    if ((role == TrackRole::Lead || role == TrackRole::Bass || role == TrackRole::Pad || role == TrackRole::Chords) && ! inKey (n.pitch, 2, true)) ++bad;
                }
            }
    CHECK (notes > 200, "notes generated from the user's clips (" << notes << ")");
    CHECK (outside == 0, "every note inside its part");
    CHECK (bad == 0, "every melodic note in D minor (" << bad << " out)");

    // Drop 2: the oboe answers itself in the 2nd half; drums carry the clap fills.
    const auto* d2 = findNew (plan, SecKind::Drop2);
    CHECK (d2 != nullptr, "drop 2 planned");
    if (d2 != nullptr)
    {
        const auto& oboe = d2->cells[(size_t) trackIndex (p, "Oboe")];
        CHECK (oboe.parts.size() == 2, "oboe: copy then development");
        if (oboe.parts.size() == 2)
        {
            CHECK (oboe.parts[0].exactCopy(), "first half exact copy");
            const auto& dev = oboe.parts[1];
            const auto src = LiveNotes::tiled (p.tracks[0].arrangement[0], dev.bars * a.beatsPerBar);
            std::set<std::pair<int, int>> a1, a2;
            for (auto& n : src) a1.insert ({ n.pitch, (int) std::round (n.start * 4) });
            for (auto& n : dev.notes) a2.insert ({ n.pitch, (int) std::round (n.start * 4) });
            CHECK (a1 != a2 && ! dev.notes.empty(), "development differs from the source motif");
            int keptRhythm = 0;
            for (auto& n : dev.notes) for (auto& s : src) if (std::abs (std::fmod (n.start, 4.0) - std::fmod (s.start, 4.0)) < 1e-6) { ++keptRhythm; break; }
            CHECK (keptRhythm == (int) dev.notes.size(), "development keeps the motif's rhythm");
        }
        const auto& dr = d2->cells[(size_t) trackIndex (p, "Flavonoid Drums")];
        int claps = 0;
        for (auto& part : dr.parts) for (auto& n : part.notes) if (n.pitch == 39 && n.start >= part.bars * 4.0 - 1.0) ++claps;
        CHECK (claps >= 4, "fill on the user's clap at the end of each 8 bars");
        const auto& flute = d2->cells[(size_t) trackIndex (p, "Flute")];
        CHECK (! flute.parts.empty(), "flute (in the drop) plays in drop 2");
    }
    const auto* br = findNew (plan, SecKind::Break);
    if (br != nullptr)
    {
        CHECK (br->cells[(size_t) trackIndex (p, "Vintage Low Synth")].parts.empty()
               || br->cells[(size_t) trackIndex (p, "Vintage Low Synth")].parts[0].startBar > 0, "no bass at the start of the break");
        int kicks = 0;
        for (auto& part : br->cells[(size_t) trackIndex (p, "Flavonoid Drums")].parts) for (auto& n : part.notes) if (n.pitch == 36) ++kicks;
        CHECK (kicks == 0 || br->variant == 2, "no kick in the break");
    }

    // Request
    juce::StringArray notesOut;
    auto out = sourceDir().getChildFile ("build/test_out");
    out.createDirectory();
    auto req = ProjectPlanner::buildApplyRequest (plan, p, a, "plan-enrich", out.getChildFile ("fx"), 44100.0, notesOut);
    auto* ops = req["ops"].getArray();
    CHECK (ops != nullptr && ops->size() > 20, "ops built");
    int copies = 0, varied = 0, cues = 0, newTracks = 0;
    for (auto& op : *ops)
    {
        if (op["op"].toString() == "cue") ++cues;
        if (op["op"].toString() == "newTrack" || op["op"].toString() == "altTrack") ++newTracks;
        if (op["op"].toString() == "clip") { if (op.hasProperty ("notes")) ++varied; else ++copies; }
    }
    CHECK (cues == 4, "a locator per new section");
    CHECK (newTracks == 0, "ENRICHIR adds no track");
    CHECK (copies > 5 && varied > 5, "copies and variations (" << copies << " / " << varied << ")");
    out.getChildFile ("plan_request.json").replaceWithText (juce::JSON::toString (req));
    std::cout << "  ops: " << ops->size() << " (" << copies << " copies, " << varied << " variations)\n";

    auto prev = ProjectPlanner::toPreview (plan, p, a);
    CHECK (prev.sections.size() == 4 && prev.totalBars() == 56, "preview arrangement");
    auto audio = PreviewSynth::renderOffline (PreviewSynth::compile (prev), prev.bpm, 22050.0, 0.0, 16.0);
    CHECK (audio.getMagnitude (0, audio.getNumSamples()) > 0.01f, "preview renders sound");
}

void testPlanLevelsAndConstraints()
{
    std::cout << "[project] levels, constraints, range, full version\n";
    auto p = loadDemo();
    auto a = ProjectAnalyzer::analyse (p);

    // COMPLETER: nothing but the user's own notes (subsets of their clips, same pitch and position).
    PlanOptions o;
    o.level = Level::Complete;
    auto plan = ProjectPlanner::makePlan (p, a, o);
    int foreign = 0;
    for (auto& s : plan.sections)
        if (! s.existing)
            for (auto& c : s.cells)
            {
                const auto* clip = &p.tracks[(size_t) c.track];
                if (clip->arrangement.empty() || ! clip->midi) continue;
                for (auto& part : c.parts)
                {
                    const auto& prof = a.tracks[(size_t) c.track];
                    const LiveClip* m = nullptr;
                    for (auto& k : clip->arrangement) if (std::abs (k.start - prof.material.start) < 1e-6) m = &k;
                    if (m == nullptr) continue;
                    const auto src = LiveNotes::tiled (*m, part.bars * a.beatsPerBar);
                    for (auto& n : part.notes)
                    {
                        bool found = false;
                        for (auto& s2 : src) if (s2.pitch == n.pitch && std::abs (s2.start - n.start) < 1e-6) { found = true; break; }
                        if (! found) ++foreign;
                    }
                }
            }
    CHECK (foreign == 0, "COMPLETER writes no new note (" << foreign << ")");

    // "sans toucher à ma mélodie principale"
    o.level = Level::Enrich;
    o.lockedRoles = { TrackRole::Lead };
    plan = ProjectPlanner::makePlan (p, a, o);
    int changedLead = 0;
    for (auto& s : plan.sections)
        if (! s.existing)
            for (auto& c : s.cells)
                if (a.tracks[(size_t) c.track].role == TrackRole::Lead)
                    for (auto& part : c.parts) if (! part.exactCopy()) ++changedLead;
    CHECK (changedLead == 0, "locked melody = exact copies only");

    // "fais monter l'énergie pendant les 16 prochaines mesures"
    PlanOptions r;
    r.rangeBars = 16;
    plan = ProjectPlanner::makePlan (p, a, r);
    CHECK (plan.newSectionCount() == 1 && findNew (plan, SecKind::Rise) != nullptr && findNew (plan, SecKind::Rise)->bars == 16, "one 16-bar rise");
    if (const auto* rise = findNew (plan, SecKind::Rise))
    {
        const auto& dr = rise->cells[(size_t) trackIndex (p, "Flavonoid Drums")];
        CHECK (! dr.parts.empty() && dr.parts[0].startBar >= 8, "drums come in during the second half");
        const auto& pad = rise->cells[(size_t) trackIndex (p, "Key Synth Pad")];
        CHECK (! pad.parts.empty() && pad.parts[0].startBar == 0, "pad from the start");
    }

    // AMELIORER: transition layer before the existing drop + FX track
    PlanOptions im;
    im.level = Level::Improve;
    plan = ProjectPlanner::makePlan (p, a, im);
    CHECK (plan.layers.size() == 1 && plan.layers[0].startBar == 15, "fill layered on bar 16, before the user's drop");
    juce::StringArray notesOut;
    auto out = sourceDir().getChildFile ("build/test_out");
    auto req = ProjectPlanner::buildApplyRequest (plan, p, a, "plan-improve", out.getChildFile ("fx"), 44100.0, notesOut);
    int alt = 0, fx = 0;
    for (auto& op : *req["ops"].getArray())
    {
        if (op["op"].toString() == "altTrack") ++alt;
        if (op["op"].toString() == "clip" && op.hasProperty ("file") && juce::File (op["file"].toString()).existsAsFile()) ++fx;
    }
    CHECK (alt == 1 && fx == 0, "AMELIORER: copy of the user's drum track, no FINISH sound by default (" << alt << ", " << fx << ")");
    int variedWithSource = 0, varied = 0;
    for (auto& op : *req["ops"].getArray())
        if (op["op"].toString() == "clip" && op.hasProperty ("notes") && ! op["track"].toString().startsWith ("@"))
        {
            ++varied;
            if (op.hasProperty ("source") && op.hasProperty ("unit")) ++variedWithSource;
        }
    CHECK (varied > 5 && variedWithSource == varied, "every variation is built on a copy of the user's clip (" << variedWithSource << "/" << varied << ")");
    out.getChildFile ("plan_request_improve.json").replaceWithText (juce::JSON::toString (req));
    {
        PlanOptions fxOn;
        fxOn.level = Level::Improve;
        fxOn.finishFx = true;
        auto planFx = ProjectPlanner::makePlan (p, a, fxOn);
        auto reqFx = ProjectPlanner::buildApplyRequest (planFx, p, a, "plan-fx", out.getChildFile ("fx"), 44100.0, notesOut);
        int fxClips = 0;
        for (auto& op : *reqFx["ops"].getArray())
            if (op["op"].toString() == "clip" && op.hasProperty ("file") && juce::File (op["file"].toString()).existsAsFile()) ++fxClips;
        CHECK (fxClips >= 3, "SONS FINISH on: FX track with risers/impacts (" << fxClips << ")");
    }

    // Sound inventory
    const auto inv = ProjectAnalyzer::soundInventory (a, p);
    CHECK (inv.contains ("Dreamy Oboe") && inv.contains ("Kick.wav") && inv.contains ("F1 Freq") && inv.contains (utf8 ("aucun par défaut")),
           "sound inventory lists presets, samples, automation, and the no-foreign-sound rule");

    // ESQUISSE routed onto the user's own tracks
    {
        Arrangement sk;
        sk.bpm = 115.0;
        GenSection g;
        g.role = Role::Break; g.name = "BREAK"; g.bars = 8;
        for (int b = 0; b < 8; ++b)
        {
            for (int k = 0; k < 4; ++k) g.clips[(size_t) Lane::Drums].notes.push_back ({ b * 4.0 + k, 0.2, 36, 1.0f });
            g.clips[(size_t) Lane::Drums].notes.push_back ({ b * 4.0 + 1, 0.2, 38, 0.9f });
            g.clips[(size_t) Lane::Drums].notes.push_back ({ b * 4.0 + 0.5, 0.2, 42, 0.7f });
            g.clips[(size_t) Lane::Drums].notes.push_back ({ b * 4.0, 0.5, 49, 0.8f });   // crash: the user's kit has none
            g.clips[(size_t) Lane::Bass].notes.push_back ({ b * 4.0, 1.0, 33, 0.9f });    // A in A minor
            g.clips[(size_t) Lane::Lead].notes.push_back ({ b * 4.0, 1.0, 69, 0.8f });
        }
        sk.sections.push_back (g);
        sk.layout();
        auto sp = ProjectPlanner::planFromSketch (sk, 9, true, p, a);
        CHECK (sp.valid && sp.newSectionCount() == 1, "sketch plan");
        const PlanSection* ns = nullptr;
        for (auto& sec : sp.sections) if (! sec.existing) ns = &sec;
        if (ns != nullptr)
        {
            const auto& drumsCell = ns->cells[(size_t) trackIndex (p, "Flavonoid Drums")];
            std::set<int> pitches;
            for (auto& part : drumsCell.parts) for (auto& n : part.notes) pitches.insert (n.pitch);
            CHECK ((pitches == std::set<int> { 36, 39, 42 }), "sketch drums land on the user's own pads (kick 36, clap 39, hat 42)");
            const auto& bassCell = ns->cells[(size_t) trackIndex (p, "Vintage Low Synth")];
            CHECK (! bassCell.parts.empty() && ! bassCell.parts[0].notes.empty() && bassCell.parts[0].notes[0].pitch == 38,
                   "sketch bass on the user's bass track, transposed into D minor");
            CHECK (! ns->cells[(size_t) trackIndex (p, "Oboe")].parts.empty(), "sketch lead on the user's lead track");
            bool crashReported = false;
            for (auto& m : sp.manual) crashReported |= m.contains ("sans pad");
            CHECK (crashReported, "a hit with no matching pad is reported, not replaced");
            auto sreq = ProjectPlanner::buildApplyRequest (sp, p, a, "sketch", out.getChildFile ("fx"), 44100.0, notesOut);
            int newTracks = 0;
            for (auto& op : *sreq["ops"].getArray()) if (op["op"].toString() == "newTrack") ++newTracks;
            CHECK (newTracks == 0, "the sketch adds no track and loads no instrument");
        }
    }

    // 3 versions of a build
    std::set<juce::String> versions;
    for (int v = 0; v < 3; ++v)
    {
        PlanOptions b;
        b.onlySections = { SecKind::Build };
        b.variants[(size_t) SecKind::Build] = v;
        auto pl = ProjectPlanner::makePlan (p, a, b);
        juce::String sig;
        if (auto* s = findNew (pl, SecKind::Build)) for (auto& c : s->cells) sig << c.summary() << "|";
        versions.insert (sig);
    }
    CHECK (versions.size() == 3, "three different build versions");

    // A sketch that starts on its drop: full version written after it.
    auto p2 = loadDemo();
    for (auto& t : p2.tracks)
        t.arrangement.erase (std::remove_if (t.arrangement.begin(), t.arrangement.end(), [] (auto& c) { return c.start < 1.0; }), t.arrangement.end());
    for (auto& t : p2.tracks) for (auto& c : t.arrangement) { c.start -= 64.0; c.end -= 64.0; }
    auto a2 = ProjectAnalyzer::analyse (p2);
    auto plan2 = ProjectPlanner::makePlan (p2, a2, PlanOptions {});
    CHECK (plan2.fullVersion && plan2.firstNewBar == 12 && plan2.newSectionCount() == 7, "full version after an 8-bar drop sketch (bar " << plan2.firstNewBar + 1 << ")");

    // Session-only sketch
    auto p3 = loadDemo();
    for (auto& t : p3.tracks) { for (auto& c : t.arrangement) { auto s = c; s.session = true; s.index = (int) t.session.size() + 1; s.start = 0; s.end = s.loopLength(); t.session.push_back (s); } t.arrangement.clear(); }
    auto a3 = ProjectAnalyzer::analyse (p3);
    auto plan3 = ProjectPlanner::makePlan (p3, a3, PlanOptions {});
    CHECK (a3.sessionSketch && plan3.fullVersion && plan3.firstNewBar == 0, "Session sketch -> full arrangement from bar 1");
}
}

namespace
{
void testCommands()
{
    std::cout << "[project] free commands\n";
    using K = ProjectCommand::Kind;
    struct Case { const char* text; K kind; std::function<bool (const ProjectCommand&)> extra; const char* what; };
    const Case cases[] = {
        { "Termine mon morceau sans toucher à ma mélodie principale", K::Finish, [] (auto& c) { return c.lockRoles.count (TrackRole::Lead) > 0; }, "finish + lock melody" },
        { "Fais monter progressivement l'énergie pendant les 16 prochaines mesures", K::Rise, [] (auto& c) { return c.rangeBars == 16; }, "rise 16 bars" },
        { "Rends mon drop plus puissant sans modifier la ligne de basse", K::Sections, [] (auto& c) { return c.lockRoles.count (TrackRole::Bass) > 0 && c.mood.intense > 0
                                                                                                   && std::find (c.sections.begin(), c.sections.end(), SecKind::Drop2) != c.sections.end(); }, "drop 2 stronger, bass locked" },
        { "Ajoute des variations de drums en conservant mon groove", K::Variation, [] (auto& c) { return c.onlyRoles.count (TrackRole::Drums) > 0 && c.lockRoles.empty(); }, "drum variations" },
        { "Crée une transition plus fluide entre le break et le drop", K::Sections, [] (auto& c) { return c.sections.size() == 1 && c.sections[0] == SecKind::Build && c.mood.progressive > 0; }, "smooth transition" },
        { "Analyse mon mix et explique-moi pourquoi il manque de punch", K::AnalyseMix, [] (auto& c) { return c.mixTopic == "punch"; }, "mix question" },
        { "Améliore le mastering en conservant le caractère sombre", K::Master, [] (auto& c) { return c.keepDark; }, "master keep dark" },
        { "Rends cette basse plus agressive", K::Variation, [] (auto& c) { return c.onlyRoles.count (TrackRole::Bass) > 0 && ! c.notes.isEmpty(); }, "aggressive bass (with honest note)" },
        { "Propose trois versions différentes de mon build-up", K::Sections, [] (auto& c) { return c.versions == 3 && c.sections.size() == 1 && c.sections[0] == SecKind::Build; }, "3 builds" },
        { "Écoute et analyse ce que j'ai déjà créé, puis dis-moi ce qui manque", K::AnalyseProject, [] (auto&) { return true; }, "what is missing" },
        { "plus subtil", K::Iterate, [] (auto& c) { return c.levelDelta == -1; }, "subtler" },
        { "plus sombre", K::Iterate, [] (auto& c) { return c.mood.darker > 0; }, "darker" },
        { "garde la version précédente", K::KeepPrevious, [] (auto&) { return true; }, "keep previous" },
        { "modifie uniquement la basse", K::Variation, [] (auto& c) { return c.onlyRoles.count (TrackRole::Bass) > 0; }, "only the bass" },
        { "envoie dans Live", K::Send, [] (auto&) { return true; }, "send" },
        { "annule l'envoi", K::UndoSend, [] (auto&) { return true; }, "undo send" },
        { "finish my track without touching the bassline", K::Finish, [] (auto& c) { return c.lockRoles.count (TrackRole::Bass) > 0; }, "english" },
    };
    for (auto& c : cases)
    {
        const auto cmd = ProjectCommand::parse (utf8 (c.text));
        CHECK (cmd.kind == c.kind && c.extra (cmd), c.what << " <- " << c.text << " (kind " << (int) cmd.kind << ")");
    }
}

// The whole chain against the real Remote Script running on a mock Live (Tests/live_mock_server.py):
// read the set, plan, command, write, read back, undo.
void testLiveEndToEnd()
{
    const auto home = juce::SystemStats::getEnvironmentVariable ("FINISH_E2E_HOME", {});
    if (home.isEmpty()) return;
    std::cout << "[project] end to end with the Remote Script on a mock Live (" << home << ")\n";
    ProjectSession s;
    auto pump = [&] (std::function<bool()> done, double seconds)
    {
        const auto until = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
        while (juce::Time::getMillisecondCounterHiRes() < until)
        {
            s.tick();
            if (done()) return true;
            juce::Thread::sleep (20);
        }
        return false;
    };
    CHECK (pump ([&] { s.refreshDiagnosis(); return s.getDiagnosis().connected(); }, 10.0), "bridge connected (" << s.getDiagnosis().title << ")");
    juce::String err;
    CHECK (s.readProject (err), "snapshot requested: " << err);
    CHECK (pump ([&] { return s.hasPlan(); }, 15.0), "project read and planned");
    if (! s.hasPlan()) return;
    s.takeFreshPlan();
    CHECK (s.getProject().tracks.size() == 8 && s.getAnalysis().keyRoot == 2, "the mock set as read through Live");
    auto out = s.apply (ProjectCommand::parse (utf8 ("termine mon morceau sans toucher à ma mélodie principale")));
    int changedLead = 0;
    for (auto& sec : s.getPlan().sections)
        if (! sec.existing)
            for (auto& c : sec.cells)
                if (s.getAnalysis().tracks[(size_t) c.track].role == TrackRole::Lead)
                    for (auto& part : c.parts) if (! part.exactCopy()) ++changedLead;
    CHECK (changedLead == 0, "command applied: melody untouched in the plan");
    const int clipsBefore = s.getProject().countClips (true);
    CHECK (s.send (48000.0, err), "send: " << err);
    CHECK (pump ([&] { return s.takeLiveResult(); }, 15.0), "Live answered the apply");
    std::cout << "  " << s.getLastReport().replace ("\n", "\n  ") << "\n";
    CHECK (s.getLastReport().contains ("clips ecrits"), "clips written");
    CHECK (s.getLastReport().contains (utf8 ("FIDÉLITÉ")), "fidelity verified after writing: no instrument or effect changed");
    CHECK (pump ([&] { return s.getPhase() == ProjectSession::Phase::Ready; }, 15.0), "set read again after writing");
    const int clipsAfter = s.getProject().countClips (true);
    {
        bool consistent = s.hasPlan();
        for (auto& sec : s.getPlan().sections) consistent &= sec.cells.size() == s.getProject().tracks.size();
        CHECK (consistent, "plan follows the set after it changed (new tracks): no stale cells for the UI");
    }
    CHECK (clipsAfter > clipsBefore + 10, "the new clips are in the set (" << clipsBefore << " -> " << clipsAfter << ")");
    CHECK (s.hasSent() && s.undoSend (err), "undo: " << err);
    CHECK (pump ([&] { return s.takeLiveResult(); }, 15.0), "Live answered the undo");
    CHECK (s.getLastReport().contains ("annule"), "send undone: " << s.getLastReport());
    CHECK (pump ([&] { return s.getPhase() == ProjectSession::Phase::Ready && s.getProject().countClips (true) == clipsBefore; }, 15.0),
           "back to the original set (" << s.getProject().countClips (true) << " clips)");
    // Hear it in Live with the project's real sounds: write, play from 2 bars before the new parts, then undo.
    CHECK (s.sendAndListen (48000.0, err), "write + listen: " << err);
    CHECK (pump ([&] { return s.takeLiveResult(); }, 15.0), "Live answered the audition write");
    std::cout << "  " << s.getLastReport().replace ("\n", "\n  ") << "\n";
    CHECK (s.getLastReport().contains ("Automation") && s.getLastReport().contains ("F1 Freq"),
           "the bass filter automation that cannot be extended is reported");
    CHECK (pump ([&] { return s.getPhase() == ProjectSession::Phase::Ready && s.hasSent(); }, 15.0), "audition finished");
    CHECK (s.undoSend (err), "undo after audition");
    CHECK (pump ([&] { return s.takeLiveResult(); }, 15.0), "undo answered");
    pump ([&] { return s.getPhase() == ProjectSession::Phase::Ready; }, 15.0);
    juce::File (home).getChildFile ("stop").create();
    juce::Thread::sleep (300);
    {
        const auto tr = juce::JSON::parse (juce::File (home).getChildFile ("transport.json").loadFileAsString());
        const double from = (double) tr.getProperty ("played_from", -1.0);
        CHECK (std::abs (from - 22.0 * 4.0) < 1e-6, "Live played from bar 23, 2 bars before the new parts (got beat " << from << ")");
    }
    for (auto& e : s.getJournal()) std::cout << "  journal: " << e.text.upToFirstOccurrenceOf ("\n", false, false) << "\n";
}
}

void runProjectTests()
{
    testCommands();
    testLiveEndToEnd();
    testReadAndAnalyse();
    testPlanEnrich();
    testPlanLevelsAndConstraints();
}
