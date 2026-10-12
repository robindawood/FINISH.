// WHALE offline tests: DSP safety, loudness, AUTO against the reference profiles, presets, state, snapshots.
//
//   WhaleTests                       run all tests
//   WhaleTests --snap <dir> [wav]    render screenshots of the editor (optionally driven by a real track)
//   WhaleTests --render <in.wav> <out.wav> [style]   master a file offline with AUTO (for listening tests)
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "  ok  : " << msg << "\n"; } while (0)

struct TestAccess
{
    static void pump (WhaleAudioProcessor& p) { p.timerCallback(); }
    static void tickEditor (WhaleEditor& e) { e.forcedDt = 1.0f / 60.0f; e.timerCallback(); }
    static juce::Component& content (WhaleEditor& e) { return e.content; }
    static void openExpert (WhaleEditor& e, int m) { e.openExpert (m); }
    static void showBrowser (WhaleEditor& e) { e.browser.refresh(); e.browser.setVisible (true); }
    static void hideBrowser (WhaleEditor& e) { e.browser.setVisible (false); }
    static juce::Component& browser (WhaleEditor& e) { return e.browser; }
};

namespace
{
    constexpr double sr = 44100.0;
    constexpr int block = 512;

    float dbOf (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

    // A small club-like mix: 4/4 kick, off-beat bass, hats, a chord pad. Deterministic.
    struct MixGen
    {
        double t = 0, bpm = 126;
        juce::Random r { 3 };
        float pink[7] {};
        float gainDb = -12.0f;
        bool stereoBass = false, invertR = false;
        void fill (juce::AudioBuffer<float>& b)
        {
            const double beat = 60.0 / bpm;
            const float g = juce::Decibels::decibelsToGain (gainDb);
            for (int i = 0; i < b.getNumSamples(); ++i, t += 1.0 / sr)
            {
                const double pb = std::fmod (t, beat);
                const float kick = (float) (std::sin (2 * juce::MathConstants<double>::pi * (50.0 + 90.0 * std::exp (-pb * 30.0)) * pb) * std::exp (-pb * 7.0));
                const double off = std::fmod (t + beat * 0.5, beat);
                const float bass = (float) (0.5 * std::sin (2 * juce::MathConstants<double>::pi * 55.0 * t) * std::exp (-off * 5.0));
                float w = r.nextFloat() * 2 - 1;
                pink[0] = 0.99886f * pink[0] + w * 0.0555179f; pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
                pink[2] = 0.96900f * pink[2] + w * 0.1538520f; pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
                const float pn = (pink[0] + pink[1] + pink[2] + pink[3]) * 0.2f;
                const double hb = std::fmod (t, beat * 0.5);
                const float hat = (float) ((r.nextFloat() * 2 - 1) * std::exp (-hb * 60.0) * 0.25);
                const float pad = 0.08f * (float) (std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t) + std::sin (2 * juce::MathConstants<double>::pi * 277.2 * t + 1.0)
                                                    + std::sin (2 * juce::MathConstants<double>::pi * 329.6 * t + 2.0));
                const float L = g * (kick + bass + hat + pad + 0.15f * pn), R = g * (kick + (stereoBass ? -0.6f : 1.0f) * bass + 0.8f * hat + pad * 0.9f + 0.15f * pn);
                b.setSample (0, i, L);
                b.setSample (1, i, invertR ? -R : R);
            }
        }
    };

    float truePeakDb (const juce::AudioBuffer<float>& b)
    {
        juce::dsp::Oversampling<float> os (2, 3, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        os.initProcessing ((size_t) b.getNumSamples());
        juce::AudioBuffer<float> copy (b);
        juce::dsp::AudioBlock<float> blk (copy);
        auto up = os.processSamplesUp (blk);
        float pk = 0;
        for (size_t c = 0; c < up.getNumChannels(); ++c)
            for (size_t i = 0; i < up.getNumSamples(); ++i) pk = std::max (pk, std::abs (up.getSample ((int) c, (int) i)));
        return dbOf (pk);
    }

    void setParam (WhaleAudioProcessor& p, const char* id, float v)
    {
        auto* prm = p.state.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }

    // Run the processor on the mix generator for `seconds`, pumping the message-thread logic.
    void run (WhaleAudioProcessor& p, MixGen& gen, double seconds, juce::AudioBuffer<float>* capture = nullptr, WhaleEditor* ed = nullptr)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double acc = 0;
        const int n = (int) (seconds * sr / block);
        for (int k = 0; k < n; ++k)
        {
            gen.fill (buf);
            p.processBlock (buf, midi);
            if (capture != nullptr && k * block + block <= capture->getNumSamples())
                for (int c = 0; c < 2; ++c) capture->copyFrom (c, k * block, buf, c, 0, block);
            acc += block / sr;
            if (acc >= 1.0 / 30.0) { acc = 0; TestAccess::pump (p); if (ed != nullptr) TestAccess::tickEditor (*ed); }
        }
    }

    // ------------------------------------------------------------------------------------------- tests

    void testTransparency()
    {
        std::cout << "DSP neutral = transparent (apart from the latency)\n";
        WhaleDSP d;
        d.prepare (sr, block, 2);
        WhaleDSP::Settings s;   // neutral
        const int lat = d.getLatencySamples();
        juce::AudioBuffer<float> b (2, block);
        std::vector<float> in, out;
        for (int k = 0; k < 200; ++k)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = 0.25f * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * (k * block + i) / sr);
                b.setSample (0, i, v); b.setSample (1, i, v * 0.7f);
                in.push_back (v);
            }
            d.process (b, s);
            for (int i = 0; i < block; ++i) out.push_back (b.getSample (0, i));
        }
        double err = 0, ref = 0;
        for (size_t i = 20000; i < in.size(); ++i) { const double e = out[i] - in[i - (size_t) lat]; err += e * e; ref += (double) in[i] * in[i]; }
        const double snr = 10.0 * std::log10 (ref / std::max (err, 1.0e-20));
        std::cout << "    latency " << lat << " samples, null depth " << snr << " dB\n";
        CHECK (lat > 60 && lat < 200, "latency reported (oversampling + look-ahead)");
        CHECK (snr > 50.0, "neutral chain nulls against the delayed input (> 50 dB)");
    }

    void testCeiling()
    {
        std::cout << "True-peak ceiling under +18 dB drive with the clipper\n";
        WhaleDSP d;
        d.prepare (sr, block, 2);
        WhaleDSP::Settings s;
        s.driveDb = 18.0f; s.clip = 0.6f; s.satDrive = 0.3f; s.glue = 0.5f; s.ceilingDb = -1.0f;
        MixGen gen; gen.gainDb = -6.0f;
        juce::AudioBuffer<float> all (2, (int) (8 * sr)), b (2, block);
        for (int k = 0; k * block + block <= all.getNumSamples(); ++k)
        {
            gen.fill (b);
            d.process (b, s);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, k * block, b, c, 0, block);
        }
        const float tp = truePeakDb (all);
        std::cout << "    true peak " << tp << " dBTP\n";
        CHECK (tp <= -0.9f, "output true peak <= ceiling (+0.1 dB tolerance)");
        bool finite = true;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < all.getNumSamples(); ++i) finite &= std::isfinite (all.getSample (c, i));
        CHECK (finite, "no NaN / inf");
    }

    void testLoudnessLoopAndAB()
    {
        std::cout << "Processor: loudness target (Club -8) is reached, A/B is level-matched\n";
        WhaleAudioProcessor p;
        p.prepareToPlay (sr, block);
        p.applyPreset (0);                 // Club Punch (clipper + glue: the limiter does not do all the work)
        setParam (p, PID::target, 3.0f);   // Club -8 -> loud passages at -7
        MixGen gen; gen.gainDb = -20.0f;
        run (p, gen, 40.0);
        const float out = p.getDSP().getOutputLoudness();
        std::cout << "    output short-term " << out << " LUFS (target " << p.getEffectiveTarget() << ")\n";
        CHECK (std::abs (out - p.getEffectiveTarget()) < 1.0f, "closed loop holds the target within 1 dB");
        setParam (p, PID::bypass, 1.0f);
        run (p, gen, 6.0);
        const float byp = p.getDSP().getOutputLoudness();
        std::cout << "    bypass (SANS) loudness " << byp << " LUFS\n";
        CHECK (std::abs (byp - out) < 1.0f, "SANS is played at the same loudness as AVEC");
    }

    void testAutoProfiles()
    {
        std::cout << "AUTO against the reference profiles\n";
        // A mix that already sits on the Tech House medians should get (almost) no EQ moves.
        const auto& p = AutoMaster::profile (styleTechHouse);
        AudioFeatures f;
        f.valid = true; f.integratedLufs = -16; f.plr = 13; f.lowRatio = p.lowRatio.median; f.subRatio = p.subRatio.median; f.lowMidRatio = p.lowMidRatio.median;
        f.presenceRatio = p.presenceRatio.median; f.highRatio = p.highRatio.median; f.sideRatio = p.sideRatio.median; f.correlation = 0.9f; f.monoLossDb = 3.1f;
        auto r = AutoMaster::compute (f, styleTechHouse, 126.0, styleTarget, -1.0f);
        float moves = std::abs (r.base.toneLow) + std::abs (r.base.toneAir);
        for (int b = 0; b < 4; ++b) moves += std::abs (r.base.specGain[b]);
        CHECK (r.valid && moves < 0.01f, "a mix inside the corridor gets no EQ moves");
        CHECK (std::abs (r.loudPassageTarget - p.st_drop.median) < 0.01f, "style target = loud-passage median of the references");
        CHECK (r.base.widthMono > 100.0f, "club style: bass kept mono");

        // Muddy + harsh mix
        auto g = f;
        g.lowMidRatio = p.lowMidRatio.max * 1.6f; g.presenceRatio = p.presenceRatio.max * 1.8f;
        r = AutoMaster::compute (g, styleTechHouse, 126.0, styleTarget, -1.0f);
        CHECK (r.base.specGain[1] < -0.5f, "muddy mix: low-mid cut");
        CHECK (r.base.specGain[2] < -0.5f && r.base.specDyn[2] > 0.4f, "harsh mix: dynamic presence cut");
        for (auto& l : r.report) std::cout << "      > " << l << "\n";

        // Style detection by tempo
        CHECK (AutoMaster::detectStyle (f, 174.0) == styleDnb, "174 BPM -> Drum & Bass");
        CHECK (AutoMaster::detectStyle (f, 155.0) == styleHardTechno, "155 BPM -> Hard Techno");
        CHECK (AutoMaster::detectStyle (f, 85.0) == styleLofi, "85 BPM -> Lo-fi");
        CHECK (AutoMaster::detectStyle (f, 126.0) == styleTechHouse || AutoMaster::detectStyle (f, 126.0) == styleAfro, "126 BPM -> house family");
        // Lo-fi: never squashed
        auto lf = AutoMaster::compute (f, styleLofi, 85.0, styleTarget, -1.0f);
        CHECK (lf.base.clipAmt < 0.01f && lf.base.compGlue < 0.3f && lf.loudPassageTarget < -10.0f, "lo-fi: soft target, no clipper");
    }

    void testAutoEndToEnd()
    {
        std::cout << "AUTO end to end on a synthetic mix (stereo bass)\n";
        WhaleAudioProcessor p;
        p.prepareToPlay (sr, block);
        MixGen gen; gen.gainDb = -18.0f; gen.stereoBass = true;
        setParam (p, PID::style, (float) styleTechHouse);   // (synthetic loop: style detection is tested on real tracks)
        run (p, gen, 3.0);
        p.toggleAuto();
        run (p, gen, 14.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::verifying, "after 12 s of audio AUTO has applied and verifies");
        run (p, gen, 8.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::done, "verification finished");
        const auto& r = p.getAutoResult();
        CHECK (r.valid && r.report.size() >= 3, "report written");
        for (auto& l : r.report) std::cout << "      > " << l << "\n";
        CHECK (p.state.getRawParameterValue (PID::widthMono)->load() >= 100.0f, "stereo bass -> mono bass enabled");
        run (p, gen, 15.0);
        std::cout << "    verdict: " << p.getVerdictText() << ", loudness error " << p.getLoudnessError() << " dB\n";
        CHECK (p.getVerdict() == WhaleAudioProcessor::Verdict::validated, "master validated (on target, no problem)");
        CHECK (p.getSouffleCount() == 1, "le souffle triggered exactly once");
    }

    void testPresetsAndState()
    {
        std::cout << "Presets and project state\n";
        WhaleAudioProcessor p;
        p.prepareToPlay (sr, block);
        CHECK (p.presets.getAll().size() >= 20, "factory presets available (" + std::to_string (p.presets.getAll().size()) + ")");
        int perStyle[numStyles] {};
        for (auto& pr : p.presets.getAll()) perStyle[pr.style]++;
        bool all = true;
        for (int s = 1; s < numStyles; ++s) all &= perStyle[s] >= 3;
        CHECK (all, "at least 3 presets per style");
        p.applyPreset (0);
        const float glue = p.state.getRawParameterValue (PID::compGlue)->load();
        CHECK (std::abs (glue - p.presets.getAll()[0].base.compGlue) < 0.01f, "preset applied to the modules");
        setParam (p, PID::mAir, 0.8f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        WhaleAudioProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (std::abs (q.state.getRawParameterValue (PID::mAir)->load() - 0.8f) < 0.01f, "macros restored with the project");
        CHECK (q.presets.getCurrentName() == p.presets.getCurrentName(), "current preset name restored");
    }

    // ------------------------------------------------------------------------------------------- snapshots

    struct FileSource
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::int64 pos = 0;
        bool load (const juce::File& f, double startSec)
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            reader.reset (fm.createReaderFor (f));
            if (reader == nullptr) return false;
            pos = (juce::int64) (startSec * reader->sampleRate);
            return true;
        }
    };

    void snapshot (WhaleEditor& ed, const juce::File& file)
    {
        auto& c = TestAccess::content (ed);
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        file.deleteFile();
        juce::FileOutputStream os (file);
        png.writeImageToStream (img, os);
        std::cout << "  wrote " << file.getFullPathName() << "\n";
    }

    void runSnapshots (const juce::File& dir, const juce::String& wav)
    {
        dir.createDirectory();
        WhaleAudioProcessor p;
        p.prepareToPlay (sr, block);
        std::unique_ptr<WhaleEditor> ed (dynamic_cast<WhaleEditor*> (p.createEditor()));
        FileSource src;
        bool haveFile = wav.isNotEmpty() && src.load (juce::File (wav), 60.0);
        MixGen gen; gen.gainDb = -16.0f;
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double acc = 0;
        auto play = [&] (double seconds, float gainDb = 0.0f)
        {
            for (int k = 0; k < (int) (seconds * sr / block); ++k)
            {
                if (haveFile && src.reader != nullptr) { src.reader->read (&buf, 0, block, src.pos, true, true); src.pos += block; buf.applyGain (juce::Decibels::decibelsToGain (gainDb - 6.0f)); }
                else gen.fill (buf);
                p.processBlock (buf, midi);
                acc += block / sr;
                if (acc >= 1.0 / 60.0) { acc = 0; TestAccess::pump (p); TestAccess::tickEditor (*ed); }
            }
        };
        play (6.0);
        snapshot (*ed, dir.getChildFile ("whale_1_ready.png"));
        p.toggleAuto();
        play (7.0);
        snapshot (*ed, dir.getChildFile ("whale_2_auto_listening.png"));
        play (8.0);
        play (6.0);
        snapshot (*ed, dir.getChildFile ("whale_3_validated_souffle.png"));
        play (8.0);
        TestAccess::openExpert (*ed, 0); play (1.0);
        snapshot (*ed, dir.getChildFile ("whale_4_expert_spectral.png"));
        TestAccess::openExpert (*ed, 7); play (0.5);
        snapshot (*ed, dir.getChildFile ("whale_5_rapport_auto.png"));
        TestAccess::openExpert (*ed, -1);
        TestAccess::showBrowser (*ed); play (0.5);
        snapshot (*ed, dir.getChildFile ("whale_6_presets.png"));
        {
            auto& b = TestAccess::browser (*ed);
            auto img = b.createComponentSnapshot (b.getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; auto f = dir.getChildFile ("debug_browser.png"); f.deleteFile();
            juce::FileOutputStream os (f); png.writeImageToStream (img, os);
        }
        // too loud: manual gain far above the style target
        TestAccess::hideBrowser (*ed);
        setParam (p, PID::target, (float) manualTarget);
        setParam (p, PID::manualDrive, 9.0f);
        play (6.0);
        snapshot (*ed, dir.getChildFile ("whale_7_trop_fort.png"));
        setParam (p, PID::target, (float) styleTarget);
        setParam (p, PID::manualDrive, 0.0f);
        // phase problem: polarity-inverted right channel (synthetic source)
        gen.invertR = true;
        const bool hadFile = haveFile;
        haveFile = false; juce::ignoreUnused (hadFile);
        play (6.0);
        snapshot (*ed, dir.getChildFile ("whale_8_phase.png"));
    }

    void renderFile (const juce::File& in, const juce::File& out, int style)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (in));
        if (r == nullptr) { std::cout << "cannot read " << in.getFullPathName() << "\n"; ++failures; return; }
        WhaleAudioProcessor p;
        p.setRateAndBufferSizeDetails (r->sampleRate, block);
        p.prepareToPlay (r->sampleRate, block);
        setParam (p, PID::style, (float) style);
        const juce::int64 len = r->lengthInSamples;
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        // pass 1: listen from the loudest region (middle of the track) and let AUTO apply
        p.toggleAuto();
        juce::int64 pos = len / 3;
        double acc = 0;
        while (p.getAutoState() != WhaleAudioProcessor::AutoState::done && pos < len)
        {
            r->read (&buf, 0, block, pos, true, true); pos += block;
            p.processBlock (buf, midi);
            acc += block / r->sampleRate;
            if (acc >= 1.0 / 30.0) { acc = 0; TestAccess::pump (p); }
        }
        for (auto& l : p.getAutoResult().report) std::cout << "  > " << l << "\n";
        for (auto& l : p.getAutoResult().mixNotes) std::cout << "  ! " << l << "\n";
        // pass 2: render the whole file (latency compensated)
        p.reset();
        const int lat = p.getLatencySamples();
        out.deleteFile();
        juce::WavAudioFormat wavFmt;
        std::unique_ptr<juce::AudioFormatWriter> w (wavFmt.createWriterFor (new juce::FileOutputStream (out), r->sampleRate, 2, 24, {}, 0));
        juce::int64 written = 0;
        pos = 0;
        while (written < len)
        {
            r->read (&buf, 0, block, pos, true, true); pos += block;
            p.processBlock (buf, midi);
            acc += block / r->sampleRate;
            if (acc >= 1.0 / 30.0) { acc = 0; TestAccess::pump (p); }
            int start = 0;
            if (pos - block < lat) start = (int) juce::jmin ((juce::int64) block, lat - (pos - block));
            const int n = (int) juce::jmin ((juce::int64) (block - start), len - written);
            if (n > 0) { w->writeFromAudioSampleBuffer (buf, start, n); written += n; }
        }
        std::cout << "  rendered " << out.getFullPathName() << "\n";
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));

    if (args[0] == "--snap") { runSnapshots (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), args[2]); return failures; }
    if (args[0] == "--render") { renderFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), juce::File::getCurrentWorkingDirectory().getChildFile (args[2]), args[3].isEmpty() ? 0 : args[3].getIntValue()); return failures; }

    testTransparency();
    testCeiling();
    testLoudnessLoopAndAB();
    testAutoProfiles();
    testAutoEndToEnd();
    testPresetsAndState();
    std::cout << (failures == 0 ? "\nALL TESTS PASSED\n" : "\nFAILURES: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
