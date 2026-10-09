#pragma once
#include <JuceHeader.h>
#include "../Diagnosis/Diagnosis.h"
#include "../Intent/IntentVector.h"
#include "../Params.h"
#include <vector>

struct Proposal
{
    juce::String title;          // PUNCH / WIDE / MASSIVE
    juce::String description;    // what it does
    juce::String reason;         // why it is proposed for this track
    float confidence = 0;
    bool refused = false;        // FINISH declines when the change would likely degrade the mix
    MacroSettings targets;
};

struct DecisionResult
{
    std::vector<Proposal> proposals;   // best first, refused last
    juce::StringArray notes;           // intent feedback / refusals
};

class DecisionEngine
{
public:
    DecisionResult build (const std::vector<Diagnosis>&, const IntentVector&, bool hasAnalysis) const;
};
