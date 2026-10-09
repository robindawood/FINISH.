#pragma once
#include "MusicState.h"
#include "../Analysis/AudioAnalyzer.h"

// Splits the track into sections from its loudness contour (INTRO / VERSE / BUILD / DROP / BREAK / OUTRO),
// with a confidence per section and a second-best reading when unsure. FINISH never claims a structure
// with certainty: uncertain sections are flagged so the producer can confirm or correct them.
class SectionDetector
{
public:
    // frames: one frame per 100 ms of song time (gaps = zero power), starting at song position startTime.
    MusicState analyze (const std::vector<AnalysisFrame>& frames, double startTime = 0.0) const;

    // Centred 3 s loudness contour (LUFS per frame).
    static std::vector<float> contour (const std::vector<AnalysisFrame>& frames);
    // Renumbers labels (DROP, DROP 2...) after the user relabels or moves sections.
    static void relabel (std::vector<Section>& sections);
    // Mean contour level between two song positions.
    static float meanLevel (const MusicState& m, double from, double to);
    // Recomputes loudness of each section from the contour.
    static void updateLoudness (MusicState& m);
};
