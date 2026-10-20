#pragma once
#include <JuceHeader.h>
#include "Theme.h"

// First launch (1.3): three steps to a first master, shown once; menu > Quick tour shows it again.
class WelcomeOverlay : public juce::Component
{
public:
    WelcomeOverlay();
    std::function<void()> onClose;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    // remembered per user in the WHALE settings folder
    static bool wasSeen();
    static void markSeen();
private:
    juce::Rectangle<float> buttonBounds() const;
    juce::Rectangle<float> autoSpot { 14, 566, 184, 184 };   // the AUTO button in the 1200 x 750 design
};
