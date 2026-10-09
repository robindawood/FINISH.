#include "VariantGenerator.h"

namespace
{
    using An = TargetSpec::Anchor;

    struct Def
    {
        const char* name; const char* desc; const char* tag;
        An anchor; double seconds; int targetOffset;   // 0 = this section, -1 = previous one
        SectionMacros from, to;
    };

    Variant make (char letter, const Def& d, int section, int sectionCount)
    {
        Variant v;
        v.letter = letter;
        v.name = d.name; v.description = d.desc; v.tag = d.tag;
        int target = section + d.targetOffset;
        auto anchor = d.anchor;
        if (target < 0 || target >= sectionCount) { target = section; anchor = An::Head; }
        v.target = { target, anchor, d.seconds };
        v.from = d.from; v.to = d.to;
        return v;
    }

    // Static treatment helper: same offsets across the whole region.
    constexpr SectionMacros M (float impact, float punch, float width, float warmth, float loud, float trim)
    {
        return { impact, punch, width, warmth, loud, trim };
    }
}

std::vector<Variant> VariantGenerator::forSection (int s, const MusicState& m, const std::vector<SectionDNA>& dna, int generation)
{
    std::vector<Variant> out;
    if (! juce::isPositiveAndBelow (s, (int) m.sections.size())) return out;
    const auto type = m.sections[(size_t) s].type;
    const auto role = s < (int) dna.size() ? dna[(size_t) s].role : SectionRole::Whole;
    const bool alt = (generation % 2) == 1;
    const SectionMacros none {};

    std::vector<Def> defs;
    switch (type)
    {
        case SectionType::Intro:
            defs = ! alt ? std::vector<Def> {
                { "Atmosphere", "More space and depth to set the scene.", "space", An::Whole, 0, 0, M (0, 0, 0.15f, 0.08f, 0, 0), M (0, 0, 0.15f, 0.08f, 0, 0) },
                { "Focus", "Tighter and clearer, the groove shows from the first bar.", "punch", An::Whole, 0, 0, M (0, 0.2f, 0, -0.06f, 0, 0), M (0, 0.2f, 0, -0.06f, 0, 0) },
                { "Night Air", "Brighter top and a touch quieter: a cool, open start.", "air", An::Whole, 0, 0, M (0, 0, 0.1f, -0.12f, 0, -0.5f), M (0, 0, 0.1f, -0.12f, 0, -0.5f) } }
            : std::vector<Def> {
                { "Soft Start", "Begins lower and grows into the section.", "minimal", An::Whole, 0, 0, M (0, 0, 0, 0, 0, -2.0f), none },
                { "Dark Veil", "Warmer and slightly narrower: mystery before the song opens.", "warm", An::Whole, 0, 0, M (0, 0, -0.05f, 0.15f, 0, 0), M (0, 0, -0.05f, 0.15f, 0, 0) },
                { "Clean", "A little more definition on the first elements.", "punch", An::Whole, 0, 0, M (0.1f, 0.1f, 0, 0, 0, 0), M (0.1f, 0.1f, 0, 0, 0, 0) } };
            break;

        case SectionType::Build:
            defs = ! alt ? std::vector<Def> {
                { "Rising Energy", "Controlled progression: density and level grow towards the end.", "rise", An::Whole, 0, 0, none, M (0.3f, 0, 0.1f, 0, 0.25f, 0) },
                { "Tension", "Spectral and stereo contrast: brighter and narrower as it climbs.", "tension", An::Whole, 0, 0, none, M (0, 0, -0.15f, -0.22f, 0, 0) },
                { "Impact Setup", "Pulls weight and width out of the last seconds so the next hit lands.", "setup", An::Tail, 6.0, 0, none, M (0, 0, -0.2f, -0.25f, 0, -2.0f) } }
            : std::vector<Def> {
                { "Lift", "Sharper attacks and a brighter top as it rises.", "rise", An::Whole, 0, 0, none, M (0, 0.3f, 0, -0.1f, 0, 0) },
                { "Squeeze", "Denser and narrower: pressure before the release.", "tension", An::Whole, 0, 0, none, M (0.45f, 0, -0.1f, 0, 0, 0) },
                { "Hollow Out", "The last seconds empty out: the drop fills the space.", "setup", An::Tail, 3.0, 0, M (0, 0, 0, -0.2f, 0, -3.0f), M (0, 0, 0, -0.2f, 0, -3.0f) } };
            break;

        case SectionType::Drop:
        case SectionType::Chorus:
        case SectionType::Excerpt:
            if (role == SectionRole::NewPeak)
                defs = ! alt ? std::vector<Def> {
                    { "Bigger", "More perceived impact than the first peak.", "bigger", An::Whole, 0, 0, M (0.3f, 0.25f, 0, 0, 0.3f, 0.5f), M (0.3f, 0.25f, 0, 0, 0.3f, 0.5f) },
                    { "Evolved", "A different texture: wider and brighter than the first peak.", "evolve", An::Whole, 0, 0, M (0, 0, 0.22f, -0.1f, 0, 0), M (0, 0, 0.22f, -0.1f, 0, 0) },
                    { "Contrast", "Works the end of the previous section so this return lands harder.", "contrast", An::Tail, 6.0, -1, none, M (0, 0, -0.2f, -0.2f, 0, -2.5f) } }
                : std::vector<Def> {
                    { "Heavier", "More weight and glue than the first peak.", "bigger", An::Whole, 0, 0, M (0.4f, 0, 0, 0.12f, 0.2f, 0), M (0.4f, 0, 0, 0.12f, 0.2f, 0) },
                    { "Brighter", "Sharper and more open: same energy, new colour.", "evolve", An::Whole, 0, 0, M (0, 0.2f, 0, -0.18f, 0, 0), M (0, 0.2f, 0, -0.18f, 0, 0) },
                    { "Gap", "A near-silent half second before the return.", "contrast", An::Tail, 0.5, -1, M (0, 0, 0, 0, 0, -10.0f), M (0, 0, 0, 0, 0, -10.0f) } };
            else
                defs = ! alt ? std::vector<Def> {
                    { "Punch", "Stronger attack and impact.", "punch", An::Whole, 0, 0, M (0.15f, 0.5f, 0, 0, 0, 0), M (0.15f, 0.5f, 0, 0, 0, 0) },
                    { "Wide", "More space and depth, bass kept mono.", "width", An::Whole, 0, 0, M (0, 0.1f, 0.2f, 0, 0, 0), M (0, 0.1f, 0.2f, 0, 0, 0) },
                    { "Massive", "Denser sound with a controlled low end.", "mass", An::Whole, 0, 0, M (0.4f, 0, 0, 0.06f, 0.4f, 0), M (0.4f, 0, 0, 0.06f, 0.4f, 0) } }
                : std::vector<Def> {
                    { "Glue", "Tighter, more cohesive, attacks preserved.", "punch", An::Whole, 0, 0, M (0.35f, 0.15f, 0, 0, 0, 0), M (0.35f, 0.15f, 0, 0, 0, 0) },
                    { "Air", "Brighter and wider: lift instead of weight.", "width", An::Whole, 0, 0, M (0, 0, 0.12f, -0.12f, 0, 0), M (0, 0, 0.12f, -0.12f, 0, 0) },
                    { "Weight", "Warmer and heavier low end.", "mass", An::Whole, 0, 0, M (0.2f, 0, 0, 0.15f, 0.25f, 0), M (0.2f, 0, 0, 0.15f, 0.25f, 0) } };
            break;

        case SectionType::Break:
        case SectionType::Bridge:
            defs = ! alt ? std::vector<Def> {
                { "Atmospheric", "More space and depth.", "space", An::Whole, 0, 0, M (0, 0, 0.2f, 0.1f, 0, -1.0f), M (0, 0, 0.2f, 0.1f, 0, -1.0f) },
                { "Tension", "Gradual progression towards the next section.", "tension", An::Whole, 0, 0, none, M (0.25f, 0, 0, -0.15f, 0.15f, 0) },
                { "Minimal", "Less density to create a real breath.", "minimal", An::Whole, 0, 0, M (0, 0, -0.1f, -0.1f, 0, -2.5f), M (0, 0, -0.1f, -0.1f, 0, -2.5f) } }
            : std::vector<Def> {
                { "Deep Space", "Wider, warmer, further away.", "space", An::Whole, 0, 0, M (0, 0, 0.3f, 0.15f, 0, -1.5f), M (0, 0, 0.3f, 0.15f, 0, -1.5f) },
                { "Slow Rise", "Starts low and climbs back to full level.", "tension", An::Whole, 0, 0, M (0, 0, 0, 0, 0, -2.0f), M (0.2f, 0, 0, 0, 0, 0) },
                { "Hush", "The first bars pulled right back.", "minimal", An::Head, 8.0, 0, M (0, 0, 0, 0, 0, -3.5f), M (0, 0, 0, 0, 0, -3.5f) } };
            break;

        case SectionType::Verse:
            defs = ! alt ? std::vector<Def> {
                { "Clarity", "Cleaner attacks, slightly brighter.", "punch", An::Whole, 0, 0, M (0, 0.25f, 0, -0.05f, 0, 0), M (0, 0.25f, 0, -0.05f, 0, 0) },
                { "Space", "A bit more width around the centre.", "width", An::Whole, 0, 0, M (0, 0, 0.15f, 0, 0, 0), M (0, 0, 0.15f, 0, 0, 0) },
                { "Warmth", "Rounder and more cohesive.", "warm", An::Whole, 0, 0, M (0.1f, 0, 0, 0.12f, 0, 0), M (0.1f, 0, 0, 0.12f, 0, 0) } }
            : std::vector<Def> {
                { "Groove", "Glue and attack for a tighter pocket.", "punch", An::Whole, 0, 0, M (0.15f, 0.2f, 0, 0, 0, 0), M (0.15f, 0.2f, 0, 0, 0, 0) },
                { "Depth", "Slightly wider and warmer.", "width", An::Whole, 0, 0, M (0, 0, 0.1f, 0.08f, 0, 0), M (0, 0, 0.1f, 0.08f, 0, 0) },
                { "Lean", "A touch quieter so the next section feels bigger.", "minimal", An::Whole, 0, 0, M (0.1f, 0, 0, 0, 0, -1.0f), M (0.1f, 0, 0, 0, 0, -1.0f) } };
            break;

        case SectionType::Outro:
            defs = ! alt ? std::vector<Def> {
                { "Fade Release", "Energy decreases gradually to a natural end.", "fade", An::Whole, 0, 0, none, M (0, 0, -0.1f, 0, 0, -4.0f) },
                { "Wide Tail", "The ending opens up and glows.", "space", An::Whole, 0, 0, M (0, 0, 0.2f, 0.1f, 0, 0), M (0, 0, 0.2f, 0.1f, 0, 0) },
                { "Keep Energy", "Holds the momentum to the last bar.", "bigger", An::Whole, 0, 0, M (0.15f, 0, 0, 0, 0, 0), M (0.15f, 0, 0, 0, 0, 0) } }
            : std::vector<Def> {
                { "Long Fade", "A deeper fade towards the end.", "fade", An::Whole, 0, 0, none, M (0, 0, 0, 0, 0, -6.0f) },
                { "Dark Tail", "Warmer and softer towards the end.", "space", An::Whole, 0, 0, none, M (0, 0, 0, 0.25f, 0, -1.0f) },
                { "Clean End", "Keeps definition on the last hits.", "punch", An::Whole, 0, 0, M (0, 0.2f, 0, 0, 0, 0), M (0, 0.2f, 0, 0, 0, 0) } };
            break;
    }

    const char letters[] = { 'A', 'B', 'C' };
    for (size_t i = 0; i < defs.size() && i < 3; ++i)
        out.push_back (make (letters[i], defs[i], s, (int) m.sections.size()));
    return out;
}

// ======================================================================================

void VariantRanker::rank (std::vector<Variant>& vs, const Wants& wants, const IntentVector& in, const Risks& risks,
                          const std::vector<SectionDNA>& dna, bool preferKeep)
{
    float best = -1.0f; int bestIndex = -1;
    for (size_t i = 0; i < vs.size(); ++i)
    {
        auto& v = vs[i];
        const auto d = SectionMacros::lerp (v.from, v.to, 0.5f);
        float score = 0.35f;
        juce::String why;

        if (auto it = wants.find (v.tag); it != wants.end())
        {
            score += it->second;
            if (it->second >= 0.25f) why = "Addresses what this part needs.";
        }

        // Intent alignment (-1..1).
        float align = in.punch * d.punch * 2.0f + in.impact * d.impact * 1.5f + in.width * d.width * 4.0f
                    + (in.warmth - in.brightness) * d.warmth * 4.0f + in.loudness * (d.loudness + d.trimDb * 0.1f) * 1.5f
                    + in.energy * (d.impact + d.loudness) + in.bass * d.warmth * 2.0f
                    - in.dynamics * (d.impact + d.loudness);
        align = juce::jlimit (-1.0f, 1.0f, align);
        score += 0.25f * align;
        if (align > 0.2f) why = "Matches your intent.";
        else if (align < -0.2f) why = "Goes against your intent.";

        const int target = v.target.section;
        const float density = juce::isPositiveAndBelow (target, (int) dna.size()) ? dna[(size_t) target].density : 0.0f;
        const float plr = juce::isPositiveAndBelow (target, (int) dna.size()) ? dna[(size_t) target].features.plr : 12.0f;

        v.refused = false;
        if (d.width > 0.05f && risks.stereoRisk)
        {
            v.refused = true;
            v.reason = "Declined: the mix already has mono-compatibility problems. Widening would make them worse.";
        }
        else if ((d.loudness > 0.15f || d.impact > 0.3f) && (risks.squashed || density > 0.75f))
        {
            v.refused = true;
            v.reason = "Declined: this part is already squashed (PLR " + juce::String (plr, 1) + " dB). More density would flatten it.";
        }
        else
        {
            if (d.impact > 0.25f && density > 0.6f) score -= 0.15f;
            v.reason = why;
        }

        v.score = juce::jlimit (0.05f, 0.97f, score);
        v.recommended = false;
        if (! v.refused && v.score > best) { best = v.score; bestIndex = (int) i; }
    }
    // When the part already works, FINISH recommends keeping it unless something is clearly better.
    const float threshold = preferKeep ? 0.62f : 0.42f;
    if (bestIndex >= 0 && best >= threshold)
    {
        vs[(size_t) bestIndex].recommended = true;
        if (vs[(size_t) bestIndex].reason.isEmpty()) vs[(size_t) bestIndex].reason = "Best fit for this part.";
    }
}
