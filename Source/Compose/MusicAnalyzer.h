#pragma once
#include "ComposeTypes.h"
#include "MusicCapture.h"

// Listens to what the user played (a drop, a loop, a lone melody...) and extracts its musical identity:
// tempo grid, key (with confidence), groove templates, swing, bass line, chord loop, melody motif, character, style.
// Every estimate carries a confidence; nothing is presented as certain.
namespace MusicAnalyzer
{
    MusicalDNA analyze (const MusicCapture::Snapshot& snapshot);

    // Key-finding helper (Krumhansl-Schmuckler). Returns correlation for (root, minor).
    float keyCorrelation (const std::array<float, 12>& chroma, int root, bool minor);
    // Pitch classes of the 7 diatonic triad roots and their quality for a key.
    void diatonicTriads (int keyRoot, bool minor, int roots[7], bool isMinor[7]);
}
