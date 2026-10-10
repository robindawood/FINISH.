#pragma once
#include "NightTheme.h"
#include "../PluginProcessor.h"

// MASTER page screen: loudness over the last 20 seconds against the target, big readouts (short-term LUFS,
// true peak, limiter), and the mastering chain's live state (leveller bands, clipper, tonal balance).
class MasterView : public juce::Component
{
public:
    explicit MasterView (FINISHAudioProcessor&);
    void tick();                 // 30 Hz from the editor
    void paint (juce::Graphics&) override;
    void resized() override;
    void showReport (bool);

private:
    FINISHAudioProcessor& proc;
    juce::TextButton reportButton;
    juce::TextEditor reportText;
    bool reportMode = false;
    int reportTicks = 0;
    static constexpr int historySize = 600;   // 20 s at 30 Hz
    std::array<float, historySize> shortTerm {}, momentary {}, reduction {};
    int writePos = 0;
    float tpHold = -100.0f, grHold = 0.0f;
    int tpHoldTicks = 0;
};

// MASTER page column: loudness target (5 keys) and true-peak ceiling.
class MasterColumn : public juce::Component
{
public:
    explicit MasterColumn (FINISHAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    FINISHAudioProcessor& proc;
    std::array<juce::TextButton, numLoudnessTargets> targets;
    std::array<juce::TextButton, numMasterProfiles> profiles;
    juce::Slider ceiling { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider intensity { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ceilingAttachment, intensityAttachment;
};

// MASTER page console: the chain as a row of modules with their live values.
class MasterConsole : public juce::Component
{
public:
    explicit MasterConsole (FINISHAudioProcessor&);
    void paint (juce::Graphics&) override;

private:
    FINISHAudioProcessor& proc;
};
