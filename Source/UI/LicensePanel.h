#pragma once
#include <JuceHeader.h>
#include "../License/LicenseManager.h"
#include "Theme.h"

// Licence overlay: trial status, key field, ACTIVER, link to buy.
class LicensePanel : public juce::Component
{
public:
    explicit LicensePanel (LicenseManager&);
    std::function<void()> onClose;
    void refresh();                               // status text from the licence manager
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    friend struct TestAccess;
private:
    juce::Rectangle<float> card() const;
    juce::Rectangle<int> activateBounds() const, buyBounds() const, closeBounds() const;
    void startActivation();

    LicenseManager& lic;
    juce::TextEditor keyField;
    juce::String message;
    juce::Colour messageColour = Theme::dim;
    bool working = false;
    int hover = 0;                                // 1 activate, 2 buy, 3 close
};
