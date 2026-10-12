#pragma once
#include <JuceHeader.h>
#include "../Params.h"
#include "../Analysis/AudioFeatures.h"
#include "StyleProfilesData.h"

// AUTO: compares what was heard (features of the input mix) with the reference corridor of a style, measured on
// real released masters, and writes prudent module settings. Rule-based and explainable: every move has a reason
// line in the report. It never corrects what already sits inside the corridor.
struct AutoResult
{
    bool valid = false;
    int style = styleTechHouse;        // style used (1..6)
    bool styleWasDetected = false;
    float loudPassageTarget = -7.0f;   // LUFS short-term of the loud passages (what the closed loop holds)
    float integratedTarget = -8.5f;    // what the style's masters measure integrated (for display)
    ModuleBase base;
    juce::StringArray report;          // what AUTO did (French, for the user)
    juce::StringArray mixNotes;        // problems that belong to the mix, not to the master
};

class AutoMaster
{
public:
    static const StyleProfileData& profile (int style) noexcept;   // style 1..6
    static int detectStyle (const AudioFeatures& f, double bpm);
    // Tempo from 1 kHz rhythm envelopes (hats grid + kick), half-time folded into 100-200 BPM. 0 if unclear.
    static double estimateTempo (const std::vector<float>& low, const std::vector<float>& high, double rate);
    static AutoResult compute (const AudioFeatures& f, int styleParam, double bpm, int targetIndex, float ceilingDb);

    // Distance (dB of energy odds) from the style corridor: 0 inside, < 0 below, > 0 above.
    static float corridorDistanceDb (float value, const ProfileStat& s) noexcept;
};
