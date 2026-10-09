#include "TransitionAnalyzer.h"
#include "SectionDetector.h"
#include "SectionFeatureAnalyzer.h"

namespace
{
    juce::String db (float v, bool sign = true)
    {
        return (sign && v >= 0 ? "+" : "") + juce::String (v, 1) + " dB";
    }
}

std::vector<Transition> TransitionAnalyzer::analyze (const std::vector<AnalysisFrame>& grid, const MusicState& m,
                                                     const std::vector<SectionDNA>& dna)
{
    juce::ignoreUnused (dna);
    std::vector<Transition> out;
    const auto& S = m.sections;

    // Typical level of the peaks, to judge how much a break breathes.
    float peakSum = 0; int peakCount = 0;
    for (const auto& s : S)
        if (isPeakType (s.type)) { peakSum += s.loudness; ++peakCount; }

    for (size_t b = 0; b + 1 < S.size(); ++b)
    {
        const auto& A = S[b];
        const auto& B = S[b + 1];
        Transition t;
        t.index = (int) b;
        t.title = A.label + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + B.label;

        const double edgeA = std::min (4.0, A.length() * 0.5), edgeB = std::min (4.0, B.length() * 0.5);
        const float La = SectionDetector::meanLevel (m, A.endTime - edgeA, A.endTime);
        const float Lb = SectionDetector::meanLevel (m, B.startTime, B.startTime + edgeB);
        t.contrastDb = Lb - La;
        const auto fa = SectionFeatureAnalyzer::measure (grid, m, A.endTime - edgeA, A.endTime);
        const auto fb = SectionFeatureAnalyzer::measure (grid, m, B.startTime, B.startTime + edgeB);

        if (isPeakType (B.type) && ! isPeakType (A.type))
        {
            // ---- Into a peak: the arrival needs contrast.
            t.kind = Transition::Kind::IntoPeak;
            if (t.contrastDb >= 3.0f)
            {
                t.works = true;
                t.headline = "Works. Clear lift into " + B.label + ".";
                t.explanation = "The last seconds of " + A.label + " sit " + db (t.contrastDb, false) + " below " + B.label
                              + ". The arrival has room to hit. No change needed.";
            }
            else
            {
                t.works = false;
                t.severity = juce::jlimit (0.2f, 1.0f, (3.0f - t.contrastDb) / 3.0f);
                const bool bassFull = fa.valid && fb.valid && fa.lowRatio >= fb.lowRatio * 0.9f;
                t.headline = "Insufficient contrast.";
                t.explanation = (t.contrastDb <= 0.0f ? A.label + " ends as loud as " + B.label + " or louder (" + db (t.contrastDb) + ")"
                                                      : A.label + " ends only " + db (t.contrastDb, false) + " below " + B.label)
                              + (bassFull ? " and already carries the full low end" : "")
                              + ". " + B.label + " risks sounding less impressive than it is.";
                t.wants = { { "setup", bassFull ? 0.45f : 0.3f }, { "gap", 0.3f }, { "arrival", 0.3f } };
            }
        }
        else if (isPeakType (A.type) && ! isPeakType (B.type) && B.type != SectionType::Outro)
        {
            // ---- Out of a peak: the music should breathe.
            t.kind = Transition::Kind::OutOfPeak;
            const float release = std::max (-t.contrastDb, peakCount > 0 ? peakSum / (float) peakCount - B.loudness : -t.contrastDb);
            if (release >= 4.0f)
            {
                t.works = true;
                t.headline = "Works. The music breathes.";
                t.explanation = B.label + " sits " + db (release, false) + " under the peaks. That release makes the next peak feel like a return.";
            }
            else
            {
                t.works = false;
                t.severity = juce::jlimit (0.2f, 1.0f, (4.0f - release) / 4.0f);
                t.headline = "Not enough release.";
                t.explanation = (release <= 0.0f ? B.label + " is even louder than " + A.label + " (" + db (-release, false) + " more)."
                                                 : B.label + " keeps most of " + A.label + "'s energy (only " + db (release, false) + " less).")
                              + " Without a breath, the next peak can't feel like a return.";
                t.wants = { { "minimal", 0.35f }, { "space", 0.3f }, { "release", 0.25f } };
            }
        }
        else if (B.type == SectionType::Outro)
        {
            t.kind = Transition::Kind::IntoOutro;
            if (t.contrastDb >= -1.0f && B.length() > 8.0)
            {
                t.works = false;
                t.severity = 0.4f;
                t.headline = "The ending doesn't wind down.";
                t.explanation = "OUTRO starts at the same energy as " + A.label + " (" + db (t.contrastDb) + "). The track may stop rather than conclude.";
                t.wants = { { "fade", 0.4f } };
            }
            else
            {
                t.works = true;
                t.headline = "Works. Lands naturally.";
                t.explanation = "Energy steps down " + db (-t.contrastDb, false) + " into the OUTRO.";
            }
        }
        else if (A.type == SectionType::Intro && t.contrastDb < -2.0f)
        {
            t.kind = Transition::Kind::IntroDrop;
            t.works = false;
            t.severity = 0.45f;
            t.headline = "INTRO is louder than what follows.";
            t.explanation = "Energy falls " + db (-t.contrastDb, false) + " after the intro: the track loses momentum early.";
            t.wants = { { "minimal", 0.3f }, { "arrival", 0.3f } };
        }
        else
        {
            t.kind = Transition::Kind::Other;
            t.works = true;
            t.headline = std::abs (t.contrastDb) < 0.5f ? "Works. Seamless." : "Works. Flows naturally.";
            t.explanation = "Energy changes " + db (t.contrastDb) + " across the boundary. Nothing to fix.";
        }

        buildVariants (t, m, 0);
        out.push_back (t);
    }
    return out;
}

void TransitionAnalyzer::buildVariants (Transition& t, const MusicState& m, int generation)
{
    t.variants.clear();
    const auto& S = m.sections;
    if (t.index + 1 >= (int) S.size()) return;
    const auto& A = S[(size_t) t.index];
    const auto& B = S[(size_t) t.index + 1];
    const int a = t.index, b = t.index + 1;
    const bool alt = (generation % 2) == 1;
    using An = TargetSpec::Anchor;

    auto add = [&] (char letter, juce::String name, juce::String desc, juce::String tag, int section,
                    An anchor, double seconds, SectionMacros from, SectionMacros to)
    {
        Variant v;
        v.letter = letter; v.name = std::move (name); v.description = std::move (desc); v.tag = std::move (tag);
        v.target = { section, anchor, seconds };
        v.from = from; v.to = to;
        t.variants.push_back (v);
    };

    switch (t.kind)
    {
        case Transition::Kind::IntoPeak:
            if (! alt)
            {
                add ('A', "Thin out", "Less low end and width in the last seconds before " + B.label + ".", "setup",
                     a, An::Tail, 6.0, {}, { 0, 0, -0.2f, -0.3f, 0, -1.5f });
                add ('B', "Pre-drop dip", "A short dip and narrowing right before the hit: sharper timing.", "gap",
                     a, An::Tail, 0.6, { 0, 0, -0.3f, 0, 0, -4.0f }, { 0, 0, -0.4f, 0, 0, -6.0f });
                add ('C', "Arrival impact", "Extra punch and weight on the first bars of " + B.label + ", easing back.", "arrival",
                     b, An::Head, 8.0, { 0.35f, 0.6f, 0, 0, 0.3f, 0.8f }, { 0.2f, 0.3f, 0, 0, 0.2f, 0.0f });
            }
            else
            {
                add ('A', "Filter rise", "The top end opens while the low end thins: classic tension before the hit.", "setup",
                     a, An::Tail, 8.0, {}, { 0, 0, -0.1f, -0.4f, 0, -0.5f });
                add ('B', "Silence beat", "A near-silent quarter second right before " + B.label + ".", "gap",
                     a, An::Tail, 0.25, { 0, 0, 0, 0, 0, -18.0f }, { 0, 0, 0, 0, 0, -18.0f });
                add ('C', "Wider arrival", "The first bars of " + B.label + " open up in width and attack.", "arrival",
                     b, An::Head, 8.0, { 0, 0.3f, 0.2f, 0, 0, 0 }, { 0, 0.15f, 0.1f, 0, 0, 0 });
            }
            break;

        case Transition::Kind::OutOfPeak:
            if (! alt)
            {
                add ('A', "Atmospheric", "More space and depth, less density in " + B.label + ".", "space",
                     b, An::Whole, 0, { 0, 0, 0.2f, 0.1f, 0, -1.0f }, { 0, 0, 0.2f, 0.1f, 0, -1.0f });
                add ('B', "Release", "Let the end of " + A.label + " ease off so the change feels intentional.", "release",
                     a, An::Tail, 4.0, {}, { 0, 0, -0.1f, 0, 0, -2.0f });
                add ('C', "Minimal start", "Pull the first bars of " + B.label + " back for a real breath.", "minimal",
                     b, An::Head, 8.0, { 0, 0, -0.1f, -0.1f, 0, -3.0f }, { 0, 0, -0.1f, -0.1f, 0, -3.0f });
            }
            else
            {
                add ('A', "Deep space", "Wider, warmer and further away.", "space",
                     b, An::Whole, 0, { 0, 0, 0.3f, 0.15f, 0, -1.5f }, { 0, 0, 0.3f, 0.15f, 0, -1.5f });
                add ('B', "Tail fade", "A longer fade at the end of " + A.label + ".", "release",
                     a, An::Tail, 6.0, {}, { 0, 0, 0, 0, 0, -3.0f });
                add ('C', "Hush", "The whole section a little quieter and calmer.", "minimal",
                     b, An::Whole, 0, { 0, 0, 0, 0, 0, -2.5f }, { 0, 0, 0, 0, 0, -2.5f });
            }
            break;

        case Transition::Kind::IntoOutro:
            if (! alt)
            {
                add ('A', "Fade release", "A gradual decrease across the outro.", "fade",
                     b, An::Whole, 0, {}, { 0, 0, -0.1f, 0, 0, -4.0f });
                add ('B', "Wide tail", "Let the ending open up and glow.", "space",
                     b, An::Whole, 0, { 0, 0, 0.2f, 0.1f, 0, 0 }, { 0, 0, 0.2f, 0.1f, 0, 0 });
                add ('C', "Soften the end", "Ease the last seconds before the outro.", "release",
                     a, An::Tail, 6.0, {}, { 0, 0, 0, 0, 0, -1.5f });
            }
            else
            {
                add ('A', "Long fade", "A deeper fade towards the end.", "fade",
                     b, An::Whole, 0, {}, { 0, 0, 0, 0, 0, -6.0f });
                add ('B', "Dark tail", "The ending gets warmer and softer.", "space",
                     b, An::Whole, 0, {}, { 0, 0, 0, 0.25f, 0, -1.0f });
                add ('C', "Clean end", "Keeps definition on the last hits.", "arrival",
                     b, An::Head, 6.0, { 0, 0.2f, 0, 0, 0, 0 }, { 0, 0.2f, 0, 0, 0, 0 });
            }
            break;

        case Transition::Kind::IntroDrop:
            add ('A', "Soften", "Lower the intro so the song grows from it.", "minimal",
                 a, An::Whole, 0, { 0, 0, 0, 0, 0, -2.0f }, { 0, 0, 0, 0, 0, -2.0f });
            add ('B', "Lift", "Push the first bars of " + B.label + ".", "arrival",
                 b, An::Head, 6.0, { 0.2f, 0.2f, 0, 0, 0, 1.0f }, { 0.1f, 0.1f, 0, 0, 0, 0.5f });
            add ('C', "Narrow", "A narrower intro makes the next section feel wider.", "width",
                 a, An::Whole, 0, { 0, 0, -0.15f, 0, 0, 0 }, { 0, 0, -0.15f, 0, 0, 0 });
            break;

        case Transition::Kind::Other:
            if (! alt)
            {
                add ('A', "Lift the tail", "Slight rise of energy at the end of " + A.label + ".", "rise",
                     a, An::Tail, 4.0, {}, { 0.2f, 0.1f, 0, 0, 0.1f, 0 });
                add ('B', "Accent", "A brief dip that marks the change.", "gap",
                     a, An::Tail, 0.5, { 0, 0, 0, 0, 0, -3.0f }, { 0, 0, 0, 0, 0, -3.0f });
                add ('C', "Push the start", "More attack on the first bars of " + B.label + ".", "arrival",
                     b, An::Head, 4.0, { 0, 0.3f, 0, 0, 0, 0 }, { 0, 0.15f, 0, 0, 0, 0 });
            }
            else
            {
                add ('A', "Squeeze", "Narrower and denser at the end of " + A.label + ".", "rise",
                     a, An::Tail, 4.0, {}, { 0.2f, 0, -0.15f, 0, 0, 0 });
                add ('B', "Silence beat", "A near-silent quarter second at the boundary.", "gap",
                     a, An::Tail, 0.25, { 0, 0, 0, 0, 0, -18.0f }, { 0, 0, 0, 0, 0, -18.0f });
                add ('C', "Wider start", "The first bars of " + B.label + " open up.", "arrival",
                     b, An::Head, 6.0, { 0, 0, 0.15f, 0, 0, 0 }, { 0, 0, 0.08f, 0, 0, 0 });
            }
            break;
    }
}
