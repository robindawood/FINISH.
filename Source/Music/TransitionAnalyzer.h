#pragma once
#include "SectionTypes.h"
#include "../Analysis/AudioAnalyzer.h"

// Transition Intelligence: judges every boundary (BUILD → DROP, DROP → BREAK, ...) by comparing the
// last seconds of one section with the first seconds of the next. Can conclude a transition already works.
class TransitionAnalyzer
{
public:
    static std::vector<Transition> analyze (const std::vector<AnalysisFrame>& grid, const MusicState& music,
                                            const std::vector<SectionDNA>& dna);
    // Three directions for a transition. generation > 0 gives alternative directions ("Regenerate").
    static void buildVariants (Transition&, const MusicState& music, int generation);
};
