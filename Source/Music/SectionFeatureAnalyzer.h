#pragma once
#include "SectionTypes.h"
#include "../Analysis/AudioAnalyzer.h"

// Measures every section (Section DNA) and assigns its musical role.
class SectionFeatureAnalyzer
{
public:
    static std::vector<SectionDNA> analyze (const std::vector<AnalysisFrame>& grid, const MusicState& music);
    // Features of an arbitrary song-time window (used for transition edges).
    static AudioFeatures measure (const std::vector<AnalysisFrame>& grid, const MusicState& music, double from, double to);
};
