#pragma once
#include "SectionTypes.h"
#include "../Intent/IntentVector.h"

// Builds three genuinely different directions per section, chosen by the section's musical function.
class VariantGenerator
{
public:
    static std::vector<Variant> forSection (int sectionIndex, const MusicState&, const std::vector<SectionDNA>&, int generation);
};

// Scores variants against what the track needs, the producer's intent and the risks.
class VariantRanker
{
public:
    struct Risks { bool stereoRisk = false; bool squashed = false; };
    static void rank (std::vector<Variant>&, const Wants&, const IntentVector&, const Risks&,
                      const std::vector<SectionDNA>&, bool preferKeep);
};
