// Offline test runner for FINISH: DSP safety, analysis/diagnosis on synthetic mixes,
// intent parsing, state round-trip and (optionally) a PNG snapshot of the editor.
//
//   FinishTests                 run all tests
//   FinishTests --snapshot x.png  also render the UI after a full analysis

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Music/SectionDetector.h"
#include <random>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "  (" #cond ")\n"; } } while (0)

// --------------------------------------------------------------------------------------
// Synthetic music

struct MixOptions
{
    double sr = 48000.0;
    float levelDb = 0.0f;       // overall gain
    bool arrangement = true;    // intro / build / drop / break / drop / outro
    float seconds = 150.0f;
    float width = 1.0f;         // 0 = mono, 1 = normal stereo pad
    bool invertRight = false;   // phase problem
    bool squash = false;        // brickwall + clipping
    bool denseBuild = false;    // build already at drop energy (weak BUILD -> DROP contrast)
    bool denseBreak = false;    // break keeps the drums (doesn't breathe)
};

static juce::AudioBuffer<float> makeMix (const MixOptions& o)
{
    const int n = (int) (o.seconds * o.sr);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    const double beat = 60.0 / 124.0;
    const double twoPi = juce::MathConstants<double>::twoPi;
    float hpState = 0, lpBass = 0;
    double bassPhase = 0, kickPhase = 0;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / o.sr;
        // Arrangement energy per section
        float drums = 1, bass = 1, pad = 1;
        if (o.arrangement)
        {
            const double frac = t / o.seconds;
            if (frac < 0.12)      { drums = 0.0f; bass = 0.0f; pad = 0.6f; }            // intro
            else if (frac < 0.25) { drums = o.denseBuild ? 1.0f : (float) ((frac - 0.12) / 0.13) * 0.8f; bass = o.denseBuild ? 1.0f : 0.4f; pad = o.denseBuild ? 1.0f : 0.8f; } // build
            else if (frac < 0.48) { drums = 1; bass = 1; pad = 1; }                        // drop
            else if (frac < 0.62) { drums = 0.0f; bass = o.denseBreak ? 1.0f : 0.2f; pad = o.denseBreak ? 2.0f : 0.7f; } // break
            else if (frac < 0.88) { drums = 1; bass = 1; pad = 1; }                        // drop 2
            else                  { drums = 0.3f; bass = 0.0f; pad = 0.5f; }               // outro
        }
        const double inBeat = std::fmod (t, beat);
        const int beatIndex = (int) (t / beat);

        // Kick: pitch-dropping sine with sharp attack
        float kick = 0;
        {
            const double env = std::exp (-inBeat * 18.0);
            const double f = 50.0 + 120.0 * std::exp (-inBeat * 40.0);
            kickPhase += twoPi * f / o.sr;
            if (inBeat < 1.0 / o.sr) kickPhase = 0;
            kick = (float) (std::sin (kickPhase) * env) * 0.9f;
        }
        // Snare on 2 and 4
        float snare = 0;
        if (beatIndex % 2 == 1)
            snare = noise (rng) * (float) std::exp (-inBeat * 22.0) * 0.45f;
        // Hats on 8ths (high-passed noise)
        const double in8 = std::fmod (t, beat * 0.5);
        const float hn = noise (rng);
        hpState = 0.6f * hpState + 0.4f * hn;
        const float hat = (hn - hpState) * (float) std::exp (-in8 * 45.0) * 0.5f;
        // Bass: filtered saw at 55 Hz, sidechained by the kick
        bassPhase += 55.0 / o.sr;
        const float saw = (float) (2.0 * (bassPhase - std::floor (bassPhase)) - 1.0);
        lpBass = 0.97f * lpBass + 0.03f * saw;
        const float duck = 1.0f - 0.7f * drums * (float) std::exp (-inBeat * 8.0);
        const float bassS = lpBass * 0.6f * duck;
        // Pad: detuned chord, decorrelated left/right
        float padL = 0, padR = 0;
        for (double f : { 220.0, 277.18, 329.63, 440.0 })
        {
            padL += (float) std::sin (twoPi * f * 1.003 * t) * 0.09f;
            padR += (float) std::sin (twoPi * f * 0.997 * t + 1.3) * 0.09f;
        }

        const float centre = drums * (kick + snare) + bass * bassS;
        const float hatL = drums * hat * 0.8f, hatR = drums * hat * 1.0f;
        const float pm = 0.5f * (padL + padR), ps = 0.5f * (padL - padR) * o.width;
        float l = centre + hatL * (0.5f + 0.5f * o.width) + hatR * (0.5f - 0.5f * o.width) + pad * (pm + ps);
        float r = centre + hatR * (0.5f + 0.5f * o.width) + hatL * (0.5f - 0.5f * o.width) + pad * (pm - ps);
        b.setSample (0, i, l);
        b.setSample (1, i, o.invertRight ? -r : r);
    }

    if (o.squash)
    {
        b.applyGain (6.0f);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, std::tanh (b.getSample (ch, i) * 2.5f) / std::tanh (2.5f));
    }

    const float peak = b.getMagnitude (0, n);
    b.applyGain (juce::Decibels::decibelsToGain (o.levelDb) * (o.squash ? 0.999f : 0.5f) / std::max (peak, 1.0e-6f));
    return b;
}

static AudioFeatures measure (const juce::AudioBuffer<float>& b, double sr, int startSample = 0, MusicState* music = nullptr)
{
    AudioAnalyzer a;
    a.setPublishSamples (false);
    a.prepare (sr);
    AnalysisSession s;
    std::vector<AnalysisFrame> tmp (512);
    const int n = b.getNumSamples() - startSample;
    for (int pos = 0; pos < n; pos += 4096)
    {
        const int len = std::min (4096, n - pos);
        a.process (b.getReadPointer (0, startSample + pos), b.getNumChannels() > 1 ? b.getReadPointer (1, startSample + pos) : nullptr, len);
        for (int got; (got = a.readFrames (tmp.data(), 512)) > 0;)
            for (int i = 0; i < got; ++i) s.add (tmp[(size_t) i]);
    }
    if (music != nullptr) *music = SectionDetector().analyze (s.getFrames());
    return s.computeFeatures();
}

static void printFeatures (const char* name, const AudioFeatures& f)
{
    std::cout << "  [" << name << "] LUFS " << f.integratedLufs << "  peak " << f.peakDb << "  PLR " << f.plr
              << "  LRA " << f.loudnessRange << "  low " << f.lowRatio << "  high " << f.highRatio
              << "  side " << f.sideRatio << "  lowSide " << f.lowSideRatio << "  corr " << f.correlation
              << "  punch " << f.punchIndex << "\n";
}

static void runDSP (FinishDSP& dsp, juce::AudioBuffer<float>& b, const FinishDSP::Settings& s, int block = 512)
{
    for (int pos = 0; pos < b.getNumSamples(); pos += block)
    {
        const int len = std::min (block, b.getNumSamples() - pos);
        juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), b.getNumChannels(), pos, len);
        dsp.process (view, s);
    }
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}

// --------------------------------------------------------------------------------------

static void testTransparency()
{
    std::cout << "DSP: neutral settings are transparent\n";
    MixOptions o; o.seconds = 10; o.levelDb = -3.0f;   // peak -9 dBFS
    auto in = makeMix (o);
    auto out = in;
    FinishDSP dsp; dsp.prepare (o.sr, 512, 2);
    runDSP (dsp, out, {});
    float maxDiff = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::abs (in.getSample (ch, i) - out.getSample (ch, i)));
    std::cout << "  max difference " << maxDiff << "\n";
    CHECK (maxDiff < 1.0e-4f, "neutral chain changes the audio");
}

static void testStrategies()
{
    std::cout << "DSP: strategies are safe, deterministic and level-matched\n";
    MixOptions o; o.seconds = 40; o.arrangement = false; o.levelDb = -14.0f;
    const auto in = makeMix (o);
    const auto fin = measure (in, o.sr, (int) (o.sr * 10));
    printFeatures ("input", fin);

    const std::pair<const char*, MacroSettings> presets[] = {
        { "PUNCH",   { 1.0f, 0.25f, 0.8f, 0.5f, 0.5f, 0.2f } },
        { "WIDE",    { 1.0f, 0.1f, 0.15f, 0.8f, 0.5f, 0.1f } },
        { "MASSIVE", { 1.0f, 0.7f, 0.2f, 0.5f, 0.6f, 0.9f } },
        { "EXTREME", { 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f } },
    };
    for (auto& [name, m] : presets)
    {
        for (bool match : { true, false })
        {
            auto out = in;
            FinishDSP dsp; dsp.prepare (o.sr, 512, 2);
            FinishDSP::Settings s; s.macros = m; s.levelMatch = match;
            runDSP (dsp, out, s);
            const auto fo = measure (out, o.sr, (int) (o.sr * 10));
            const float peakDb = juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples()));
            std::cout << "  " << name << (match ? " (match on) " : " (match off)") << "  LUFS " << fo.integratedLufs
                      << "  peak " << peakDb << "  PLR " << fo.plr << "  side " << fo.sideRatio << "  lowSide " << fo.lowSideRatio
                      << "  punch " << fo.punchIndex << "\n";
            CHECK (allFinite (out), juce::String (name) + " produced NaN/Inf");
            CHECK (peakDb <= FinishDSP::ceilingDb + 0.01f, juce::String (name) + " exceeds the ceiling");
            if (match)
                CHECK (std::abs (fo.integratedLufs - fin.integratedLufs) < 1.0f, juce::String (name) + " level match off by more than 1 LU");
            if (! match && juce::String (name) == "MASSIVE")
                CHECK (fo.integratedLufs > fin.integratedLufs + 3.0f, "MASSIVE should be clearly louder without match");
            if (juce::String (name) == "WIDE")
            {
                CHECK (fo.sideRatio > fin.sideRatio * 1.3f, "WIDE should widen");
                CHECK (fo.lowSideRatio <= fin.lowSideRatio + 0.01f, "WIDE must not widen the bass");
            }
            if (juce::String (name) == "PUNCH")
                CHECK (fo.punchIndex > fin.punchIndex + 0.5f, "PUNCH should strengthen transients");
        }
    }

    // Determinism + block-size independence
    FinishDSP::Settings s; s.macros = presets[2].second;
    auto a = in, b2 = in;
    { FinishDSP d; d.prepare (o.sr, 4096, 2); runDSP (d, a, s, 4096); }
    { FinishDSP d; d.prepare (o.sr, 4096, 2); runDSP (d, b2, s, 4096); }
    bool same = true;
    for (int ch = 0; ch < 2 && same; ++ch)
        same = std::memcmp (a.getReadPointer (ch), b2.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) == 0;
    CHECK (same, "offline render is not deterministic");
}

static void testSampleRates()
{
    std::cout << "DSP + analysis at 44.1 / 48 / 88.2 / 96 / 192 kHz\n";
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        MixOptions o; o.sr = sr; o.seconds = 30; o.arrangement = false; o.levelDb = -6.0f;
        const auto in = makeMix (o);
        const auto fin = measure (in, sr, (int) (sr * 8));
        auto out = in;
        FinishDSP dsp; dsp.prepare (sr, 1024, 2);
        FinishDSP::Settings s; s.macros = { 1.0f, 0.7f, 0.6f, 0.75f, 0.4f, 0.9f };
        runDSP (dsp, out, s, 1024);
        const auto fo = measure (out, sr, (int) (sr * 8));
        const float peakDb = juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples()));
        std::cout << "  " << sr << " Hz: in " << fin.integratedLufs << " LUFS (punch " << fin.punchIndex << ", low " << fin.lowRatio
                  << ")  out " << fo.integratedLufs << " LUFS, peak " << peakDb << "\n";
        CHECK (allFinite (out), "NaN at " + juce::String (sr));
        CHECK (peakDb <= FinishDSP::ceilingDb + 0.01f, "ceiling exceeded at " + juce::String (sr));
        CHECK (std::abs (fo.integratedLufs - fin.integratedLufs) < 1.0f, "level match off at " + juce::String (sr));
    }
}

static void testDiagnosis()
{
    std::cout << "Analysis & diagnosis on synthetic mixes\n";
    DiagnosisEngine de;
    DecisionEngine dec;
    ReferenceProfile noRef;

    auto run = [&] (const char* name, const MixOptions& o, std::vector<Diagnosis>& d, DecisionResult& r, MusicState& m)
    {
        const auto mix = makeMix (o);
        const auto f = measure (mix, o.sr, 0, &m);
        printFeatures (name, f);
        d = de.analyze (f, m, noRef);
        r = dec.build (d, {}, true);
        std::cout << "    score " << DiagnosisEngine::finishScore (d) << "  sections:";
        for (auto& s : m.sections) std::cout << " " << s.label << "(" << (int) s.startTime << "-" << (int) s.endTime << ")";
        std::cout << "\n    diagnoses:";
        for (auto& x : d) std::cout << " [" << x.title << " " << x.severity << "]";
        std::cout << "\n    strategies:";
        for (auto& p : r.proposals) std::cout << " " << p.title << (p.refused ? "(refused)" : "") << " " << p.confidence;
        std::cout << "\n";
        return f;
    };

    std::vector<Diagnosis> d; DecisionResult r; MusicState m;

    MixOptions clean; clean.levelDb = -10.0f;   // quiet, punchy, decent width
    run ("quiet clean mix", clean, d, r, m);
    CHECK (DiagnosisEngine::has (d, DiagnosisType::TooQuiet), "quiet mix not flagged");
    CHECK (! DiagnosisEngine::has (d, DiagnosisType::OverCompressed), "clean mix flagged as squashed");
    CHECK (! DiagnosisEngine::has (d, DiagnosisType::WeakTransients), "clean punchy mix flagged as soft");
    bool hasDrop = false; for (auto& s : m.sections) hasDrop |= s.type == SectionType::Drop;
    CHECK (hasDrop && m.sections.size() >= 4, "arrangement sections not detected");
    CHECK (! r.proposals.empty() && r.proposals[0].title == "MASSIVE", "quiet mix should recommend MASSIVE");

    MixOptions squashed; squashed.squash = true;
    run ("squashed mix", squashed, d, r, m);
    CHECK (DiagnosisEngine::has (d, DiagnosisType::OverCompressed), "squashed mix not flagged");
    CHECK (DiagnosisEngine::has (d, DiagnosisType::WeakTransients), "squashed mix transients not flagged");
    bool massiveRefused = false; for (auto& p : r.proposals) if (p.title == "MASSIVE") massiveRefused = p.refused;
    CHECK (massiveRefused, "MASSIVE must be refused on a squashed mix");
    CHECK (r.proposals[0].title == "PUNCH", "squashed mix should recommend PUNCH");

    MixOptions mono; mono.width = 0.0f; mono.levelDb = -4.0f;
    run ("mono mix", mono, d, r, m);
    CHECK (DiagnosisEngine::has (d, DiagnosisType::Narrow), "mono mix not flagged narrow");

    MixOptions phase; phase.invertRight = true; phase.levelDb = -4.0f;
    run ("phase-inverted mix", phase, d, r, m);
    CHECK (DiagnosisEngine::has (d, DiagnosisType::StereoRisk), "phase problem not flagged");
    bool wideRefused = false; for (auto& p : r.proposals) if (p.title == "WIDE") wideRefused = p.refused;
    CHECK (wideRefused, "WIDE must be refused when mono compatibility is at risk");

    MixOptions loop; loop.arrangement = false; loop.seconds = 90; loop.levelDb = -4.0f;
    run ("flat loop", loop, d, r, m);
    CHECK (DiagnosisEngine::has (d, DiagnosisType::FlatArrangement), "flat loop not flagged");

    // Reference comparison: a quiet mix against a louder version of itself.
    ReferenceProfile ref; ref.valid = true; ref.name = "Loud master";
    MixOptions loud; loud.levelDb = 0.0f;
    ref.features = measure (makeMix (loud), loud.sr);
    MixOptions quieter; quieter.levelDb = -8.0f;
    const auto fq = measure (makeMix (quieter), quieter.sr, 0, &m);
    d = de.analyze (fq, m, ref);
    std::cout << "  vs reference:"; for (auto& x : d) std::cout << " [" << x.title << "]"; std::cout << "\n";
    CHECK (DiagnosisEngine::has (d, DiagnosisType::TooQuiet), "reference loudness gap not flagged");
}

static void testIntent()
{
    std::cout << "Intent parser (EN/FR)\n";
    struct Case { const char* text; std::function<bool (const IntentVector&)> ok; };
    const Case cases[] = {
        { "Make my drop hit harder",              [] (const IntentVector& v) { return v.punch > 0.5f && v.impact > 0.3f; } },
        { "plus large mais pas plus fort",        [] (const IntentVector& v) { return v.width > 0.5f && v.loudness < -0.5f; } },
        { "Je veux que ça tape plus fort",        [] (const IntentVector& v) { return v.punch > 0.5f; } },
        { "trop de basses",                       [] (const IntentVector& v) { return v.bass < -0.5f; } },
        { "pas assez de basses",                  [] (const IntentVector& v) { return v.bass > 0.5f; } },
        { "too much bass, needs more air",        [] (const IntentVector& v) { return v.bass < -0.5f && v.brightness > 0.5f; } },
        { "it sounds dull and squashed",          [] (const IntentVector& v) { return v.brightness > 0.5f && v.dynamics > 0.5f; } },
        { "wider without getting louder",         [] (const IntentVector& v) { return v.width > 0.5f && v.loudness < 0.0f; } },
        { "plus chaud, moins agressif",           [] (const IntentVector& v) { return v.warmth > 0.5f; } },
        { "make it massive",                      [] (const IntentVector& v) { return v.energy > 0.5f; } },
        { "le mix est trop étroit, je veux de l'espace", [] (const IntentVector& v) { return v.width > 0.0f; } },
    };
    for (auto& c : cases)
    {
        const auto r = IntentParser::parse (juce::CharPointer_UTF8 (c.text));
        std::cout << "  \"" << c.text << "\" -> " << r.understood.joinIntoString (" ") << "\n";
        CHECK (c.ok (r.intent), juce::String ("intent misread: ") + juce::String (juce::CharPointer_UTF8 (c.text)));
    }
}

struct TestPlayHead : juce::AudioPlayHead
{
    double time = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSeconds (time);
        info.setIsPlaying (playing);
        return info;
    }
};

// Feeds audio as if the DAW were playing it from song position startTime.
static void feedTimed (FINISHAudioProcessor& p, TestPlayHead& ph, const juce::AudioBuffer<float>& src, double sr,
                       double startTime, int block, bool pump, juce::AudioBuffer<float>* out = nullptr)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, block);
    if (out != nullptr) out->setSize (2, src.getNumSamples());
    for (int pos = 0; pos < src.getNumSamples(); pos += block)
    {
        const int len = std::min (block, src.getNumSamples() - pos);
        buf.setSize (2, len, false, false, true);
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, src, ch, pos, len);
        ph.time = startTime + pos / sr;
        p.processBlock (buf, midi);
        if (out != nullptr) for (int ch = 0; ch < 2; ++ch) out->copyFrom (ch, pos, buf, ch, 0, len);
        if (pump && (pos / block) % 400 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    if (pump) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
}

static void feed (FINISHAudioProcessor& p, const juce::AudioBuffer<float>& src, int block, bool pump)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, block);
    for (int pos = 0; pos < src.getNumSamples(); pos += block)
    {
        const int len = std::min (block, src.getNumSamples() - pos);
        buf.setSize (2, len, false, false, true);
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, src, ch, pos, len);
        p.processBlock (buf, midi);
        if (pump && (pos / block) % 400 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    if (pump) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
}

static void testProcessorAndState()
{
    std::cout << "Processor: analysis workflow, odd block sizes, mono, state round-trip\n";
    MixOptions o; o.levelDb = -10.0f; o.seconds = 150;
    const auto mix = makeMix (o);

    FINISHAudioProcessor p;
    p.prepareToPlay (o.sr, 512);
    p.toggleAnalysis();
    feed (p, mix, 512, true);
    p.toggleAnalysis();
    CHECK (p.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Ready, "analysis did not complete");
    CHECK (std::abs (p.getFeatures().durationSec - 150.0f) < 2.0f, "analysis lost audio frames");
    std::cout << "  analysed " << p.getFeatures().durationSec << " s, score " << p.getScore() << "\n";

    p.setIntent ("make the drop hit harder but not louder");
    std::cout << "  intent notes: " << p.getDecision().notes.joinIntoString (" | ") << "\n";
    p.applyProposal (0);
    const auto applied = p.getCurrentMacros();
    CHECK (p.getAppliedStrategy().isNotEmpty(), "proposal not applied");

    // Odd block sizes through the full processor
    {
        juce::AudioBuffer<float> chunk (2, 48000);
        for (int ch = 0; ch < 2; ++ch) chunk.copyFrom (ch, 0, mix, ch, 48000 * 40, 48000);
        juce::MidiBuffer midi;
        int pos = 0;
        for (int bs : { 1, 7, 64, 333, 1024, 4096, 13, 2048 })
        {
            if (pos + bs > chunk.getNumSamples()) break;
            juce::AudioBuffer<float> view (chunk.getArrayOfWritePointers(), 2, pos, bs);
            p.processBlock (view, midi);
            pos += bs;
        }
        CHECK (allFinite (chunk), "odd block sizes produced NaN");
    }

    // Mono layout
    {
        FINISHAudioProcessor m;
        auto layout = m.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        CHECK (m.setBusesLayout (layout), "mono layout rejected");
        m.prepareToPlay (44100, 256);
        juce::AudioBuffer<float> mono (1, 256);
        juce::MidiBuffer midi;
        for (int k = 0; k < 200; ++k)
        {
            for (int i = 0; i < 256; ++i) mono.setSample (0, i, 0.5f * (float) std::sin (0.05 * (k * 256 + i)));
            m.processBlock (mono, midi);
        }
        CHECK (allFinite (mono), "mono processing produced NaN");
    }

    // State round-trip
    juce::MemoryBlock state;
    p.getStateInformation (state);
    FINISHAudioProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    const auto restored = q.getCurrentMacros();
    CHECK (std::abs (restored.punch - applied.punch) < 1e-4f && std::abs (restored.width - applied.width) < 1e-4f
           && std::abs (restored.loudness - applied.loudness) < 1e-4f, "parameters not restored");
    CHECK (q.getFeatures().valid && q.getMusic().sections.size() == p.getMusic().sections.size(), "analysis not restored");
    CHECK (q.getIntentText() == p.getIntentText(), "intent not restored");
    CHECK (q.getScore() == p.getScore(), "score differs after restore");
}


static juce::AudioBuffer<float> slice (const juce::AudioBuffer<float>& b, double sr, double from, double to)
{
    const int a = (int) (from * sr), n = (int) ((to - from) * sr);
    juce::AudioBuffer<float> out (2, n);
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, b, ch, a, n);
    return out;
}

static float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float d = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i)
            d = std::max (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

static void printTrack (const TrackEngine& t)
{
    const auto& m = t.getMusic();
    std::cout << "    map " << m.startTime << " .. " << m.endTime() << " s\n";
    for (size_t i = 0; i < m.sections.size(); ++i)
    {
        const auto& s = m.sections[i];
        const auto& v = t.getVerdicts()[i];
        std::cout << "    [" << s.label << " " << (int) s.startTime << "-" << (int) s.endTime << " conf " << s.confidence
                  << (s.isUncertain() ? " ?" + sectionTypeName (s.alternative) : juce::String()) << "] "
                  << v.headline << " (potential " << v.potential << ")";
        if (v.transitionRef >= 0) std::cout << " -> transition " << v.transitionRef;
        std::cout << "\n       variants:";
        for (auto& x : *t.variantsFor (TrackEngine::Selection::section ((int) i)))
            std::cout << " " << x.letter << ":" << x.name << (x.recommended ? "*" : "") << (x.refused ? "(declined)" : "") << " " << x.score;
        std::cout << "\n";
    }
    for (auto& tr : t.getTransitions())
    {
        std::cout << "    <" << tr.title << "> " << (tr.works ? "WORKS " : "IMPROVE ") << tr.headline << " (" << tr.contrastDb << " dB)\n       variants:";
        for (auto& x : tr.variants) std::cout << " " << x.letter << ":" << x.name << (x.recommended ? "*" : "") << " " << x.score;
        std::cout << "\n";
    }
}

static void analyseTimed (FINISHAudioProcessor& p, TestPlayHead& ph, const juce::AudioBuffer<float>& mix, double sr, double songStart)
{
    p.prepareToPlay (sr, 512);
    p.setPlayHead (&ph);
    p.toggleAnalysis();
    feedTimed (p, ph, mix, sr, songStart, 512, true);
    ph.playing = false;
    // Transport stopped: FINISH finishes the analysis on its own.
    feedTimed (p, ph, slice (mix, sr, 0, 0.5), sr, songStart, 512, true);
    ph.playing = true;
}

static void testTrackEngine()
{
    std::cout << "FINISH MY TRACK: track map, Section DNA, transitions, variants\n";
    using Sel = TrackEngine::Selection;

    // 1. Normal arrangement, analysed from song position 30 s (the DAW timeline, not "listening time").
    {
        MixOptions o; o.levelDb = -6.0f;
        const auto mix = makeMix (o);
        FINISHAudioProcessor p; TestPlayHead ph;
        analyseTimed (p, ph, mix, o.sr, 30.0);
        CHECK (p.getAnalysisState() == FINISHAudioProcessor::AnalysisState::Ready, "analysis did not auto-finish when the transport stopped");
        const auto& t = p.getTrack();
        printTrack (t);
        const auto& S = t.getMusic().sections;
        CHECK (std::abs (t.getMusic().startTime - 30.0) < 0.2, "track map is not aligned to the song position");
        CHECK (S.size() == 6, "expected 6 sections");
        CHECK (t.getTransitions().size() == S.size() - 1, "one transition per boundary");
        if (S.size() == 6)
        {
            CHECK (S[2].type == SectionType::Drop && S[4].type == SectionType::Drop, "drops not found");
            CHECK (std::abs (S[2].startTime - (30.0 + 0.25 * 150)) < 2.5, "DROP 1 start misplaced");
            CHECK (t.getDNA()[4].role == SectionRole::NewPeak, "second drop should be a New peak");
            // Identical drops: FINISH should say DROP 2 is already powerful and point at the transition before it.
            CHECK (t.getVerdicts()[4].transitionRef == 3, "DROP 2 should recommend working the BREAK -> DROP 2 transition");
            CHECK (t.getTransitions()[3].works, "BREAK -> DROP 2 has a clear lift and should be judged as working");
        }
        for (size_t i = 0; i < S.size(); ++i)
        {
            const auto* vs = t.variantsFor (Sel::section ((int) i));
            CHECK (vs != nullptr && vs->size() == 3 && (*vs)[0].letter == 'A' && (*vs)[2].letter == 'C', "three variants A/B/C per section");
            CHECK (S[i].confidence > 0.0f && S[i].confidence <= 1.0f, "confidence out of range");
        }

        // 2. Apply DROP 1 · A Punch with Coherence 0: audio changes inside DROP 1 only.
        if (auto* c = p.params.getParameter (ParamIDs::coherence)) c->setValueNotifyingHost (0.0f);
        FINISHAudioProcessor ref; TestPlayHead refPh;
        if (auto* c = ref.params.getParameter (ParamIDs::coherence)) c->setValueNotifyingHost (0.0f);
        ref.prepareToPlay (o.sr, 512); ref.setPlayHead (&refPh);
        p.prepareToPlay (o.sr, 512);
        p.editTrack ([] (TrackEngine& e) { e.apply (Sel::section (2), 0, 1.0f); });
        CHECK (p.getTrack().getEdits().size() == 1, "edit not stored");
        const auto plan = p.getTrack().compilePlan();
        CHECK (plan.count == 1 && plan.evaluate ((S[2].startTime + S[2].endTime) / 2, 0.0f).delta.punch > 0.4f, "plan has no punch inside DROP 1");
        CHECK (std::abs (plan.evaluate (S[0].startTime + 5.0, 0.0f).delta.punch) < 1e-6f, "plan leaks outside DROP 1");

        juce::AudioBuffer<float> outP, outR;
        const auto part = slice (mix, o.sr, 0.0, 80.0);   // intro, build, most of drop 1
        feedTimed (p, ph, part, o.sr, 30.0, 512, false, &outP);
        feedTimed (ref, refPh, part, o.sr, 30.0, 512, false, &outR);
        const float dIntro = maxDiff (slice (outP, o.sr, 2.0, 15.0), slice (outR, o.sr, 2.0, 15.0));
        const float dDrop = maxDiff (slice (outP, o.sr, 45.0, 70.0), slice (outR, o.sr, 45.0, 70.0));
        std::cout << "    section edit: diff in INTRO " << dIntro << ", diff in DROP 1 " << dDrop << "\n";
        CHECK (dIntro < 1.0e-6f, "section edit changed audio outside its section");
        CHECK (dDrop > 0.01f, "section edit had no effect inside its section");
        CHECK (allFinite (outP), "NaN with a section plan");

        // 3. Preview, Keep Original, Undo, Regenerate.
        p.editTrack ([] (TrackEngine& e) { e.preview (Sel::section (0), 2); });
        CHECK (p.getTrack().effectiveEdits().size() == 2, "preview not layered on top of the applied edits");
        p.editTrack ([] (TrackEngine& e) { e.clearPreview(); });
        CHECK (p.getTrack().effectiveEdits().size() == 1, "preview not cleared");

        if (auto* pp = p.params.getParameter (ParamIDs::punch)) pp->setValueNotifyingHost (0.8f);   // global processing on
        p.editTrack ([] (TrackEngine& e) { e.keepOriginal (Sel::section (0)); });
        juce::AudioBuffer<float> outKeep;
        feedTimed (p, ph, slice (mix, o.sr, 0.0, 16.0), o.sr, 30.0, 512, false, &outKeep);
        const float dKeep = maxDiff (slice (outKeep, o.sr, 3.0, 14.0), slice (mix, o.sr, 3.0, 14.0));
        std::cout << "    keep original: max diff vs untouched audio " << dKeep << "\n";
        CHECK (dKeep < 1.0e-5f, "Keep Original did not leave the section untouched");
        if (auto* pp = p.params.getParameter (ParamIDs::punch)) pp->setValueNotifyingHost (0.0f);

        const auto namesBefore = (*p.getTrack().variantsFor (Sel::section (3)))[0].name;
        p.editTrack ([] (TrackEngine& e) { e.regenerate (Sel::section (3)); });
        CHECK ((*p.getTrack().variantsFor (Sel::section (3)))[0].name != namesBefore, "Regenerate gave the same directions");

        p.editTrack ([] (TrackEngine& e) { e.undo(); });
        CHECK (p.getTrack().getEdits().size() == 1 && ! p.getTrack().getEdits()[0].keepOriginal, "Undo did not restore the previous edits");

        // 4. Structure corrections.
        p.editTrack ([] (TrackEngine& e) { e.setSectionType (3, SectionType::Bridge); });
        CHECK (p.getTrack().getMusic().sections[3].label == "BRIDGE" && p.getTrack().getMusic().sections[3].confirmed, "relabel failed");
        const double oldEnd = p.getTrack().getMusic().sections[1].endTime;
        p.editTrack ([oldEnd] (TrackEngine& e) { e.moveBoundary (1, oldEnd - 2.0); });
        CHECK (std::abs (p.getTrack().getMusic().sections[2].startTime - (oldEnd - 2.0)) < 0.01, "boundary move failed");
        p.editTrack ([] (TrackEngine& e) { e.undo(); });
        p.editTrack ([] (TrackEngine& e) { e.undo(); });
        CHECK (p.getTrack().getMusic().sections[3].type == SectionType::Break
               && std::abs (p.getTrack().getMusic().sections[1].endTime - oldEnd) < 0.01, "undo of structure edits failed");

        // 5. Coherence: big width jump flagged at 0 %, smoothed at 100 %.
        p.editTrack ([] (TrackEngine& e) { e.apply (Sel::section (2), 1, 1.0f); e.apply (Sel::section (3), 2, 1.0f); });
        const auto w0 = p.getTrack().coherenceWarnings (0.0f), w1 = p.getTrack().coherenceWarnings (1.0f);
        std::cout << "    coherence warnings at 0%: " << w0.size() << " (" << w0.joinIntoString (" | ") << "), at 100%: " << w1.size() << "\n";
        CHECK (w0.size() > w1.size(), "Coherence does not reduce disparities");

        // 6. Transition edit lands on the boundary.
        p.editTrack ([] (TrackEngine& e) { e.apply (Sel::transition (1), 1, 1.0f); });
        const auto* te = p.getTrack().editFor (Sel::transition (1));
        CHECK (te != nullptr, "transition edit missing");
        if (te != nullptr)
        {
            const auto [a, b] = p.getTrack().rangeOf (*te);
            CHECK (std::abs (b - S[2].startTime) < 0.01 && b - a < 1.0, "transition edit is not right before the drop");
        }

        // 7. Everything survives save / reload.
        juce::MemoryBlock state;
        p.getStateInformation (state);
        FINISHAudioProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (q.getTrack().getMusic().sections.size() == p.getTrack().getMusic().sections.size(), "sections not restored");
        CHECK (q.getTrack().getEdits().size() == p.getTrack().getEdits().size(), "edits not restored");
        const auto pa = p.getTrack().compilePlan(), qa = q.getTrack().compilePlan();
        bool samePlan = pa.count == qa.count;
        for (int i = 0; samePlan && i < pa.count; ++i)
            samePlan = std::abs (pa.edits[(size_t) i].start - qa.edits[(size_t) i].start) < 1e-3
                    && std::abs (pa.edits[(size_t) i].to.width - qa.edits[(size_t) i].to.width) < 1e-3f;
        CHECK (samePlan, "section plan differs after reload");
    }

    // 8. Build already at drop energy: FINISH blames the transition, not the drop.
    {
        MixOptions o; o.levelDb = -6.0f; o.denseBuild = true;
        const auto mix = makeMix (o);
        FINISHAudioProcessor p; TestPlayHead ph;
        analyseTimed (p, ph, mix, o.sr, 0.0);
        std::cout << "  dense build:\n";
        printTrack (p.getTrack());
        bool flagged = false;
        for (auto& tr : p.getTrack().getTransitions())
            if (tr.kind == Transition::Kind::IntoPeak && ! tr.works) flagged = true;
        const auto& S = p.getTrack().getMusic().sections;
        bool dropsFound = false; for (auto& s : S) dropsFound |= s.type == SectionType::Drop;
        CHECK (dropsFound, "drops not detected with a dense build");
        CHECK (flagged || S.size() < 6, "weak arrival into the drop not flagged");
    }

    // 9. Break that doesn't breathe.
    {
        MixOptions o; o.levelDb = -6.0f; o.denseBreak = true;
        const auto mix = makeMix (o);
        FINISHAudioProcessor p; TestPlayHead ph;
        analyseTimed (p, ph, mix, o.sr, 0.0);
        std::cout << "  dense break:\n";
        printTrack (p.getTrack());
        bool breathFlagged = false;
        for (size_t i = 0; i < p.getTrack().getMusic().sections.size(); ++i)
        {
            const auto type = p.getTrack().getMusic().sections[i].type;
            if ((isBreathType (type) || type == SectionType::Verse) && p.getTrack().getVerdicts()[i].potential >= 0.5f) breathFlagged = true;
        }
        for (auto& tr : p.getTrack().getTransitions())
            if (tr.kind == Transition::Kind::OutOfPeak && ! tr.works) breathFlagged = true;
        CHECK (breathFlagged, "a break that doesn't breathe was not flagged");
    }
}

static void writePng (juce::Component& c, const juce::File& file)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::cout << "  wrote " << file.getFileName() << "\n";
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void snapshot (const juce::File& base)
{
    std::cout << "Rendering editor snapshots\n";
    using Sel = TrackEngine::Selection;
    auto file = [&] (const char* suffix) { return base.getSiblingFile (base.getFileNameWithoutExtension() + "_" + suffix + ".png"); };

    MixOptions o; o.levelDb = -6.0f; o.seconds = 170; o.denseBreak = true;
    const auto mix = makeMix (o);

    FINISHAudioProcessor p;
    TestPlayHead ph;
    p.prepareToPlay (o.sr, 512);
    p.setPlayHead (&ph);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorIfNeeded());
    auto* e = dynamic_cast<FINISHAudioProcessorEditor*> (ed.get());
    if (e == nullptr) return;
    feedTimed (p, ph, slice (mix, o.sr, 0, 3), o.sr, 12.0, 512, true);
    pump (200);
    writePng (*e, file ("idle"));

    // Listening, half-way through the song.
    p.toggleAnalysis();
    feedTimed (p, ph, slice (mix, o.sr, 0, 96), o.sr, 12.0, 512, true);
    pump (200);
    writePng (*e, file ("listening"));
    feedTimed (p, ph, slice (mix, o.sr, 96, 170), o.sr, 12.0 + 96.0, 512, true);
    ph.playing = false;
    feedTimed (p, ph, slice (mix, o.sr, 0, 0.5), o.sr, 12.0 + 170.0, 512, true);
    ph.playing = true;
    pump (200);

    p.setIntent ("plus large mais pas plus fort");
    e->getTrackPanel().intentEditor.setText (p.getIntentText(), false);
    // Apply DROP 1 · A, keep the playhead inside DROP 2.
    p.editTrack ([] (TrackEngine& t) { t.apply (Sel::section (3), 0, 1.0f); });
    const auto& S = p.getTrack().getMusic().sections;
    int breakIndex = -1;
    for (size_t i = 0; i < S.size(); ++i) if (isBreathType (S[i].type)) breakIndex = (int) i;
    feedTimed (p, ph, slice (mix, o.sr, 122, 124), o.sr, 12.0 + 122.0, 512, true);
    pump (200);
    if (breakIndex >= 0)
    {
        e->select (Sel::section (breakIndex));
        p.editTrack ([breakIndex] (TrackEngine& t) { t.preview (Sel::section (breakIndex), 2, 1.0f); });
        e->getInspector().refresh();
    }
    pump (250);
    writePng (*e, file ("section"));

    p.editTrack ([] (TrackEngine& t) { t.clearPreview(); });
    e->select (Sel::transition (std::max (0, breakIndex - 1)));
    pump (250);
    writePng (*e, file ("transition"));

    e->select (Sel::track());
    pump (250);
    writePng (*e, file ("track"));

    ed.reset();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    testTransparency();
    testStrategies();
    testSampleRates();
    testDiagnosis();
    testIntent();
    testProcessorAndState();
    testTrackEngine();

    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--snapshot")
            snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1]));

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
