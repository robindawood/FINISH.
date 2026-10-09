#pragma once
#include <JuceHeader.h>

enum class DiagnosisType
{
    Clipping, OverCompressed, TooQuiet, LouderThanReference, WeakTransients,
    Narrow, StereoRisk, WideBass, LowHeavy, LowThin, Harsh, Dull, FlatArrangement
};

struct Diagnosis
{
    DiagnosisType type;
    float severity = 0;      // 0..1, how much it holds the track back
    float confidence = 0;    // 0..1
    juce::String title, explanation;
};
