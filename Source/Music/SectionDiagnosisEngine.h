#pragma once
#include "SectionTypes.h"

// Judges each section in context (never in isolation): a drop can be fine while the build before it
// is the real problem. Can say a section already works, and can point to a transition instead.
class SectionDiagnosisEngine
{
public:
    static std::vector<SectionVerdict> analyze (const MusicState&, const std::vector<SectionDNA>&,
                                                const std::vector<Transition>&);

    static juce::String statusText (float potential)
    {
        return potential < 0.25f ? "ALREADY WORKING" : (potential < 0.5f ? "POTENTIAL  MEDIUM" : "POTENTIAL  HIGH");
    }
};
