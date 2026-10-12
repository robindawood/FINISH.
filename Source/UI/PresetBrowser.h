#pragma once
#include <JuceHeader.h>
#include "Theme.h"

class WhaleAudioProcessor;

// Overlay listing factory + user presets by style, with search, favourites and "save my preset".
class PresetBrowser : public juce::Component
{
public:
    explicit PresetBrowser (WhaleAudioProcessor&);
    std::function<void()> onClose;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> panel() const;
    juce::Rectangle<float> rowBounds (int visibleIndex) const;
    juce::Rectangle<float> catBounds (int i) const;
    juce::Rectangle<float> saveBounds() const;
    void rebuild();
    void saveCurrent();

    WhaleAudioProcessor& proc;
    juce::TextEditor search;
    int category = 0;      // 0 all, 1..6 styles, 7 favourites, 8 mine
    juce::Array<int> visible;
    int hoverRow = -1, scroll = 0;
};
