#pragma once
#include "NightTheme.h"
#include "../PluginProcessor.h"

// Right-hand column (under the FINISH arcade key): 7-segment score, opportunities LCD, intent LCD.
class TrackPanel : public juce::Component
{
public:
    explicit TrackPanel (FINISHAudioProcessor&);
    std::function<void()> onIntent;
    std::function<void()> onShowTrack;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::TextEditor intentEditor;   // public for the snapshot test
private:
    void submit();
    FINISHAudioProcessor& proc;
    juce::TextButton go { "GO" };
};

// Bottom row: master knobs (global base layer) + two backlit VU meters.
class ControlDeck : public juce::Component
{
public:
    explicit ControlDeck (FINISHAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void repaintLive() { repaint (getLocalBounds().withTrimmedLeft (meterX - 6)); }

private:
    void paintVU (juce::Graphics&, juce::Rectangle<float>, float vu, const juce::String& name, const juce::String& extra);

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::String name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    FINISHAudioProcessor& proc;
    std::array<Knob, 7> knobs;
    int meterX = 790;
    float inNeedle = -20.0f, outNeedle = -20.0f;
};
