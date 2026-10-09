#include "DecisionEngine.h"
#include <algorithm>

namespace
{
    float sevOf (const std::vector<Diagnosis>& d, DiagnosisType t)
    {
        for (auto& x : d) if (x.type == t) return x.severity * (0.5f + 0.5f * x.confidence);
        return 0.0f;
    }
    float clampf (float v, float lo, float hi) { return juce::jlimit (lo, hi, v); }
}

DecisionResult DecisionEngine::build (const std::vector<Diagnosis>& d, const IntentVector& in, bool hasAnalysis) const
{
    DecisionResult res;

    const float weak     = sevOf (d, DiagnosisType::WeakTransients);
    const float squashed = sevOf (d, DiagnosisType::OverCompressed);
    const float flat     = sevOf (d, DiagnosisType::FlatArrangement);
    const float quiet    = sevOf (d, DiagnosisType::TooQuiet);
    const float louder   = sevOf (d, DiagnosisType::LouderThanReference);
    const float clip     = sevOf (d, DiagnosisType::Clipping);
    const float narrow   = sevOf (d, DiagnosisType::Narrow);
    const float risk     = sevOf (d, DiagnosisType::StereoRisk);
    const float wideBass = sevOf (d, DiagnosisType::WideBass);
    const float heavy    = sevOf (d, DiagnosisType::LowHeavy);
    const float thin     = sevOf (d, DiagnosisType::LowThin);
    const float harsh    = sevOf (d, DiagnosisType::Harsh);
    const float dull     = sevOf (d, DiagnosisType::Dull);

    // --- Intent conflicts: say no when the request would hurt the track. -----------------
    float bassWish = in.bass, brightWish = in.brightness, loudWish = in.loudness, widthWish = in.width;
    if (loudWish > 0.3f && squashed > 0.2f)
    {
        res.notes.add ("Louder refused: the mix is already dense, more limiting would flatten it. PUNCH adds impact without loudness.");
        loudWish = 0;
    }
    if (widthWish > 0.3f && risk > 0.2f)
    {
        res.notes.add ("Wider refused: the stereo image already has mono-compatibility problems.");
        widthWish = 0;
    }
    if (bassWish > 0.3f && heavy > 0.2f)
    {
        res.notes.add ("More bass refused: the low end already dominates the mix.");
        bassWish = 0;
    }
    if (brightWish > 0.3f && harsh > 0.2f)
    {
        res.notes.add ("Brighter refused: the top end is already harsh.");
        brightWish = 0;
    }

    // Shared tonal correction, applied by every strategy.
    const float warmth = clampf (0.5f + 0.15f * harsh - 0.15f * dull + 0.10f * thin - 0.08f * heavy
                                 + 0.12f * (in.warmth - brightWish) + 0.06f * bassWish, 0.25f, 0.75f);

    auto finalise = [&] (Proposal& p, float score)
    {
        p.confidence = clampf (score * (hasAnalysis ? 1.0f : 0.7f), 0.05f, 0.97f);
        p.targets.warmth = warmth;
        if (! hasAnalysis)
            p.reason = "Based on your intent only. Run FINISH MY TRACK for a precise proposal.";
    };

    // --- PUNCH ----------------------------------------------------------------------------
    {
        Proposal p;
        p.title = "PUNCH";
        p.description = "Sharper transients and contrast. Impact without extra loudness.";
        const float score = 0.35f + weak * 0.5f + squashed * 0.3f + flat * 0.2f
                          + 0.4f * std::max (0.0f, in.punch) + 0.2f * std::max (0.0f, in.impact) + 0.15f * std::max (0.0f, in.dynamics);
        p.targets.punch = clampf (0.45f + 0.35f * weak + 0.15f * in.punch, 0.3f, 0.9f);
        p.targets.impact = squashed > 0.1f ? 0.1f : 0.25f;
        p.targets.loudness = (squashed > 0.1f || clip > 0.1f || louder > 0.1f) ? 0.05f : 0.2f;
        p.targets.width = 0.5f;
        p.reason = weak > 0 ? "Transients are soft; this brings the drums forward."
                 : squashed > 0 ? "The mix is dense; restoring attacks is the safe way to add impact."
                 : "Adds definition and contrast between hits.";
        finalise (p, score);
        res.proposals.push_back (p);
    }

    // --- WIDE -----------------------------------------------------------------------------
    {
        Proposal p;
        p.title = "WIDE";
        p.description = "Bigger stereo image. Bass kept mono so the track stays solid on every system.";
        const float score = 0.33f + narrow * 0.6f + wideBass * 0.2f + 0.45f * std::max (0.0f, widthWish);
        p.targets.width = clampf (0.62f + 0.15f * narrow + 0.08f * widthWish, 0.55f, 0.85f);
        p.targets.punch = 0.15f;
        p.targets.impact = 0.1f;
        p.targets.loudness = (squashed > 0.1f || louder > 0.1f) ? 0.0f : 0.1f;
        p.reason = narrow > 0 ? "The image is narrow; this opens the sides."
                 : wideBass > 0 ? "Also folds the stereo bass back to mono."
                 : "Adds space around the centre.";
        finalise (p, score);
        if (risk > 0.2f)       { p.refused = true; p.reason = "Refused: the image already has mono-compatibility problems. Widening would make them worse."; }
        else if (in.width < -0.3f) { p.refused = true; p.reason = "Refused: you asked for a narrower image. Turn WIDTH below 100 % instead."; }
        res.proposals.push_back (p);
    }

    // --- MASSIVE --------------------------------------------------------------------------
    {
        Proposal p;
        p.title = "MASSIVE";
        p.description = "More density, weight and loudness. Peaks held at -1 dBFS.";
        const float score = 0.32f + quiet * 0.6f + thin * 0.3f
                          + 0.35f * std::max (0.0f, in.energy) + 0.35f * std::max (0.0f, loudWish) + 0.15f * std::max (0.0f, bassWish)
                          - 0.3f * std::max (0.0f, in.dynamics);
        p.targets.impact = clampf (0.45f + 0.25f * quiet, 0.3f, 0.8f);
        p.targets.loudness = clampf (0.4f + 0.45f * quiet + 0.1f * loudWish, 0.3f, 0.9f);
        p.targets.punch = 0.2f;
        p.targets.width = 0.5f;
        p.reason = quiet > 0 ? "There is loudness headroom; this uses it with controlled density."
                 : thin > 0 ? "Adds weight and density to a light mix."
                 : "Adds density and perceived energy.";
        finalise (p, score);
        p.targets.warmth = clampf (p.targets.warmth + 0.06f, 0.25f, 0.8f);
        if (squashed > 0.5f)   { p.refused = true; p.reason = "Refused: the mix is already squashed. More density would remove what is left of the dynamics."; }
        else if (louder > 0.1f){ p.refused = true; p.reason = "Refused: the track is already louder than your reference."; }
        res.proposals.push_back (p);
    }

    std::stable_sort (res.proposals.begin(), res.proposals.end(), [] (const Proposal& a, const Proposal& b)
    {
        if (a.refused != b.refused) return ! a.refused;
        return a.confidence > b.confidence;
    });
    return res;
}
