#pragma once
#include <JuceHeader.h>
#include "MusicState.h"
#include "../Analysis/AudioFeatures.h"
#include <map>
#include <vector>

// Offsets added on top of the global FINISH controls while a section/transition edit is active.
// impact/punch/loudness: 0..1 deltas, width/warmth: deltas around neutral 0.5, trimDb: level change in dB.
// hp/lp/gate/echo/space: creative FX amounts 0..1 (filter sweeps, trance gate, echo throw, reverb throw).
struct SectionMacros
{
    float impact = 0, punch = 0, width = 0, warmth = 0, loudness = 0, trimDb = 0;
    float hp = 0, lp = 0, gate = 0, echo = 0, space = 0;

    static constexpr int numValues = 11;
    float* values() noexcept { return &impact; }
    const float* values() const noexcept { return &impact; }

    SectionMacros scaled (float k) const noexcept
    {
        SectionMacros r;
        for (int i = 0; i < numValues; ++i) r.values()[i] = values()[i] * k;
        return r;
    }
    SectionMacros& operator+= (const SectionMacros& o) noexcept
    {
        for (int i = 0; i < numValues; ++i) values()[i] += o.values()[i];
        return *this;
    }
    static SectionMacros lerp (const SectionMacros& a, const SectionMacros& b, float t) noexcept
    {
        SectionMacros r;
        for (int i = 0; i < numValues; ++i) r.values()[i] = a.values()[i] + (b.values()[i] - a.values()[i]) * t;
        return r;
    }
    bool hasFx() const noexcept { return hp + lp + gate + echo + space > 1.0e-4f; }
};
static_assert (sizeof (SectionMacros) == SectionMacros::numValues * sizeof (float), "SectionMacros must stay a flat float block");

// Builder for readable variant definitions.
struct FX
{
    SectionMacros m;
    FX& impact (float v)   { m.impact = v; return *this; }
    FX& punch (float v)    { m.punch = v; return *this; }
    FX& width (float v)    { m.width = v; return *this; }
    FX& warmth (float v)   { m.warmth = v; return *this; }
    FX& loud (float v)     { m.loudness = v; return *this; }
    FX& trim (float v)     { m.trimDb = v; return *this; }
    FX& hp (float v)       { m.hp = v; return *this; }
    FX& lp (float v)       { m.lp = v; return *this; }
    FX& gate (float v)     { m.gate = v; return *this; }
    FX& echo (float v)     { m.echo = v; return *this; }
    FX& space (float v)    { m.space = v; return *this; }
    operator SectionMacros() const { return m; }
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
