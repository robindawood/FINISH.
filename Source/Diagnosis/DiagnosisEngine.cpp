#include "DiagnosisEngine.h"
#include <algorithm>

namespace
{
    juce::String fmt (float v, int decimals = 1) { return decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals); }
    float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }
    float ratioDb (float a, float b) { return 10.0f * std::log10 (std::max (a, 1.0e-6f) / std::max (b, 1.0e-6f)); }
}

std::vector<Diagnosis> DiagnosisEngine::analyze (const AudioFeatures& f, const MusicState& m, const ReferenceProfile& ref) const
{
    std::vector<Diagnosis> d;
    if (! f.valid) return d;

    const bool useRef = ref.valid && ref.features.valid;
    const auto& r = ref.features;
    // Short excerpts give less reliable answers.
    const float durConf = juce::jlimit (0.5f, 1.0f, f.durationSec / 30.0f);
    auto add = [&] (DiagnosisType t, float sev, float conf, juce::String title, juce::String text)
    {
        d.push_back ({ t, clamp01 (sev), clamp01 (conf * durConf), std::move (title), std::move (text) });
    };

    // --- Level / dynamics --------------------------------------------------------------
    if (f.peakDb > -0.1f)
        add (DiagnosisType::Clipping, 0.85f, 0.9f, "NO HEADROOM",
             "Peaks reach " + fmt (f.peakDb) + " dBFS: the mix is probably clipping before FINISH. "
             "Pull the master fader (or the loudest bus) down 2-3 dB.");

    if (useRef && f.plr < r.plr - 2.0f)
        add (DiagnosisType::OverCompressed, (r.plr - f.plr) / 5.0f, 0.85f, "MORE SQUASHED THAN REFERENCE",
             "Peak-to-loudness ratio is " + fmt (f.plr) + " dB vs " + fmt (r.plr) + " dB on the reference. "
             "Adding loudness would flatten it further.");
    else if (! useRef && f.plr < 8.0f)
        add (DiagnosisType::OverCompressed, (8.0f - f.plr) / 5.0f, 0.85f, "LOW DYNAMIC CONTRAST",
             "Peak-to-loudness ratio is only " + fmt (f.plr) + " dB: the mix is already dense. "
             "FINISH will not push loudness here; impact has to come from transients.");

    if (useRef)
    {
        const float diff = r.integratedLufs - f.integratedLufs;
        if (diff > 2.0f)
            add (DiagnosisType::TooQuiet, diff / 8.0f, 0.85f, "QUIETER THAN REFERENCE",
                 fmt (diff) + " LU quieter than \"" + ref.name + "\" (" + fmt (f.integratedLufs) + " vs "
                 + fmt (r.integratedLufs) + " LUFS).");
        else if (diff < -2.0f)
            add (DiagnosisType::LouderThanReference, -diff / 10.0f, 0.75f, "LOUDER THAN REFERENCE",
                 fmt (-diff) + " LU louder than the reference. Extra loudness is costing dynamics.");
    }
    else if (f.integratedLufs < -16.0f)
    {
        add (DiagnosisType::TooQuiet, (-16.0f - f.integratedLufs) / 10.0f, 0.8f, "LOUDNESS OPPORTUNITY",
             "Integrated loudness is " + fmt (f.integratedLufs) + " LUFS, below release levels "
             "(about -14 for streaming, -9 to -7 for club). There is room to add density.");
    }

    // --- Impact ------------------------------------------------------------------------
    {
        const float threshold = useRef ? r.punchIndex - 1.5f : 5.0f;
        if (f.punchIndex < threshold)
            add (DiagnosisType::WeakTransients, (threshold - f.punchIndex) / 4.0f + 0.3f, 0.75f, "SOFT TRANSIENTS",
                 "Attacks rise " + fmt (f.punchIndex) + " dB above the body of the sound"
                 + (useRef ? " (reference: " + fmt (r.punchIndex) + " dB)" : juce::String()) +
                 ". Drums lack definition, so the track feels less impactful than it is loud.");
    }

    // --- Stereo ------------------------------------------------------------------------
    if (f.correlation < 0.2f || f.sideRatio > 0.42f)
        add (DiagnosisType::StereoRisk, 0.8f, 0.9f, "MONO COMPATIBILITY RISK",
             "L/R correlation is " + fmt (f.correlation, 2) + ". Wide or out-of-phase material will "
             "lose level on phones, club systems and mono speakers.");
    else if ((useRef && f.sideRatio < r.sideRatio * 0.6f) || (! useRef && f.sideRatio < 0.04f))
        add (DiagnosisType::Narrow, useRef ? 0.5f : (0.04f - f.sideRatio) * 12.0f + 0.35f, 0.8f, "NARROW IMAGE",
             "Only " + fmt (f.sideRatio * 100.0f) + " % of the energy is in the sides"
             + (useRef ? " (reference: " + fmt (r.sideRatio * 100.0f) + " %)" : juce::String()) +
             ". The mix sits in the middle of the speakers.");

    if (f.lowSideRatio > 0.2f)
        add (DiagnosisType::WideBass, (f.lowSideRatio - 0.2f) * 3.0f + 0.3f, 0.8f, "STEREO BASS",
             fmt (f.lowSideRatio * 100.0f) + " % of the low end is in stereo. Bass below 140 Hz should "
             "be mono to stay solid on large systems.");

    // --- Tonal balance -----------------------------------------------------------------
    if (useRef)
    {
        const float lowDiff = ratioDb (f.lowRatio, r.lowRatio);
        const float highDiff = ratioDb (f.highRatio, r.highRatio);
        if (lowDiff > 2.0f)
            add (DiagnosisType::LowHeavy, lowDiff / 6.0f, 0.75f, "HEAVIER LOW END THAN REFERENCE",
                 "The low end is " + fmt (lowDiff) + " dB stronger than on the reference.");
        else if (lowDiff < -2.0f)
            add (DiagnosisType::LowThin, -lowDiff / 6.0f, 0.75f, "THINNER LOW END THAN REFERENCE",
                 "The low end is " + fmt (-lowDiff) + " dB weaker than on the reference.");
        if (highDiff > 2.5f)
            add (DiagnosisType::Harsh, highDiff / 7.0f, 0.7f, "BRIGHTER THAN REFERENCE",
                 "The top end is " + fmt (highDiff) + " dB hotter than on the reference.");
        else if (highDiff < -2.5f)
            add (DiagnosisType::Dull, -highDiff / 7.0f, 0.7f, "DULLER THAN REFERENCE",
                 "The top end is " + fmt (-highDiff) + " dB lower than on the reference.");
    }
    else
    {
        if (f.lowRatio > 0.72f)
            add (DiagnosisType::LowHeavy, (f.lowRatio - 0.72f) * 3.0f + 0.3f, 0.65f, "LOW-END DOMINANCE",
                 fmt (f.lowRatio * 100.0f, 0) + " % of the energy sits below 150 Hz. The low end masks the rest of the mix.");
        else if (f.lowRatio < 0.22f)
            add (DiagnosisType::LowThin, (0.22f - f.lowRatio) * 3.0f + 0.3f, 0.6f, "THIN LOW END",
                 "Only " + fmt (f.lowRatio * 100.0f, 0) + " % of the energy is below 150 Hz. The track may lack weight.");
        if (f.highRatio > 0.15f)
            add (DiagnosisType::Harsh, (f.highRatio - 0.15f) * 4.0f + 0.3f, 0.6f, "HARSH TOP END",
                 fmt (f.highRatio * 100.0f, 0) + " % of the energy is above 4 kHz. Long listening will be tiring.");
        else if (f.highRatio < 0.012f)
            add (DiagnosisType::Dull, 0.4f, 0.6f, "DULL TOP END",
                 "Very little energy above 4 kHz. The mix may sound closed or muffled.");
    }

    // --- Arrangement -------------------------------------------------------------------
    if (m.durationSec >= 60.0 && m.contrastDb < 2.5f)
        add (DiagnosisType::FlatArrangement, (2.5f - m.contrastDb) / 2.5f * 0.6f + 0.2f, 0.6f, "FLAT ENERGY CURVE",
             "Loud and quiet sections differ by only " + fmt (m.contrastDb) + " dB. "
             "The drop cannot feel bigger than what comes before it.");

    std::stable_sort (d.begin(), d.end(), [] (const Diagnosis& a, const Diagnosis& b)
                      { return a.severity * a.confidence > b.severity * b.confidence; });
    return d;
}

int DiagnosisEngine::finishScore (const std::vector<Diagnosis>& d)
{
    float penalty = 0;
    for (auto& x : d) penalty += x.severity * x.confidence * 25.0f;
    return juce::jlimit (15, 99, (int) std::round (99.0f - penalty));
}
