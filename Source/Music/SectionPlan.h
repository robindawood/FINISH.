#pragma once
#include "SectionTypes.h"
#include <array>

// SectionProcessor runtime: the applied edits, compiled to absolute song times.
// Plain data, fixed capacity, copied to the audio thread and evaluated per block. No allocation.
struct PlanEdit
{
    double start = 0, end = 0;
    SectionMacros from, to;
    float amount = 1.0f;
    bool keepOriginal = false;
};

struct SectionPlan
{
    static constexpr int capacity = 64;
    static constexpr double edgeFade = 0.12;   // seconds of fade at region edges (click-free)

    int count = 0;
    std::array<PlanEdit, capacity> edits {};
    SectionMacros mean;                        // duration-weighted mean offset over the track (for Coherence)

    struct Layer { SectionMacros delta; float keepOriginal = 0; };

    Layer evaluate (double t, float coherence) const noexcept
    {
        Layer layer;
        SectionMacros sum;
        for (int i = 0; i < count; ++i)
        {
            const auto& e = edits[(size_t) i];
            if (t < e.start || t >= e.end) continue;
            const double len = e.end - e.start;
            const float pos = len > 0 ? (float) ((t - e.start) / len) : 0.0f;
            float fade = 1.0f;
            if (len > edgeFade * 2.5)
                fade = (float) std::min ({ 1.0, (t - e.start) / edgeFade, (e.end - t) / edgeFade });
            if (e.keepOriginal) { layer.keepOriginal = std::max (layer.keepOriginal, fade); continue; }
            sum += SectionMacros::lerp (e.from, e.to, pos).scaled (e.amount * fade);
        }
        // Coherence pulls each section towards the track-wide average so the song stays one piece.
        // Level trims are deliberate contrast and are kept as they are.
        const float k = 0.6f * juce::jlimit (0.0f, 1.0f, coherence);
        layer.delta.impact   = sum.impact   * (1 - k) + mean.impact   * k;
        layer.delta.punch    = sum.punch    * (1 - k) + mean.punch    * k;
        layer.delta.width    = sum.width    * (1 - k) + mean.width    * k;
        layer.delta.warmth   = sum.warmth   * (1 - k) + mean.warmth   * k;
        layer.delta.loudness = sum.loudness * (1 - k) + mean.loudness * k;
        layer.delta.trimDb   = sum.trimDb;
        return layer;
    }
};
