// FINISH COMPOSE tests: capture -> musical analysis -> arrangement generation -> preview / export.
#include <JuceHeader.h>
#include "Compose/MusicCapture.h"
#include "Compose/MusicAnalyzer.h"
#include "Compose/ArrangementGenerator.h"
#include "Compose/PreviewSynth.h"
#include "Compose/ArrangementExport.h"
#include "Compose/Composer.h"

extern int failures, checks;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "  (" #cond ")\n"; } } while (0)

namespace
{
    // A small "drop" written as an arrangement section so the preview synth can render it.
    struct Song
    {
        double bpm = 126.0;
        int keyRoot = 9; bool minor = true;   // A minor
        int bars = 16;
        bool drums = true, bass = true, chords = true, lead = true;
    };

    Arrangement makeReference (const Song& s)
    {
        Arrangement arr;
        arr.bpm = s.bpm;
        GenSection sec;
        sec.role = Role::Drop; sec.bars = s.bars;
        const int prog[4] = { 9, 5, 0, 7 };          // Am F C G
        const bool minorQ[4] = { true, false, false, false };
        for (int b = 0; b < s.bars; ++b)
        {
            const double t = b * 4.0;
            if (s.drums)
                for (int st = 0; st < 16; ++st)
                {
                    if (st % 4 == 0) sec.clips[0].notes.push_back ({ t + st * 0.25, 0.1, Drum::kick, 1.0f });
                    if (st == 4 || st == 12) sec.clips[0].notes.push_back ({ t + st * 0.25, 0.1, Drum::clap, 0.9f });
                    if (st % 4 == 2) sec.clips[0].notes.push_back ({ t + st * 0.25, 0.2, Drum::openHat, 0.8f });
                }
            const int root = prog[b % 4];
            if (s.bass)
                for (int st : { 2, 6, 10, 14 })
                    sec.clips[1].notes.push_back ({ t + st * 0.25, 0.45, 33 + ((root - 9 + 12) % 12), 0.9f });
            if (s.chords)
            {
                const int third = minorQ[b % 4] ? 3 : 4;
                for (int iv : { 0, third, 7 })
                    sec.clips[4].notes.push_back ({ t, 3.9, 57 + ((root - 9 + 12) % 12 + iv) % 12, 0.8f });
            }
        }
        if (s.lead)
        {
            // Two-bar motif in A minor, repeated.
            const std::vector<std::pair<double, int>> motif = { { 0.0, 76 }, { 0.75, 74 }, { 1.5, 72 }, { 2.0, 69 }, { 3.0, 72 },
                                                                 { 4.0, 76 }, { 4.75, 77 }, { 5.5, 76 }, { 6.0, 74 }, { 7.0, 69 } };
            for (int rep = 0; rep < s.bars / 2; ++rep)
                for (auto& [b, p] : motif) sec.clips[3].notes.push_back ({ rep * 8.0 + b, 0.6, p, 0.9f });
        }
        arr.sections.push_back (sec);
        arr.layout();
        return arr;
    }

    MusicCapture::Snapshot capture (const juce::AudioBuffer<float>& audio, double sr, double bpm, double songStart)
    {
        MusicCapture cap;
        cap.prepare (sr);
        const int block = 480;
        for (int p = 0; p < audio.getNumSamples(); p += block)
        {
            const int n = std::min (block, audio.getNumSamples() - p);
            const double t = songStart + p / sr;
            cap.push (audio.getReadPointer (0) + p, audio.getReadPointer (1) + p, n, t, t * bpm / 60.0, bpm, true);
        }
        return cap.snapshot();
    }

    bool inScale (int pitch, int root, bool minor)
    {
        static const int majS[7] = { 0, 2, 4, 5, 7, 9, 11 }, minS[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int pc = ((pitch - root) % 12 + 12) % 12;
        for (int i = 0; i < 7; ++i) if ((minor ? minS : majS)[i] == pc) return true;
        return false;
    }

    void printDNA (const MusicalDNA& d)
    {
        std::cout << "    " << d.bpm << " BPM, key " << d.keyName() << " (conf " << d.keyConfidence << "), style " << styleName (d.style)
                  << " (" << d.styleConfidence << "), bars " << d.barsAnalysed
                  << "\n    drums " << d.hasDrums << " 4otf " << d.fourOnFloor << " bass " << d.hasBass << " (" << d.bassLine.size() << " notes / " << d.bassBars << " bars)"
                  << " chords " << d.hasChords << " melody " << d.hasMelody << " (" << d.melody.size() << " notes / " << d.melodyBars << " bars, conf " << d.melodyConfidence << ")"
                  << " swing " << d.swing << " energy " << d.energy << "\n    chords:";
        for (size_t i = 0; i < d.chordRoots.size(); ++i) std::cout << " " << pitchClassName (d.chordRoots[i]) << (d.chordMinor[i] ? "m" : "");
        std::cout << "\n    kick:";
        for (auto v : d.kick) std::cout << (v > 0.5f ? "X" : ".");
        std::cout << "  snare:";
        for (auto v : d.snare) std::cout << (v > 0.55f ? "X" : ".");
        std::cout << "  hats:";
        for (auto v : d.hats) std::cout << (v > 0.4f ? "X" : ".");
        std::cout << "\n    melody:";
        for (auto& n : d.melody) std::cout << " " << n.beat << ":" << n.pitch;
        std::cout << "\n    bass:";
        for (auto& n : d.bassLine) std::cout << " " << n.beat << ":" << n.pitch;
        std::cout << "\n";
    }
}

static void debugSynth()
{
    {
        PreviewSynth::Sequence k1; k1.notes.push_back ({ 0.0, 0.1, 0, 36, 1.0f }); k1.lengthBeats = 4;
        const auto a = PreviewSynth::renderOffline (k1, 120.0, 48000.0, 0.0, 2.0);
        for (int k = 0; k < 12; ++k) std::cout << "  kick " << k * 50 << "ms " << a.getMagnitude (0, k * 2400, 2400) << "\n";
        for (int i = 0; i < 4800; i += 200) std::cout << "  ks " << i << " " << a.getSample (0, i) << "\n";
    }
    PreviewSynth::Sequence q;
    for (int b = 0; b < 4; ++b) q.notes.push_back ({ (double) b, 0.1, 0, 36, 1.0f });
    for (int b = 0; b < 4; ++b) q.notes.push_back ({ b + 0.5, 0.45, 1, 33, 0.9f });
    std::stable_sort (q.notes.begin(), q.notes.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    q.lengthBeats = 4;
    const auto a = PreviewSynth::renderOffline (q, 120.0, 48000.0, 0.0, 4.0);
    for (int k = 0; k < 16; ++k) std::cout << "  q" << k << " " << a.getMagnitude (0, k * 6000, 6000) << "\n";
    Song s; s.chords = false; s.lead = false;
    const auto seq = PreviewSynth::compile (makeReference (s));
    const auto b = PreviewSynth::renderOffline (seq, s.bpm, 48000.0, 0.0, 4.0);
    const int qs = (int) (48000.0 * 60.0 / s.bpm / 4.0);
    for (int k = 0; k < 16; ++k) std::cout << "  song q" << k << " " << b.getMagnitude (0, k * qs, qs) << "\n";
    const auto snap = capture (b, 48000.0, s.bpm, 0.0);
    const int qq = (int) (snap.rate * 60.0 / s.bpm / 4.0);
    std::cout << "  snap rate " << snap.rate << " start " << snap.startTime << " beat " << snap.beatAtStart << " n " << snap.audio.size() << "\n";
    for (int k = 0; k < 16; ++k) { float m = 0; for (int i = k * qq; i < (k + 1) * qq && i < (int) snap.audio.size(); ++i) m = std::max (m, std::abs (snap.audio[(size_t) i])); std::cout << "  snap q" << k << " " << m << "\n"; }
    for (int i = 0; i < 12; ++i) std::cout << "  ev " << seq.notes[(size_t) i].beat << " lane " << (int) seq.notes[(size_t) i].lane << " p " << (int) seq.notes[(size_t) i].pitch << "\n";
}

static void testMusicAnalysis()
{
    if (std::getenv ("FINISH_DEBUG_SYNTH") != nullptr) debugSynth();
    std::cout << "COMPOSE: musical analysis (tempo, key, groove, bass, chords, melody)\n";
    const double sr = 48000.0;
    {
        Song s;
        const auto ref = makeReference (s);
        const auto seq = PreviewSynth::compile (ref);
        const auto audio = PreviewSynth::renderOffline (seq, s.bpm, sr, 0.0, s.bars * 4.0);
        CHECK (audio.getMagnitude (0, audio.getNumSamples()) > 0.05f, "preview synth rendered silence");
        const auto dna = MusicAnalyzer::analyze (capture (audio, sr, s.bpm, 0.0));
        std::cout << "  full drop:\n"; printDNA (dna);
        CHECK (dna.valid, "analysis failed on a full drop");
        CHECK (std::abs (dna.bpm - 126.0) < 0.01, "host tempo not used");
        CHECK (dna.keyRoot == 9 && dna.minor, "key should be A minor, got " + dna.keyName());
        CHECK (dna.hasDrums && dna.fourOnFloor, "four-on-the-floor kick not detected");
        CHECK (dna.hasBass, "bass not detected");
        CHECK (dna.hasMelody, "melody not detected");
        int bassRootsOk = 0;
        for (auto& n : dna.bassLine) bassRootsOk += (n.pitch % 12 == 9 || n.pitch % 12 == 5 || n.pitch % 12 == 0 || n.pitch % 12 == 7);
        CHECK (! dna.bassLine.empty() && bassRootsOk >= (int) dna.bassLine.size() * 3 / 4, "bass line pitches should follow Am F C G");
        int melodyInKey = 0;
        for (auto& n : dna.melody) melodyInKey += inScale (n.pitch, 9, true);
        CHECK (! dna.melody.empty() && melodyInKey >= (int) dna.melody.size() * 3 / 4, "melody should be in A minor");
        CHECK (dna.chordRoots.size() >= 2, "chord loop missing");
        if (dna.chordRoots.size() == 4)
            CHECK (dna.chordRoots[0] == 9 && dna.chordRoots[1] == 5, "chord loop should start Am F");
        CHECK (dna.style == Style::Melodic || dna.style == Style::Techno, "126 BPM minor four-on-the-floor should read as (melodic) techno");
    }
    if (std::getenv ("FINISH_DEBUG_DNA") != nullptr)
    {
        Song s; s.bass = std::getenv ("FINISH_DEBUG_KB") != nullptr; s.chords = false; s.lead = false;
        const auto audio = PreviewSynth::renderOffline (PreviewSynth::compile (makeReference (s)), s.bpm, sr, 0.0, 8 * 4.0);
        std::cout << "  drums only:\n"; printDNA (MusicAnalyzer::analyze (capture (audio, sr, s.bpm, 0.0)));
    }
    {
        Song s; s.drums = false; s.bass = false; s.chords = false; s.bpm = 124.0;
        const auto ref = makeReference (s);
        const auto audio = PreviewSynth::renderOffline (PreviewSynth::compile (ref), s.bpm, sr, 0.0, 8 * 4.0);
        const auto dna = MusicAnalyzer::analyze (capture (audio, sr, s.bpm, 0.0));
        std::cout << "  lone melody:\n"; printDNA (dna);
        CHECK (dna.valid, "analysis failed on a lone melody");
        CHECK (! dna.hasDrums, "no drums in a lone melody");
        CHECK (dna.hasMelody && dna.melody.size() >= 6, "lone melody not transcribed");
        CHECK ((dna.keyRoot == 9 && dna.minor) || (dna.keyRoot == 0 && ! dna.minor), "lone melody key should be A minor / C major, got " + dna.keyName());
        int ok = 0;
        for (auto& n : dna.melody) ok += (n.pitch % 12 == 4 || n.pitch % 12 == 2 || n.pitch % 12 == 0 || n.pitch % 12 == 9 || n.pitch % 12 == 5);
        CHECK (ok >= (int) dna.melody.size() * 3 / 4, "transcribed pitches should match the motif");
    }
}

static void testArrangement()
{
    std::cout << "COMPOSE: arrangement generation (3 flavours, coherence, locks, regenerate)\n";
    const double sr = 48000.0;
    Song song;
    const auto audio = PreviewSynth::renderOffline (PreviewSynth::compile (makeReference (song)), song.bpm, sr, 0.0, song.bars * 4.0);
    const auto dna = MusicAnalyzer::analyze (capture (audio, sr, song.bpm, 0.0));

    for (int f = 0; f < numFlavours; ++f)
    {
        const auto arr = ArrangementGenerator::generate (dna, (Flavour) f, 0.4f, 1234);
        std::cout << "  " << flavourName ((Flavour) f) << ": " << arr.totalBars() << " bars |";
        int notes = 0, outOfKey = 0, drumNotes = 0;
        bool hasIntro = false, hasBuild = false, hasDrop2 = false, hasOutro = false, hasBreak = false, refUntouched = true;
        for (auto& s : arr.sections)
        {
            std::cout << " " << s.name << "(" << s.bars << ")";
            hasIntro |= s.role == Role::Intro; hasBuild |= s.role == Role::Build; hasDrop2 |= s.role == Role::Drop2;
            hasOutro |= s.role == Role::Outro; hasBreak |= (s.role == Role::Break || s.role == Role::Bridge);
            for (int l = 1; l < numMidiLanes; ++l)
                for (auto& n : s.clips[(size_t) l].notes) { ++notes; outOfKey += ! inScale (n.pitch, dna.keyRoot, dna.minor); }
            drumNotes += (int) s.clips[0].notes.size();
            for (auto& clip : s.clips)
                for (auto& n : clip.notes) CHECK (n.beat >= 0 && n.beat + n.length <= s.lengthBeats() + 1.0e-6, "note outside its section");
            if (s.isReference) refUntouched &= s.role == Role::Drop;
            if (! s.isReference && s.role != Role::Drop)
            {
                int lanes = 0; for (int l = 0; l < numMidiLanes; ++l) lanes += ! s.clips[(size_t) l].notes.empty();
                CHECK (lanes >= 3, juce::String (s.name) + " should have at least 3 musical parts");
                CHECK (s.recipe.isNotEmpty(), "section without an explanation");
            }
        }
        std::cout << "\n    " << notes << " melodic notes (" << outOfKey << " out of key), " << drumNotes << " drum hits\n";
        CHECK (hasIntro && hasBuild && hasDrop2 && hasOutro && hasBreak, "missing parts in the arrangement");
        CHECK (outOfKey <= notes / 50, "generated notes should stay in the detected key");
        CHECK (refUntouched, "only the drop may be the user's reference");
        CHECK (arr.summary.isNotEmpty(), "arrangement without a summary");

        // Builds carry a riser, drops an impact.
        for (auto& s : arr.sections)
        {
            if (s.role == Role::Build) CHECK (! s.clips[(size_t) Lane::Fx].fx.empty() && s.clips[(size_t) Lane::Fx].fx.front().kind == FxKind::Riser, "build without riser");
            if (s.role == Role::Drop2) { bool impact = false; for (auto& x : s.clips[(size_t) Lane::Fx].fx) impact |= x.kind == FxKind::Impact; CHECK (impact, "drop 2 without impact"); }
        }
    }

    // Flavours really differ, the same seed is deterministic.
    const auto a = ArrangementGenerator::generate (dna, Flavour::Faithful, 0.4f, 99);
    const auto b = ArrangementGenerator::generate (dna, Flavour::Faithful, 0.4f, 99);
    const auto c = ArrangementGenerator::generate (dna, Flavour::Intense, 0.4f, 99);
    auto count = [] (const Arrangement& x) { size_t n = 0; for (auto& s : x.sections) for (auto& cl : s.clips) n += cl.notes.size(); return n; };
    CHECK (count (a) == count (b), "generation is not deterministic");
    CHECK (count (a) != count (c) || a.totalBars() != c.totalBars(), "FIDELE and INTENSE are identical");

    // The intro reuses the user's groove (kick on every beat) and the drop 2 replays the motif.
    for (auto& s : a.sections)
        if (s.role == Role::Drop2)
        {
            int motifHits = 0;
            for (auto& n : s.clips[(size_t) Lane::Lead].notes) for (auto& m : dna.melody) if (n.pitch % 12 == m.pitch % 12 && std::abs (std::fmod (n.beat, dna.melodyBars * 4.0) - m.beat) < 0.01) { ++motifHits; break; }
            CHECK (motifHits >= 8, "drop 2 should be built on the user's motif");
        }

    // Regenerate one section, others untouched.
    auto arr = a;
    const int idx = 3;
    const auto before = arr.sections;
    ArrangementGenerator::buildSection (arr, idx, dna, 4242);
    bool othersSame = true;
    for (size_t i = 0; i < arr.sections.size(); ++i)
        if ((int) i != idx)
            for (int l = 0; l < numMidiLanes; ++l)
                othersSame &= arr.sections[i].clips[(size_t) l].notes.size() == before[i].clips[(size_t) l].notes.size();
    CHECK (othersSame, "regenerating one section changed others");
    size_t diff = 0;
    for (int l = 0; l < numMidiLanes; ++l) diff += arr.sections[(size_t) idx].clips[(size_t) l].notes.size() != before[(size_t) idx].clips[(size_t) l].notes.size();
    bool notesChanged = diff > 0;
    if (! notesChanged)
        for (int l = 0; l < numMidiLanes && ! notesChanged; ++l)
            for (size_t k = 0; k < arr.sections[(size_t) idx].clips[(size_t) l].notes.size(); ++k)
                if (arr.sections[(size_t) idx].clips[(size_t) l].notes[k].pitch != before[(size_t) idx].clips[(size_t) l].notes[k].pitch
                    || std::abs (arr.sections[(size_t) idx].clips[(size_t) l].notes[k].velocity - before[(size_t) idx].clips[(size_t) l].notes[k].velocity) > 1.0e-4f) { notesChanged = true; break; }
    CHECK (notesChanged, "regenerate produced the same section");

    // Insert a dark melodic bridge.
    SectionMood mood; mood.darker = 1; mood.melodic = 1;
    const int pos = ArrangementGenerator::insertSection (arr, 4, Role::Bridge, 8, dna, mood, 77);
    CHECK (arr.sections[(size_t) pos].role == Role::Bridge && ! arr.sections[(size_t) pos].clips[(size_t) Lane::Lead].notes.empty(), "bridge not inserted");
    CHECK (arr.sections[(size_t) pos].recipe.contains ("sombre"), "dark bridge not reflected in its recipe");

    // The whole arrangement renders (audible, no NaN) through the preview synth.
    const auto seq = PreviewSynth::compile (a);
    const auto mix = PreviewSynth::renderOffline (seq, dna.bpm, 44100.0, 0.0, 64.0);
    bool finite = true;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < mix.getNumSamples(); ++i) finite &= std::isfinite (mix.getSample (ch, i));
    CHECK (finite && mix.getRMSLevel (0, 0, mix.getNumSamples()) > 0.01f, "arrangement preview is silent or broken");
    for (int k = 1; k <= 5; ++k)
    {
        const auto fx = PreviewSynth::renderFx ((FxKind) k, 16.0, 128.0, 44100.0, 0.8f);
        CHECK (fx.getMagnitude (0, fx.getNumSamples()) > 0.05f && fx.getMagnitude (0, fx.getNumSamples()) <= 1.0f, juce::String ("FX render broken: ") + fxName ((FxKind) k));
    }
}

static void testExport()
{
    std::cout << "COMPOSE: export (MIDI files, FX WAV, guide, Live request)\n";
    const double sr = 48000.0;
    Song song;
    const auto audio = PreviewSynth::renderOffline (PreviewSynth::compile (makeReference (song)), song.bpm, sr, 0.0, song.bars * 4.0);
    const auto dna = MusicAnalyzer::analyze (capture (audio, sr, song.bpm, 0.0));
    const auto arr = ArrangementGenerator::generate (dna, Flavour::Faithful, 0.4f, 7);
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("finish_export_test");
    dir.deleteRecursively();
    const auto r = ArrangementExport::exportAll (arr, dna, dir, 44100.0);
    CHECK (r.ok, "export failed: " + r.error);
    CHECK (r.fullMidi.existsAsFile() && r.guide.existsAsFile(), "missing full MIDI or guide");
    juce::MidiFile mf;
    { juce::FileInputStream in (r.fullMidi); CHECK (in.openedOk() && mf.readFrom (in), "full MIDI unreadable"); }
    CHECK (mf.getNumTracks() >= 5, "full MIDI should have a conductor + 4 or more parts");
    int noteOns = 0, refNotes = 0, expected = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* e : *mf.getTrack (t)) noteOns += e->message.isNoteOn();
    for (auto& s : arr.sections)
        for (int l = 0; l < numMidiLanes; ++l)
        {
            (s.isReference ? refNotes : expected) += (int) s.clips[(size_t) l].notes.size();
            if (s.isReference) expected += (int) s.layers[(size_t) l].notes.size();
        }
    std::cout << "  " << r.files.size() << " files, full MIDI: " << mf.getNumTracks() << " tracks, " << noteOns << " notes (expected " << expected
              << ", reference drop sketch " << refNotes << " not exported)\n";
    CHECK (noteOns == expected, "full MIDI note count mismatch");
    int wavs = 0, mids = 0;
    for (auto& f : r.files) { wavs += f.hasFileExtension ("wav"); mids += f.hasFileExtension ("mid"); }
    CHECK (wavs >= 3 && mids >= 20, "expected per-section clips and FX WAVs");
    juce::WavAudioFormat wav;
    for (auto& f : r.files)
        if (f.hasFileExtension ("wav"))
        {
            std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (new juce::FileInputStream (f), true));
            CHECK (rd != nullptr && rd->lengthInSamples > 1000, "unreadable WAV " + f.getFileName());
        }
    // Request for the Live Remote Script
    const auto json = ArrangementExport::buildRequestJson (arr, dna, r, 16, true, "test");
    const auto v = juce::JSON::parse (json);
    CHECK (v.isObject() && v["tracks"].isArray() && v["tracks"].getArray()->size() >= 5, "Live request malformed");
    dir.getChildFile ("request_test.json").replaceWithText (json);
    std::cout << "  guide:\n" << r.guide.loadFileAsString().upToFirstOccurrenceOf ("LE MIDI", false, false) << "\n";
}

static void testCommands()
{
    std::cout << "COMPOSE: free commands (FR/EN) and the composer\n";
    struct Case { const char* text; std::function<bool (const ComposeCommand&)> ok; };
    const Case cases[] = {
        { "finis mon morceau", [] (const ComposeCommand& c) { return c.finishTrack; } },
        { "Finish my track", [] (const ComposeCommand& c) { return c.finishTrack; } },
        { u8"Crée un bridge mélodique sombre", [] (const ComposeCommand& c) { return c.actions.size() == 1 && c.actions[0].role == Role::Bridge && c.actions[0].create && c.actions[0].mood.darker > 0 && c.actions[0].mood.melodic > 0; } },
        { "Ajoute un build-up avant le drop", [] (const ComposeCommand& c) { return c.actions.size() == 1 && c.actions[0].role == Role::Build && c.actions[0].beforeDrop == 1; } },
        { u8"Crée un outro progressif", [] (const ComposeCommand& c) { return c.actions.size() == 1 && c.actions[0].role == Role::Outro && c.actions[0].mood.progressive > 0; } },
        { "Rends le drop plus percutant", [] (const ComposeCommand& c) { return c.soundRequest && c.soundFocus == "DROP" && c.actions.size() == 1 && c.actions[0].role == Role::Drop && c.actions[0].mood.intense > 0; } },
        { u8"Rends le drop plus percutant et crée une transition plus intense vers le deuxième drop",
          [] (const ComposeCommand& c) { return c.soundRequest && c.actions.size() == 2 && c.actions[1].role == Role::Build && c.actions[1].beforeDrop == 2 && c.actions[1].mood.intense > 0; } },
        { "make a dark melodic bridge", [] (const ComposeCommand& c) { return c.actions.size() == 1 && c.actions[0].role == Role::Bridge && c.actions[0].mood.darker > 0; } },
        { "masterise pour le club", [] (const ComposeCommand& c) { return c.masterTarget == 3; } },
        { "master for spotify", [] (const ComposeCommand& c) { return c.masterTarget == 1; } },
        { "plus large mais pas plus fort", [] (const ComposeCommand& c) { return c.actions.empty() && ! c.finishTrack; } },
        { u8"une version plus atmosphérique", [] (const ComposeCommand& c) { return c.flavour == (int) Flavour::Atmospheric; } },
        { u8"intro courte et minimale", [] (const ComposeCommand& c) { return c.actions.size() == 1 && c.actions[0].role == Role::Intro && c.actions[0].bars == 8 && c.actions[0].mood.minimal > 0; } },
    };
    for (auto& c : cases)
    {
        const auto cmd = ComposeCommand::parse (juce::String (juce::CharPointer_UTF8 (c.text)));
        std::cout << "  \"" << c.text << "\" -> " << cmd.understood.joinIntoString (" | ") << (cmd.soundRequest ? "  [+son]" : "") << "\n";
        CHECK (c.ok (cmd), juce::String ("command misunderstood: ") + juce::String (juce::CharPointer_UTF8 (c.text)));
    }

    // Composer: apply the user's examples to a real arrangement.
    const double sr = 48000.0;
    Song song;
    const auto audio = PreviewSynth::renderOffline (PreviewSynth::compile (makeReference (song)), song.bpm, sr, 0.0, song.bars * 4.0);
    const auto dna = MusicAnalyzer::analyze (capture (audio, sr, song.bpm, 0.0));
    Composer comp;
    comp.setDNA (dna, 32, 11);
    CHECK (comp.hasArrangement(), "composer has no arrangement after setDNA");
    CHECK (comp.getProposal (Flavour::Atmospheric).sections.size() > 0 && comp.getProposal (Flavour::Intense).sections.size() > 0, "three proposals expected");
    const int before = (int) comp.getArrangement().sections.size();
    auto r = comp.apply (ComposeCommand::parse (juce::String (juce::CharPointer_UTF8 (u8"Crée un bridge mélodique sombre"))));
    for (auto& d : r.done) std::cout << "    " << d << "\n";
    CHECK ((int) comp.getArrangement().sections.size() == before + 1, "bridge not inserted");
    r = comp.apply (ComposeCommand::parse (juce::String (juce::CharPointer_UTF8 (u8"Rends le drop plus percutant et crée une transition plus intense vers le deuxième drop"))));
    for (auto& d : r.done) std::cout << "    " << d << "\n";
    CHECK (r.soundRequest, "punch request lost");
    bool accents = false, intenseBuild = false;
    const auto& arr = comp.getArrangement();
    for (size_t i = 0; i < arr.sections.size(); ++i)
    {
        if (arr.sections[i].isReference) accents |= ! arr.sections[i].layers[(size_t) Lane::Drums].notes.empty();
        if (arr.sections[i].role == Role::Build && i + 1 < arr.sections.size() && arr.sections[i + 1].role == Role::Drop2) intenseBuild |= arr.sections[i].mood.intense > 0;
    }
    CHECK (accents, "no accent layer on the user's drop");
    CHECK (intenseBuild, "build before drop 2 not intensified");
    CHECK (comp.undo() && comp.undo() && (int) comp.getArrangement().sections.size() == before, "undo does not restore the arrangement");

    // state round trip regenerates the same notes
    comp.toggleLock (1);
    const auto state = comp.toState();
    Composer back;
    back.fromState (state);
    auto countNotes = [] (const Arrangement& a) { size_t n = 0; for (auto& s : a.sections) for (auto& c : s.clips) n += c.notes.size(); return n; };
    CHECK (back.hasArrangement() && countNotes (back.getArrangement()) == countNotes (comp.getArrangement()), "composer state round trip differs");
    CHECK (back.getArrangement().sections[1].locked, "lock lost in state");
    // locked section survives "new arrangement"
    const auto lockedNotes = comp.getArrangement().sections[1].clips[(size_t) Lane::Lead].notes.size();
    comp.apply (ComposeCommand::parse ("propose moi un autre arrangement"));
    CHECK (comp.getArrangement().sections[1].clips[(size_t) Lane::Lead].notes.size() == lockedNotes, "locked section was regenerated");
}

juce::AudioBuffer<float> renderReferenceDropForTests (double sr, double& bpm)
{
    Song song;
    bpm = song.bpm;
    return PreviewSynth::renderOffline (PreviewSynth::compile (makeReference (song)), song.bpm, sr, 0.0, song.bars * 4.0);
}

void runComposeTests()
{
    testCommands();
    testMusicAnalysis();
    testArrangement();
    testExport();
}
