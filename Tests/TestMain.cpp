// WHALE offline tests: DSP safety, loudness, AUTO against the reference profiles, presets, state, snapshots.
//
//   WhaleTests                       run all tests
//   WhaleTests --snap <dir> [wav]    render screenshots of the editor (optionally driven by a real track)
//   WhaleTests --render <in.wav> <out.wav> [style]   master a file offline with AUTO (for listening tests)
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Auto/SpectralCapture.h"

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
    static void showLicence (WhaleEditor& e, bool show) { e.showLicence (show); }
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
        // 96 kHz (oversampling x2 there): same guarantee
        {
            WhaleDSP d96; d96.prepare (96000.0, block, 2);
            juce::AudioBuffer<float> all96 (2, (int) (6 * 96000)), b96 (2, block);
            MixGen g96; g96.gainDb = -6.0f;
            for (int k = 0; k * block + block <= all96.getNumSamples(); ++k)
            {
                g96.fill (b96);
                d96.process (b96, s);
                for (int c = 0; c < 2; ++c) all96.copyFrom (c, k * block, b96, c, 0, block);
            }
            const float tp96 = truePeakDb (all96);
            std::cout << "    96 kHz true peak " << tp96 << " dBTP\n";
            CHECK (tp96 <= -0.9f, "96 kHz: output true peak <= ceiling");
        }
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
        std::cout << "    output short-term " << out << " LUFS (target " << p.getEffectiveTarget() << "), corr " << p.getDSP().getLoudnessCorrectionDb() << " GR " << p.getDSP().getGainReductionDb() << "\n";
        CHECK (std::abs (out - p.getEffectiveTarget()) < 1.0f, "closed loop holds the target within 1 dB");
        setParam (p, PID::bypass, 1.0f);
        run (p, gen, 6.0);
        const float byp = p.getDSP().getHeardLoudness();     // what is HEARD (the old test read the processed path)
        std::cout << "    bypass (SANS) heard loudness " << byp << " LUFS, match gain " << p.getDSP().getMatchGainDb() << " dB\n";
        CHECK (std::abs (byp - out) < 0.7f, "SANS is played at the same loudness as AVEC (within 0.7 dB)");
        CHECK (p.isMatchMeasured(), "A/B match measured");
    }

    void testIndicators()
    {
        std::cout << "Indicators (what is heard)\n";
        // true peak: a sine at fs/4 sampled at 45 degrees has samples at 0.707 but a true peak of 1.0 (0 dBTP)
        {
            WhaleDSP d; d.prepare (sr, block, 2);
            WhaleDSP::Settings s; s.bypass = true; s.matchAB = false;
            juce::AudioBuffer<float> b (2, block);
            double ph = juce::MathConstants<double>::pi / 4;
            float tp = -100;
            for (int k = 0; k < 40; ++k)
            {
                for (int i = 0; i < block; ++i) { const float v = (float) std::sin (ph); ph += juce::MathConstants<double>::halfPi; b.setSample (0, i, v); b.setSample (1, i, v); }
                d.process (b, s);
                tp = std::max (tp, d.takeMaxPeakDb());
            }
            std::cout << "    sine fs/4 at 45 deg (sample peak -3 dB): meter " << tp << " dBTP\n";
            CHECK (tp > -0.6f && tp < 0.3f, "TRUE PEAK meter reads the inter-sample peak");
        }
        // bass mono: 60 Hz in phase vs in opposition
        auto bassMono = [] (bool opposite)
        {
            WhaleDSP d; d.prepare (sr, block, 2);
            WhaleDSP::Settings s; s.bypass = true; s.matchAB = false;
            juce::AudioBuffer<float> b (2, block);
            double ph = 0;
            for (int k = 0; k < (int) (2.0 * sr / block); ++k)
            {
                for (int i = 0; i < block; ++i) { const float v = 0.3f * (float) std::sin (ph); ph += 2 * juce::MathConstants<double>::pi * 60.0 / sr; b.setSample (0, i, v); b.setSample (1, i, opposite ? -v : v); }
                d.process (b, s);
            }
            return d.getBassMono();
        };
        const float monoIn = bassMono (false), monoOpp = bassMono (true);
        std::cout << "    bass mono: in phase " << monoIn * 100 << " %, opposite " << monoOpp * 100 << " %\n";
        CHECK (monoIn > 0.98f && monoOpp < 0.05f, "GRAVES MONO measures the low end");
        // tonality: available after some playing, finite
        {
            WhaleAudioProcessor p; p.prepareToPlay (sr, block);
            setParam (p, PID::style, (float) styleTechHouse);
            MixGen gen; gen.gainDb = -14.0f;
            run (p, gen, 25.0);
            std::cout << "    TONALITE on the synthetic mix: " << p.getToneDistance() << " dB, expected crest " << p.getExpectedCrest() << " dB\n";
            CHECK (p.getToneDistance() >= 0.0f && p.getToneDistance() < 20.0f, "TONALITE meter measured after 25 s");
        }
    }

    void testSpectralCapture()
    {
        std::cout << "AUTO measurement = profile measurement (FFT bands, loud frames)\n";
        // 50 Hz (amplitude a) + 1 kHz (amplitude b), mono: low ratio = a^2 / (a^2 + b^2), no sides
        const float a = 0.4f, bAmp = 0.2f;
        juce::AudioBuffer<float> buf (2, (int) (sr * 20));
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const float v = a * (float) std::sin (2 * juce::MathConstants<double>::pi * 50.0 * t) + bAmp * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * t);
            buf.setSample (0, i, v); buf.setSample (1, i, v);
        }
        const auto c = SpectralCapture::analyse (buf, sr);
        const float expected = a * a / (a * a + bAmp * bAmp);
        std::cout << "    low ratio " << c.lowRatio << " (expected " << expected << "), sub " << c.subRatio << ", side " << c.sideRatio << ", TP " << c.truePeakDb << "\n";
        CHECK (c.valid && std::abs (c.lowRatio - expected) < 0.01f && std::abs (c.subRatio - expected) < 0.01f, "band ratios exact on a known signal");
        CHECK (c.sideRatio < 1.0e-6f, "mono signal: no side energy");
        CHECK (std::abs (c.thirdOctDb[3] - 10.0f * std::log10 (expected)) < 0.2f, "1/3-octave band holds the 50 Hz tone");
    }

    AudioFeatures featuresFromProfile (const StyleProfileData& p)
    {
        AudioFeatures f;
        f.valid = true; f.hasSpectrum = true; f.integratedLufs = -16; f.truePeakDb = -16 + 13; f.plr = 13; f.durationSec = 30; f.loudSeconds = 18; f.loudLufs = -14;
        f.lowRatio = p.lowRatio.median; f.subRatio = p.subRatio.median; f.lowMidRatio = p.lowMidRatio.median; f.presenceRatio = p.presenceRatio.median;
        f.highRatio = p.highRatio.median; f.sideRatio = p.sideRatio.median; f.correlation = 0.9f; f.monoLossDb = 3.1f;
        for (int b = 0; b < 29; ++b) f.thirdOctDb[b] = p.thirdOctMedian[b];
        return f;
    }

    void testAutoProfiles()
    {
        std::cout << "AUTO decisions against the reference profiles\n";
        const auto& p = AutoMaster::profile (styleTechHouse);
        auto eqMoves = [] (const AutoResult& r) { float m = std::abs (r.base.toneLow) + std::abs (r.base.toneAir); for (int b = 0; b < 4; ++b) m += std::abs (r.base.specGain[b]); return m; };
        auto f = featuresFromProfile (p);
        auto r = AutoMaster::compute (f, styleTechHouse, 126.0, styleTarget, -1.0f);
        CHECK (r.valid && eqMoves (r) < 0.01f, "a mix on the references' median: no EQ");
        CHECK (std::abs (r.loudPassageTarget - p.st_drop.median) < 0.01f, "style target = loud-passage median of the references");
        CHECK (r.base.widthMono > 100.0f, "club style: bass kept mono");

        auto shifted = [&] (int lo, int hi, float db) { auto g = featuresFromProfile (p); for (int b = lo; b <= hi; ++b) g.thirdOctDb[b] += db; return g; };
        r = AutoMaster::compute (shifted (9, 13, 0.8f), styleTechHouse, 126.0, styleTarget, -1.0f);
        CHECK (eqMoves (r) < 0.01f, "small deviation (+0.8 dB) stays inside the tolerance: no EQ");

        r = AutoMaster::compute (shifted (9, 13, 6.0f), styleTechHouse, 126.0, styleTarget, -1.0f);
        std::cout << "    muddy +6 dB 200-500 Hz -> 300 Hz bell " << r.base.specGain[1] << " dB\n";
        CHECK (r.base.specGain[1] <= -2.5f, "muddy mix (+6 dB at 200-500 Hz): clear low-mid cut");
        r = AutoMaster::compute (shifted (19, 23, 5.0f), styleTechHouse, 126.0, styleTarget, -1.0f);
        std::cout << "    harsh +5 dB 2-5 kHz -> 3.5 kHz bell " << r.base.specGain[2] << " dB, dynamic " << r.base.specDyn[2] << "\n";
        CHECK (r.base.specGain[2] <= -2.0f && r.base.specDyn[2] >= 0.5f, "harsh mix: presence cut, dynamic on the peaks");
        {
            auto g = featuresFromProfile (p);
            for (int b = 1; b <= 8; ++b) g.thirdOctDb[b] += 6.0f;
            for (int b = 24; b <= 28; ++b) g.thirdOctDb[b] -= 6.0f;
            r = AutoMaster::compute (g, styleTechHouse, 126.0, styleTarget, -1.0f);
            std::cout << "    boomy + dull -> bass shelf " << r.base.toneLow << ", 60 Hz " << r.base.specGain[0] << ", air " << r.base.toneAir << ", 8 kHz " << r.base.specGain[3] << "\n";
            CHECK (r.base.toneLow + r.base.specGain[0] <= -2.5f, "boomy mix: low end reduced");
            CHECK (r.base.toneAir + r.base.specGain[3] >= 2.5f, "dull mix: highs lifted");
            for (auto& l : r.report) std::cout << "      > " << l << "\n";
            // the fitted EQ actually brings the curve back towards the references
            const auto t = AutoMaster::analyseTone (g, p, 1.0f);
            auto corrected = g;
            for (int b = 0; b < 29; ++b)
            {
                corrected.thirdOctDb[b] += AutoMaster::toolResponseDb (0, profileThirdOctHz[b], r.base.toneLow) + AutoMaster::toolResponseDb (5, profileThirdOctHz[b], r.base.toneAir);
                for (int k = 0; k < 4; ++k) corrected.thirdOctDb[b] += AutoMaster::toolResponseDb (k + 1, profileThirdOctHz[b], r.base.specGain[k], r.base.specFreq[k]);
            }
            const auto t2 = AutoMaster::analyseTone (corrected, p, 1.0f);
            std::cout << "    tonal distance " << t.error << " dB -> " << t2.error << " dB after the EQ\n";
            CHECK (t2.error < 0.4f * t.error, "EQ removes most of the distance to the references");
        }
        // Dynamics: a very dynamic pre-master gets clipper + compression, an over-compressed one does not
        {
            auto g = featuresFromProfile (p); g.truePeakDb = g.loudLufs + 17.0f;
            r = AutoMaster::compute (g, styleTechHouse, 126.0, styleTarget, -1.0f);
            CHECK (r.base.clipAmt >= 0.6f && r.base.compGlue >= 0.4f, "crest 17 dB: clipper and compression share the work");
            g.truePeakDb = g.loudLufs + 5.0f;
            r = AutoMaster::compute (g, styleTechHouse, 126.0, styleTarget, -1.0f);
            CHECK (r.base.compGlue < 0.01f && ! r.mixNotes.isEmpty(), "crest 5 dB: nothing more squashed, the mix is flagged");
        }
        // Low-cut: rumble under 40 Hz is removed, a clean low end is left alone
        {
            auto clean = AutoMaster::compute (featuresFromProfile (p), styleTechHouse, 126.0, styleTarget, -1.0f);
            CHECK (clean.base.lowCut < 1.0f, "low end on the references: no low-cut");
            auto g = featuresFromProfile (p);
            g.thirdOctDb[0] += 14.0f; g.thirdOctDb[1] += 12.0f; g.thirdOctDb[2] += 6.0f;
            auto rr = AutoMaster::compute (g, styleTechHouse, 126.0, styleTarget, -1.0f);
            std::cout << "    rumble +14 dB at 25 Hz -> low-cut " << rr.base.lowCut << " Hz, 60 Hz bell " << rr.base.specGain[0] << " dB\n";
            CHECK (rr.base.lowCut >= 20.0f && rr.base.lowCut <= 40.0f, "rumble under 40 Hz: low-cut chosen between 20 and 40 Hz");
            CHECK (AutoMaster::lowCutResponseDb (rr.base.lowCut, 63.0f) > -1.0f, "the kick (63 Hz) is kept");
        }
        // Style detection by tempo
        CHECK (AutoMaster::detectStyle (featuresFromProfile (AutoMaster::profile (styleDnb)), 174.0) == styleDnb, "DnB curve at 174 BPM -> Drum & Bass");
        CHECK (AutoMaster::detectStyle (f, 155.0) == styleHardTechno, "155 BPM -> Hard Techno");
        {
            auto lofiF = featuresFromProfile (AutoMaster::profile (styleLofi)); lofiF.highRatio = 0.01f; lofiF.plr = 13.0f;
            CHECK (AutoMaster::detectStyle (lofiF, 85.0) == styleLofi, "dark, dynamic, 85 BPM -> Lo-fi");
            auto at = featuresFromProfile (AutoMaster::profile (styleAfroTech));
            CHECK (AutoMaster::detectStyle (at, 128.0) == styleAfroTech, "Afro Tech curve at 128 BPM -> Afro Tech / Riddim");
            auto dh = featuresFromProfile (AutoMaster::profile (styleDancehall));
            CHECK (AutoMaster::detectStyle (dh, 113.0) == styleDancehall, "Dancehall curve at 113 BPM -> Afro / Dancehall");
            auto af = featuresFromProfile (AutoMaster::profile (styleAfro));
            CHECK (AutoMaster::detectStyle (af, 122.0) == styleAfro, "Afro House curve at 122 BPM -> Afro / Latin House");
        }
        CHECK (AutoMaster::detectStyle (f, 126.0) == styleTechHouse || AutoMaster::detectStyle (f, 126.0) == styleAfro, "126 BPM -> house family");
        {
            // Hesitation: a curve halfway between Tech House and Afro House is a close call, the profile itself is not
            int alt = -1; float margin = -1.0f;
            const int st = AutoMaster::detectStyle (featuresFromProfile (AutoMaster::profile (styleDnb)), 174.0, &alt, &margin);
            CHECK (st == styleDnb && alt > 0 && alt != st && margin >= 0.3f, "DnB profile at 174 BPM: clear call (no hesitation)");
            auto th = featuresFromProfile (AutoMaster::profile (styleTechHouse)), afh = featuresFromProfile (AutoMaster::profile (styleAfro)), mix = th;
            for (int k = 0; k < 29; ++k) mix.thirdOctDb[k] = 0.5f * (th.thirdOctDb[k] + afh.thirdOctDb[k]);
            mix.lowRatio = 0.5f * (th.lowRatio + afh.lowRatio); mix.subRatio = 0.5f * (th.subRatio + afh.subRatio);
            mix.lowMidRatio = 0.5f * (th.lowMidRatio + afh.lowMidRatio); mix.presenceRatio = 0.5f * (th.presenceRatio + afh.presenceRatio);
            mix.highRatio = 0.5f * (th.highRatio + afh.highRatio); mix.sideRatio = 0.5f * (th.sideRatio + afh.sideRatio);
            mix.plr = 0.5f * (th.plr + afh.plr);
            const int sm = AutoMaster::detectStyle (mix, 0.0, &alt, &margin);
            std::cout << "    TH/Afro blend -> style " << sm << ", second " << alt << ", margin " << margin << "\n";
            const auto rm = AutoMaster::compute (mix, styleAuto, 0.0, styleTarget, -1.0f);
            CHECK (rm.style == sm && (margin >= 0.3f ? rm.styleAlt == 0 : rm.styleAlt == alt), "close call reported as styleAlt (margin < 0.3)");
        }
        auto lf = AutoMaster::compute (featuresFromProfile (AutoMaster::profile (styleLofi)), styleLofi, 85.0, styleTarget, -1.0f);
        CHECK (lf.base.clipAmt <= 0.2f && lf.base.compGlue < 0.3f && lf.loudPassageTarget < -10.0f, "lo-fi: soft target, light clipper at most");
    }

    void testLowCut()
    {
        std::cout << "Low-cut filter (BASSES/AIR)\n";
        auto level = [] (float hz, float cut)
        {
            WhaleDSP d;
            d.prepare (sr, block, 2);
            WhaleDSP::Settings s; s.lowCutHz = cut;
            juce::AudioBuffer<float> b (2, block);
            double ph = 0, acc = 0; int cnt = 0;
            for (int k = 0; k < (int) (3.0 * sr / block); ++k)
            {
                for (int i = 0; i < block; ++i) { const float v = 0.25f * (float) std::sin (ph); ph += 2 * juce::MathConstants<double>::pi * hz / sr; b.setSample (0, i, v); b.setSample (1, i, v); }
                d.process (b, s);
                if (k > (int) (1.5 * sr / block)) { for (int i = 0; i < block; ++i) acc += b.getSample (0, i) * b.getSample (0, i); cnt += block; }
            }
            return 10.0 * std::log10 (acc / cnt / (0.25 * 0.25 / 2));
        };
        const double r20 = level (20.0f, 30.0f), r100 = level (100.0f, 30.0f), off20 = level (20.0f, 0.0f);
        std::cout << "    30 Hz low-cut: 20 Hz " << r20 << " dB, 100 Hz " << r100 << " dB; off: 20 Hz " << off20 << " dB\n";
        CHECK (r20 < -12.0, "20 Hz rumble strongly reduced");
        CHECK (std::abs (r100) < 0.3, "100 Hz untouched");
        CHECK (std::abs (off20) < 0.3, "off = transparent");
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
        run (p, gen, 20.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::listening, "AUTO keeps listening (30 s)");
        run (p, gen, 12.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::verifying, "after 30 s of audio AUTO has applied and measures the output");
        run (p, gen, 32.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::done, "output verification and second pass finished");
        CHECK (p.getAutoResult().toneErrorAfter >= 0.0f, "final measurement of the processed output reported");
        const auto& r = p.getAutoResult();
        CHECK (r.valid && r.report.size() >= 3, "report written");
        for (auto& l : r.report) std::cout << "      > " << l << "\n";
        CHECK (p.state.getRawParameterValue (PID::widthMono)->load() >= 100.0f, "stereo bass -> mono bass enabled");
        run (p, gen, 15.0);
        std::cout << "    out ST " << p.getDSP().getOutputLoudness() << " target " << p.getEffectiveTarget() << " corr " << p.getDSP().getLoudnessCorrectionDb()
                  << " GR " << p.getDSP().getGainReductionDb() << " in ST " << p.getDSP().getInputLoudness() << "\n";
        std::cout << "    verdict: " << p.getVerdictText() << ", loudness error " << p.getLoudnessError() << " dB\n";
        const auto v = p.getVerdict();
        CHECK (v != WhaleAudioProcessor::Verdict::clipping && v != WhaleAudioProcessor::Verdict::tooLoud && v != WhaleAudioProcessor::Verdict::phase,
               "no problem state after AUTO");
        CHECK (p.getSouffleCount() == (v == WhaleAudioProcessor::Verdict::validated ? 1 : 0), "le souffle only when validated, once");
    }

    void testUndoAuto()
    {
        std::cout << "Annuler AUTO\n";
        WhaleAudioProcessor p;
        p.prepareToPlay (sr, block);
        MixGen gen; gen.gainDb = -18.0f; gen.stereoBass = true;
        setParam (p, PID::style, (float) styleTechHouse);
        p.applyPreset (2);                                  // a preset chosen before AUTO
        setParam (p, PID::mAir, 0.8f);                      // and a macro moved
        auto snap = [&] { std::vector<float> v; for (auto* prm : p.getParameters()) v.push_back (prm->getValue()); return v; };
        run (p, gen, 3.0);
        const auto before = snap();
        const int presetBefore = p.presets.getCurrent();
        const float targetBefore = p.getEffectiveTarget();
        CHECK (! p.canUndoAuto(), "nothing to undo before AUTO");
        p.toggleAuto();
        run (p, gen, 70.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::done && p.canUndoAuto(), "after AUTO: undo available");
        float changed = 0; { auto a = snap(); for (size_t i = 0; i < a.size(); ++i) changed = std::max (changed, std::abs (a[i] - before[i])); }
        CHECK (changed > 0.01f, "AUTO changed the settings");
        p.undoAuto();
        run (p, gen, 0.5);
        float diff = 0; { auto a = snap(); for (size_t i = 0; i < a.size(); ++i) diff = std::max (diff, std::abs (a[i] - before[i])); }
        std::cout << "    largest parameter difference after undo: " << diff << " (normalised)\n";
        CHECK (diff < 1.0e-4f, "undo restores every parameter (preset, macros, modules)");
        CHECK (p.presets.getCurrent() == presetBefore && std::abs (p.getEffectiveTarget() - targetBefore) < 0.01f, "undo restores the preset and the loudness target");
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::idle && ! p.canUndoAuto(), "one level of undo, AUTO back to idle");
        // cancel while AUTO verifies also restores everything
        p.toggleAuto();
        run (p, gen, 36.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::verifying, "AUTO verifying");
        p.toggleAuto();
        float diff2 = 0; { auto a = snap(); for (size_t i = 0; i < a.size(); ++i) diff2 = std::max (diff2, std::abs (a[i] - before[i])); }
        CHECK (diff2 < 1.0e-4f, "cancel during verification restores every parameter");
    }

    void testPresetLayering()
    {
        std::cout << "Presets layered on AUTO\n";
        WhaleAudioProcessor p;
        p.prepareToPlay (sr, block);
        MixGen gen; gen.gainDb = -18.0f; gen.stereoBass = true;
        setParam (p, PID::style, (float) styleTechHouse);
        run (p, gen, 3.0);
        p.toggleAuto();
        run (p, gen, 70.0);
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::done, "AUTO finished");
        auto val = [&] (const char* id) { return p.state.getRawParameterValue (id)->load(); };
        const float autoLow = val (PID::toneLow), autoCut = val (PID::lowCut), autoMono = val (PID::widthMono), autoSub = val (PID::specGain[0]);
        // a factory preset now adds its colour and keeps the correction
        int idx = -1;
        for (int i = 0; i < p.presets.getAll().size(); ++i) if (p.presets.getAll()[i].name == "Rolling Bass") idx = i;
        p.applyPreset (idx);
        const auto& pr = p.presets.getAll()[idx];
        std::cout << "    AUTO: shelf " << autoLow << ", sub bell " << autoSub << ", low-cut " << autoCut << " | + Rolling Bass: shelf " << val (PID::toneLow)
                  << ", sub bell " << val (PID::specGain[0]) << " @ " << val (PID::specFreq[0]) << " Hz, punch " << val (PID::compPunch) << "\n";
        CHECK (p.getAutoState() == WhaleAudioProcessor::AutoState::done && p.hasAutoCorrection(), "AUTO's correction is kept");
        CHECK (std::abs (val (PID::lowCut) - autoCut) < 0.5f && val (PID::widthMono) >= autoMono - 0.5f, "low-cut and mono bass kept");
        CHECK (std::abs (val (PID::compPunch) - pr.base.compPunch) < 0.01f, "the preset's punch is applied");
        // switching to another factory preset changes the colour, not the correction
        int idx2 = -1;
        for (int i = 0; i < p.presets.getAll().size(); ++i) if (p.presets.getAll()[i].name == "Deep & Warm") idx2 = i;
        p.applyPreset (idx2);
        CHECK (std::abs (val (PID::lowCut) - autoCut) < 0.5f && p.hasAutoCorrection(), "second preset: correction still there");
        CHECK (std::abs (val (PID::satDrive) - p.presets.getAll()[idx2].base.satDrive) < 0.01f, "second preset: its saturation");
        // project reload keeps both layers
        juce::MemoryBlock mb; p.getStateInformation (mb);
        WhaleAudioProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.hasAutoCorrection() && q.getCharacterPreset() == idx2, "layers restored with the project");
        // AUTO again with a character chosen: the character stays
        p.toggleAuto();
        run (p, gen, 70.0);
        CHECK (p.getCharacterPreset() == idx2 && std::abs (val (PID::satDrive) - p.presets.getAll()[idx2].base.satDrive) < 0.01f, "AUTO keeps the chosen character");
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
        {
            // within a style, two factory presets never sound alike: colour (RMS over 50 Hz - 12 kHz) or dynamics differ
            int closePairs = 0;
            const auto& ps = p.presets.getAll();
            for (int i = 0; i < ps.size(); ++i)
                for (int j = i + 1; j < ps.size(); ++j)
                {
                    if (! ps[i].factory || ! ps[j].factory || ps[i].style != ps[j].style) continue;
                    double se = 0;
                    for (int b = 3; b < 28; ++b) se += (ps[i].colour[b] - ps[j].colour[b]) * (ps[i].colour[b] - ps[j].colour[b]);
                    const double tone = std::sqrt (se / 25.0);
                    const auto& a = ps[i].base; const auto& c = ps[j].base;
                    const float dyn = std::max ({ std::abs (a.compPunch - c.compPunch), std::abs (a.satDrive - c.satDrive),
                                                  std::abs ((a.compGlue + a.clipAmt) - (c.compGlue + c.clipAmt)) * 0.5f });
                    if (tone < 1.0 && dyn < 0.25f && ps[i].target == ps[j].target)
                    {
                        ++closePairs;
                        std::cout << "    too close: " << ps[i].name << " / " << ps[j].name << " (colour " << tone << " dB, dynamics " << dyn << ")\n";
                    }
                }
            CHECK (closePairs == 0, "presets of a style are clearly different from each other");
            CHECK (ps[0].tags.isNotEmpty(), "presets describe what they do");
        }
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
        // low-cut and band centres: set by AUTO, kept by a factory preset (they correct the mix), saved with the project
        setParam (p, PID::lowCut, 32.0f);
        setParam (p, PID::specFreq[0], 110.0f);
        p.applyPreset (1);
        CHECK (std::abs (p.state.getRawParameterValue (PID::lowCut)->load() - 32.0f) < 0.6f, "factory preset keeps the low-cut");
        juce::MemoryBlock mb2;
        p.getStateInformation (mb2);
        WhaleAudioProcessor q2;
        q2.setStateInformation (mb2.getData(), (int) mb2.getSize());
        CHECK (std::abs (q2.state.getRawParameterValue (PID::lowCut)->load() - 32.0f) < 0.6f, "low-cut restored with the project");
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
            pos = std::min ((juce::int64) (startSec * reader->sampleRate), reader->lengthInSamples / 4);   // short files: start early
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

    // Marketing screenshots at 2x: a finished AUTO on a real song (full UI, report, expert, presets)
    void runPromo (const juce::File& dir, const juce::String& wav)
    {
        dir.createDirectory();
        WhaleAudioProcessor p;
        p.prepareToPlay (sr, block);
        std::unique_ptr<WhaleEditor> ed (dynamic_cast<WhaleEditor*> (p.createEditor()));
        FileSource src;
        if (! src.load (juce::File (wav), 60.0)) { std::cout << "cannot read " << wav << "\n"; ++failures; return; }
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double acc = 0;
        auto play = [&] (double seconds)
        {
            for (int k = 0; k < (int) (seconds * sr / block); ++k)
            {
                src.reader->read (&buf, 0, block, src.pos, true, true); src.pos += block;
                if (src.pos > src.reader->lengthInSamples - block) src.pos = (juce::int64) (src.reader->lengthInSamples / 4);
                buf.applyGain (juce::Decibels::decibelsToGain (-6.0f));
                p.processBlock (buf, midi);
                acc += block / sr;
                if (acc >= 1.0 / 60.0) { acc = 0; TestAccess::pump (p); TestAccess::tickEditor (*ed); }
            }
        };
        auto shot = [&] (juce::Component& c, const char* name)
        {
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png; auto f = dir.getChildFile (name); f.deleteFile();
            juce::FileOutputStream os (f); png.writeImageToStream (img, os);
            std::cout << "  wrote " << f.getFullPathName() << "\n";
        };
        play (4.0);
        p.toggleAuto();
        for (int i = 0; i < 240 && p.getAutoState() != WhaleAudioProcessor::AutoState::done; ++i) play (1.0);
        play (12.0);
        shot (TestAccess::content (*ed), "promo_main.png");
        TestAccess::openExpert (*ed, 7); play (0.5);
        shot (TestAccess::content (*ed), "promo_report.png");
        TestAccess::openExpert (*ed, 0); play (1.0);
        shot (TestAccess::content (*ed), "promo_expert.png");
        TestAccess::openExpert (*ed, -1);
        TestAccess::showBrowser (*ed); play (0.5);
        shot (TestAccess::browser (*ed), "promo_presets.png");
        TestAccess::hideBrowser (*ed);
        play (1.0);
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
        // licence: trial (header link + panel), then trial over
        LicenseManager::get().forceStatus (LicenseManager::Status::trial);
        TestAccess::showLicence (*ed, false); play (0.2);
        snapshot (*ed, dir.getChildFile ("whale_8a_essai_entete.png"));
        TestAccess::showLicence (*ed, true); play (0.2);
        snapshot (*ed, dir.getChildFile ("whale_8b_licence_essai.png"));
        LicenseManager::get().forceStatus (LicenseManager::Status::expired);
        TestAccess::showLicence (*ed, true); play (0.2);
        snapshot (*ed, dir.getChildFile ("whale_8c_licence_terminee.png"));
        LicenseManager::get().forceStatus (LicenseManager::Status::activated);
        TestAccess::showLicence (*ed, false);
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

static void testLicense()
{
    std::cout << "Licence (trial, Gumroad key, silence)\n";
    using LM = LicenseManager;
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("whale_lic_test");
    root.deleteRecursively();
    const auto mainDir = root.getChildFile ("main"), backDir = root.getChildFile ("back");
    juce::int64 t0 = juce::Time::currentTimeMillis();
    juce::int64 now = t0;
    const juce::int64 day = 24LL * 3600 * 1000;
    LM::VerifyReply reply;
    int calls = 0; bool lastIncrement = false;
    auto verifier = [&] (const juce::String&, bool inc) { ++calls; lastIncrement = inc; return reply; };
    auto clock = [&] { return juce::Time (now); };
    auto make = [&] { return std::make_unique<LM> (mainDir, backDir, verifier, clock); };

    {
        auto m = make();
        CHECK (m->getStatus() == LM::Status::trial && m->trialDaysLeft() == 7, "first run: 7-day trial");
        CHECK (m->mainFile().existsAsFile() && m->backupFile().existsAsFile(), "trial remembered in two files");
    }
    now = t0 + 3 * day + 1000;
    CHECK (make()->trialDaysLeft() == 4, "3 days later: 4 days left");
    now = t0 + 7 * day + 1000;
    CHECK (make()->getStatus() == LM::Status::expired, "after 7 days: trial over");
    {
        auto m = make();
        CHECK (m->isLocked(), "trial over and not activated: locked");
    }
    // deleting the main file does not restart the trial
    make()->mainFile().deleteFile();
    CHECK (make()->getStatus() == LM::Status::expired, "main file deleted: still over (backup)");
    // clock set back: no days given back
    now = t0 + day;
    CHECK (make()->getStatus() == LM::Status::expired, "clock set back: still over");
    // edited files: signature broken
    root.deleteRecursively();
    now = t0;
    make();
    for (auto f : { mainDir.getChildFile ("licence.whale"), backDir.getChildFile ("com.whaleaudio.whale.dat") })
    {
        auto x = juce::XmlDocument::parse (f);
        x->setAttribute ("start", juce::String (t0 + 30 * day));
        x->writeTo (f);
    }
    CHECK (make()->getStatus() == LM::Status::expired, "edited licence files are refused");

    // activation
    root.deleteRecursively();
    now = t0 + 10 * day;
    {
        auto m = make();
        reply = {}; reply.reached = false;
        auto r = m->activate ("6F0E4C97-B72A4E69-A11BF6C4-AF6517E7");
        CHECK (! r.ok && r.message.contains ("connection") && m->getStatus() == LM::Status::trial, "no connection: clear message, nothing changes");
        reply = {}; reply.reached = true; reply.success = false; reply.message = "That license does not exist for the provided product.";
        r = m->activate ("BAD-KEY-123456");
        CHECK (! r.ok && r.message.contains ("Unknown key"), "unknown key refused");
        reply = {}; reply.reached = true; reply.success = true; reply.uses = 1; reply.refunded = true;
        CHECK (! m->activate ("6F0E4C97-B72A4E69-A11BF6C4-AF6517E7").ok, "refunded purchase refused");
        reply.refunded = false; reply.uses = LM::maxActivations + 1;
        CHECK (! m->activate ("6F0E4C97-B72A4E69-A11BF6C4-AF6517E7").ok, "too many computers refused");
        reply.uses = 1; reply.email = "client@example.com";
        r = m->activate ("  6f0e4c97-b72a4e69-a11bf6c4-af6517e7 \n");
        CHECK (r.ok && lastIncrement && m->getStatus() == LM::Status::activated && m->getEmail() == "client@example.com", "valid key: activated (counts one computer)");
        CHECK (m->getKey() == "6F0E4C97-B72A4E69-A11BF6C4-AF6517E7", "key cleaned up (spaces, case)");
    }
    now = t0 + 400 * day;
    calls = 0;
    {
        auto m = make();
        CHECK (m->getStatus() == LM::Status::activated && ! m->isLocked() && calls == 0, "activated licence loads offline, a year later");
        reply = {}; reply.reached = false;
        CHECK (m->recheckNow() && m->getStatus() == LM::Status::activated, "re-check offline: still activated");
        CHECK (! lastIncrement, "re-check does not count a computer");
        reply = {}; reply.reached = true; reply.success = false; reply.message = "This license key has been disabled.";
        CHECK (! m->recheckNow() && m->getStatus() == LM::Status::expired, "disabled key: licence revoked");
    }
    CHECK (make()->getStatus() == LM::Status::expired, "revocation saved");
    root.deleteRecursively();

    // Gumroad replies
    {
        auto ok = LM::parseReply (R"({"success":true,"uses":2,"purchase":{"email":"a@b.c","refunded":false,"chargebacked":false,"disputed":false}})");
        CHECK (ok.reached && ok.success && ok.uses == 2 && ok.email == "a@b.c" && ! ok.refunded, "Gumroad success reply parsed");
        auto bad = LM::parseReply (R"({"success":false,"message":"That license does not exist for the provided product."})");
        CHECK (bad.reached && ! bad.success && bad.message.contains ("does not exist"), "Gumroad 404 reply parsed");
        auto cb = LM::parseReply (R"({"success":true,"uses":1,"purchase":{"chargebacked":true}})");
        CHECK (cb.refunded, "chargeback counts as refunded");
        CHECK (! LM::parseReply ("<html>proxy error</html>").reached, "non-JSON answer = not reached");
    }

    // silence when locked
    {
        WhaleAudioProcessor p;
        p.setRateAndBufferSizeDetails (44100.0, 512);
        p.prepareToPlay (44100.0, 512);
        setParam (p, PID::bypass, 1.0f);
        auto& lic = LicenseManager::get();
        lic.forceStatus (LM::Status::expired);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer midi;
        int silentBlocks = 0, loudBlocks = 0; double ph = 0;
        for (int k = 0; k < (int) (44.0 * 44100 / 512); ++k)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.3f * (float) std::sin (ph); ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / 44100.0; b.setSample (0, i, v); b.setSample (1, i, v); }
            p.processBlock (b, midi);
            const float m = b.getMagnitude (0, 512);
            if (m < 1.0e-5f) ++silentBlocks; else if (m > 0.1f) ++loudBlocks;
        }
        const double silentSec = silentBlocks * 512.0 / 44100.0;
        std::cout << "    locked: " << silentSec << " s silent over 44 s\n";
        CHECK (silentSec > 1.7 && silentSec < 2.1 && loudBlocks > 0, "trial over: 2 s of silence every 40 s");
        lic.forceStatus (LM::Status::activated);
        int silentAfter = 0;
        for (int k = 0; k < (int) (44.0 * 44100 / 512); ++k)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.3f * (float) std::sin (ph); ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / 44100.0; b.setSample (0, i, v); b.setSample (1, i, v); }
            p.processBlock (b, midi);
            if (b.getMagnitude (0, 512) < 1.0e-5f) ++silentAfter;
        }
        CHECK (silentAfter == 0, "activated: never silent");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    LicenseManager::get().forceStatus (LicenseManager::Status::activated);   // tests and renders are never silenced

    if (args[0] == "--licence-online")
    {
        // real Gumroad API with a fake key: must answer "does not exist" (checks HTTPS + POST + JSON on this OS)
        const auto r = LicenseManager::gumroadVerify ("00000000-00000000-00000000-00000000", false);
        std::cout << "Gumroad reached: " << (r.reached ? "yes" : "no") << ", success: " << (r.success ? "yes" : "no") << ", message: " << r.message << "\n";
        return r.reached && ! r.success ? 0 : 1;
    }

    if (args[0] == "--cpu")
    {
        // real-time cost of the whole processor (DSP + analyzers + live meters), worst case: every module on
        const bool one = args[1].isNotEmpty();
        const double seconds = one ? 4.0 : 20.0;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int blk : { 64, 512 })
            {
                if (one && (rate != args[1].getDoubleValue() || blk != 512)) continue;
                WhaleAudioProcessor p;
                p.setRateAndBufferSizeDetails (rate, blk);
                p.prepareToPlay (rate, blk);
                setParam (p, PID::style, (float) styleTechHouse);
                for (int k = 0; k < 4; ++k) { setParam (p, PID::specGain[k], 2.0f); setParam (p, PID::specDyn[k], 0.6f); }
                setParam (p, PID::compGlue, 0.5f); setParam (p, PID::compPunch, 0.6f); setParam (p, PID::satDrive, 0.5f);
                setParam (p, PID::clipAmt, 0.6f); setParam (p, PID::widthMono, 140.0f); setParam (p, PID::toneLow, 2.0f);
                setParam (p, PID::toneAir, 2.0f); setParam (p, PID::lowCut, 30.0f);
                MixGen gen; gen.gainDb = -14.0f;
                juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer midi;
                const int nBlocks = (int) (seconds * rate / blk);
                std::vector<juce::AudioBuffer<float>> pre;   // generate first: only the plug-in is timed
                double audioT = 0, msgT = 0, acc = 0;
                for (int k = 0; k < nBlocks; ++k)
                {
                    gen.fill (b);
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    p.processBlock (b, midi);
                    audioT += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                    acc += blk / rate;
                    if (acc >= 1.0 / 30.0)
                    {
                        acc = 0;
                        const auto t1 = juce::Time::getHighResolutionTicks();
                        TestAccess::pump (p);
                        msgT += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t1);
                    }
                }
                std::cout << "  " << rate / 1000.0 << " kHz, block " << blk << ": audio thread " << juce::String (100.0 * audioT / seconds, 1)
                          << " % of one core, message thread " << juce::String (100.0 * msgT / seconds, 1) << " %\n";
            }
        return 0;
    }
    if (args[0] == "--promo") { runPromo (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), args[2]); return failures; }
    if (args[0] == "--snap") { runSnapshots (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), args[2]); return failures; }
    if (args[0] == "--render") { renderFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), juce::File::getCurrentWorkingDirectory().getChildFile (args[2]), args[3].isEmpty() ? 0 : args[3].getIntValue()); return failures; }

    testTransparency();
    testCeiling();
    testLoudnessLoopAndAB();
    testIndicators();
    testSpectralCapture();
    testAutoProfiles();
    testLowCut();
    testAutoEndToEnd();
    testUndoAuto();
    testPresetLayering();
    testPresetsAndState();
    testLicense();
    std::cout << (failures == 0 ? "\nALL TESTS PASSED\n" : "\nFAILURES: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
