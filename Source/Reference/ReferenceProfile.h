#pragma once
#include <JuceHeader.h>
#include "../Analysis/AudioFeatures.h"

// A commercial/reference track analysed offline with the same analyzer as the user's mix.
struct ReferenceProfile
{
    bool valid = false;
    juce::String name;
    juce::String path;
    AudioFeatures features;
};
