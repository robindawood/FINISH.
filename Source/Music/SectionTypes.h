#pragma once
#include <JuceHeader.h>
#include "MusicState.h"
#include "../Analysis/AudioFeatures.h"
#include <map>
#include <vector>

// Offsets added on top of the global FINISH controls while a section/transition edit is active.
// impact/punch/loudness: 0..1 deltas, width/warmth: deltas around neutral 0.5, trimDb: level change in dB.
struct SectionMacros
{
    float impact = 0, punch = 0, width = 0, warmth = 0, loudness = 0, trimDb = 0;

    SectionMacros scaled (float k) const noexcept { return { impact * k, punch * k, width * k, warmth * k, loudness * k, trimDb * k }; }
    SectionMacros& operator+= (const SectionMacros& o) noexcept
    {
        impact += o.impact; punch += o.punch; width += o.width; warmth += o.warmth; loudness += o.loudness; trimDb += o.trimDb;
        return *this;
    }
    static SectionMacros lerp (const SectionMacros& a, const SectionMacros& b, float t) noexcept
    {
        return { a.impact + (b.impact - a.impact) * t, a.punch + (b.punch - a.punch) * t, a.width + (b.width - a.width) * t,
                 a.warmth + (b.warmth - a.warmth) * t, a.loudness + (b.loudness - a.loudness) * t, a.trimDb + (b.trimDb - a.trimDb) * t };
    }
};

// Where an edit lives, relative to the detected sections (survives marker edits).
struct TargetSpec
{
    enum class Anchor { Whole, Head, Tail };
    int section = 0;
    Anchor anchor = Anchor::Whole;
    double seconds = 0;    // for Head/Tail

    std::pair<double, double> resolve (const std::vector<Section>& sections) const
    {
        if (! juce::isPositiveAndBelow (section, (int) sections.size())) return { 0.0, 0.0 };
        const auto& s = sections[(size_t) section];
        const double len = s.length();
        switch (anchor)
        {
            case Anchor::Head: return { s.startTime, s.startTime + std::min (seconds, len * 0.5) };
            case Anchor::Tail: return { s.endTime - std::min (seconds, len * 0.4), s.endTime };
            case Anchor::Whole: break;
        }
        return { s.startTime, s.endTime };
    }
};

// One creative direction (A / B / C) for a section or a transition.
struct Variant
{
    char letter = 'A';
    juce::String name, description;
    juce::String tag;                 // what it addresses: punch, width, mass, space, tension, minimal, rise, setup, bigger, evolve, contrast...
    TargetSpec target;
    SectionMacros from, to;           // ramp across the target region (from == to for a static treatment)
    float score = 0;                  // 0..1 relevance for this track
    bool refused = false, recommended = false;
    juce::String reason;              // why recommended / refused
};

// Section DNA: what a section is, what it is for, how it measures against the rest of the track.
struct SectionDNA
{
    SectionRole role = SectionRole::Whole;
    AudioFeatures features;           // measured inside the section
    float level = -70, peakLevel = -70, slopeDb = 0, density = 0;
    // 0..1 relative to the track (display + comparisons)
    float nEnergy = 0, nDensity = 0, nPunch = 0, nLow = 0, nAir = 0, nWidth = 0;
};

using Wants = std::map<juce::String, float>;   // tag -> relevance

struct SectionVerdict
{
    float potential = 0;              // 0..1 improvement potential
    juce::String headline;            // "DROP 2 — Already powerful."
    juce::String explanation;
    juce::String recommendation;
    int transitionRef = -1;           // recommendation points to a transition instead
    Wants wants;
};

struct Transition
{
    enum class Kind { IntoPeak, OutOfPeak, IntoOutro, IntroDrop, Other };
    int index = 0;                    // boundary between sections[index] and sections[index + 1]
    Kind kind = Kind::Other;
    juce::String title;               // "BUILD → DROP"
    bool works = true;
    float severity = 0;
    float contrastDb = 0;             // level change across the boundary
    juce::String headline, explanation;
    Wants wants;
    std::vector<Variant> variants;
};
