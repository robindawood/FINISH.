#include "SectionDiagnosisEngine.h"

namespace
{
    juce::String db (float v, bool sign = false) { return (sign && v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    const juce::String dash = juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 "));
}

std::vector<SectionVerdict> SectionDiagnosisEngine::analyze (const MusicState& m, const std::vector<SectionDNA>& dna,
                                                             const std::vector<Transition>& transitions)
{
    std::vector<SectionVerdict> out;
    const auto& S = m.sections;
    if (S.size() != dna.size()) return out;

    float maxLevel = -100, peakSum = 0; int peakCount = 0, firstPeak = -1;
    float punchSum = 0;
    for (size_t i = 0; i < S.size(); ++i)
    {
        maxLevel = std::max (maxLevel, S[i].loudness);
        punchSum += dna[i].features.punchIndex;
        if (isPeakType (S[i].type)) { peakSum += S[i].loudness; ++peakCount; if (firstPeak < 0) firstPeak = (int) i; }
    }
    const float peakMean = peakCount > 0 ? peakSum / (float) peakCount : maxLevel;
    const float punchMean = punchSum / (float) std::max<size_t> (1, S.size());

    auto transitionInto = [&] (size_t i) -> const Transition* { return i > 0 && i - 1 < transitions.size() ? &transitions[i - 1] : nullptr; };
    auto transitionOut  = [&] (size_t i) -> const Transition* { return i < transitions.size() ? &transitions[i] : nullptr; };

    for (size_t i = 0; i < S.size(); ++i)
    {
        const auto& s = S[i];
        const auto& d = dna[i];
        SectionVerdict v;
        const juce::String name = s.label;

        switch (s.type)
        {
            case SectionType::Intro:
            {
                const auto* t = transitionOut (i);
                if (t != nullptr && t->kind == Transition::Kind::IntroDrop)
                {
                    v.potential = 0.45f;
                    v.headline = name + dash + "Louder than what follows.";
                    v.explanation = "The intro sits above the next section, so the song starts by losing energy.";
                    v.recommendation = "Soften the intro or lift the next section's first bars.";
                    v.transitionRef = (int) i;
                    v.wants = { { "minimal", 0.3f } };
                }
                else
                {
                    v.potential = 0.1f;
                    v.headline = name + dash + "Sets the mood.";
                    v.explanation = "Energy sits " + db (maxLevel - s.loudness) + " under the peak of the track: room to grow.";
                    v.recommendation = "Nothing to fix. Keep it, or explore a colour variant.";
                    v.wants = { { "space", 0.15f } };
                }
                break;
            }
            case SectionType::Build:
            {
                const auto* t = transitionOut (i);
                if (t != nullptr && t->kind == Transition::Kind::IntoPeak && ! t->works)
                {
                    v.potential = 0.65f;
                    v.headline = name + dash + "Too dense before the drop.";
                    v.explanation = "The build already reaches the next section's energy (" + db (t->contrastDb, true)
                                  + " across the boundary). The problem isn't the drop, it's what leads into it.";
                    v.recommendation = "Work on the " + t->title + " transition.";
                    v.transitionRef = (int) i;
                    v.wants = { { "setup", 0.45f }, { "tension", 0.25f } };
                }
                else if (d.slopeDb < 1.5f)
                {
                    v.potential = 0.55f;
                    v.headline = name + dash + "Doesn't build.";
                    v.explanation = "Energy changes only " + db (d.slopeDb, true) + " from start to end: little anticipation is created.";
                    v.recommendation = "Compare Rising Energy and Tension.";
                    v.wants = { { "rise", 0.4f }, { "tension", 0.3f } };
                }
                else
                {
                    v.potential = 0.15f;
                    v.headline = name + dash + "Does its job.";
                    v.explanation = "Energy rises " + db (d.slopeDb, true) + " towards the next section.";
                    v.recommendation = "Keep it.";
                    v.wants = { { "rise", 0.1f } };
                }
                break;
            }
            case SectionType::Drop:
            case SectionType::Chorus:
            {
                if ((int) i != firstPeak && firstPeak >= 0)
                {
                    const auto& f = dna[(size_t) firstPeak];
                    const float dL = s.loudness - S[(size_t) firstPeak].loudness;
                    const bool similar = std::abs (dL) < 0.8f
                                      && std::abs (d.features.sideRatio - f.features.sideRatio) < 0.03f
                                      && std::abs (d.features.lowRatio - f.features.lowRatio) < 0.05f;
                    if (dL < -1.5f)
                    {
                        v.potential = 0.6f;
                        v.headline = name + dash + "Smaller than " + S[(size_t) firstPeak].label + ".";
                        v.explanation = "It lands " + db (-dL) + " under the first peak. The return feels like a step down.";
                        v.recommendation = "Compare Bigger and Contrast.";
                        v.wants = { { "bigger", 0.45f }, { "contrast", 0.25f } };
                    }
                    else if (similar)
                    {
                        v.potential = 0.5f;
                        v.headline = name + dash + "Already powerful.";
                        v.explanation = "Impact is comparable to " + S[(size_t) firstPeak].label + " (" + db (dL, true)
                                      + "). Adding more density risks reducing contrast. It may feel like a simple repeat.";
                        const auto* t = transitionInto (i);
                        v.recommendation = t != nullptr ? "Work on the " + t->title + " transition rather than the drop itself."
                                                        : juce::String ("Try Evolved for a change of texture.");
                        v.transitionRef = t != nullptr ? (int) i - 1 : -1;
                        v.wants = { { "evolve", 0.35f }, { "contrast", 0.35f } };
                    }
                    else
                    {
                        v.potential = 0.15f;
                        v.headline = name + dash + "New peak.";
                        v.explanation = "It differs clearly from " + S[(size_t) firstPeak].label + " (" + db (dL, true) + ").";
                        v.recommendation = "Keep it.";
                        v.wants = { { "bigger", 0.1f } };
                    }
                }
                else
                {
                    if (s.loudness < maxLevel - 1.5f)
                    {
                        v.potential = 0.5f;
                        v.headline = name + dash + "Not the peak of the track.";
                        v.explanation = "Other parts of the song are " + db (maxLevel - s.loudness) + " louder than this peak.";
                        v.recommendation = "Compare Massive and Punch.";
                        v.wants = { { "mass", 0.4f }, { "punch", 0.2f } };
                    }
                    else if (d.features.punchIndex < punchMean - 1.0f)
                    {
                        v.potential = 0.45f;
                        v.headline = name + dash + "Soft attack.";
                        v.explanation = "Transients are weaker here than in the rest of the track.";
                        v.recommendation = "Compare Punch and Massive.";
                        v.wants = { { "punch", 0.4f } };
                    }
                    else
                    {
                        v.potential = 0.15f;
                        v.headline = name + dash + "Already powerful.";
                        v.explanation = "The first peak is the loudest moment of the track and its attacks are as strong as anywhere else.";
                        v.recommendation = "Keep it, or compare variants for colour.";
                        v.wants = {};
                    }
                    if (d.features.valid && d.features.sideRatio < 0.04f) v.wants["width"] += 0.3f;
                }
                break;
            }
            case SectionType::Break:
            case SectionType::Bridge:
            {
                const float under = peakMean - s.loudness;
                if (under < 3.0f && peakCount > 0)
                {
                    v.potential = 0.7f;
                    v.headline = name + dash + "High improvement potential.";
                    v.explanation = (under <= 0.0f ? "Dense: " + db (-under) + " louder than the peaks."
                                                   : "Dense: only " + db (under) + " under the peaks.")
                                  + " It doesn't give enough breath before the return.";
                    v.recommendation = "Compare an atmospheric and a minimal variant.";
                    v.wants = { { "space", 0.35f }, { "minimal", 0.35f } };
                }
                else
                {
                    v.potential = 0.15f;
                    v.headline = name + dash + "Breathes.";
                    v.explanation = "It sits " + db (under) + " under the peaks: a real release.";
                    v.recommendation = "Keep it. For more pull towards the next peak, try Tension.";
                    v.wants = { { "tension", 0.1f } };
                }
                break;
            }
            case SectionType::Verse:
            {
                v.potential = 0.2f;
                v.headline = name + dash + "Carries the groove.";
                v.explanation = "Mid-energy section, " + db (maxLevel - s.loudness) + " under the peak.";
                v.recommendation = "Keep it, or add clarity.";
                v.wants = { { "punch", 0.1f } };
                break;
            }
            case SectionType::Outro:
            {
                if (d.slopeDb >= 0.0f && s.length() > 8.0)
                {
                    v.potential = 0.4f;
                    v.headline = name + dash + "Doesn't conclude.";
                    v.explanation = "Energy holds (" + db (d.slopeDb, true) + ") instead of winding down.";
                    v.recommendation = "Try Fade Release.";
                    v.wants = { { "fade", 0.4f } };
                }
                else
                {
                    v.potential = 0.1f;
                    v.headline = name + dash + "Concludes naturally.";
                    v.explanation = "Energy decays " + db (-d.slopeDb) + " across the outro.";
                    v.recommendation = "Keep it.";
                }
                break;
            }
            case SectionType::Excerpt:
            {
                v.potential = 0.35f;
                v.headline = "EXCERPT" + dash + "Analysed as one block.";
                v.explanation = "Play at least 30 s (ideally the whole song) to get sections and transitions.";
                v.recommendation = "Compare Punch, Wide and Massive.";
                v.wants = { { "punch", 0.15f } };
                break;
            }
        }

        // Low confidence lowers how strongly FINISH insists.
        if (s.isUncertain()) v.potential *= 0.8f;
        out.push_back (v);
    }
    return out;
}
