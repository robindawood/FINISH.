#pragma once
#include <JuceHeader.h>
#include "IntentVector.h"

// Understands short requests in English or French, e.g.
//   "make my drop hit harder", "plus large mais pas plus fort", "trop de basses", "it sounds dull".
class IntentParser
{
public:
    struct Result
    {
        IntentVector intent;
        juce::StringArray understood;   // e.g. "+PUNCH", "-LOUDNESS"
    };

    static Result parse (const juce::String& text);
    static juce::String normalise (const juce::String& text);
};
