#pragma once
#include "Diagnosis.h"
#include "../Analysis/AudioFeatures.h"
#include "../Music/MusicState.h"
#include "../Reference/ReferenceProfile.h"
#include <vector>

// Turns measurements into ranked, explained problems ("what is missing for this to feel finished?").
class DiagnosisEngine
{
public:
    std::vector<Diagnosis> analyze (const AudioFeatures&, const MusicState&, const ReferenceProfile&) const;

    // 0..100. Higher = closer to finished.
    static int finishScore (const std::vector<Diagnosis>&);

    static bool has (const std::vector<Diagnosis>& d, DiagnosisType t)
    {
        for (auto& x : d) if (x.type == t) return true;
        return false;
    }
};
