#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/NightLookAndFeel.h"
#include "UI/TrackMapView.h"
#include "UI/Inspector.h"
#include "UI/Panels.h"

// Big round illuminated key (FINISH MY TRACK).
class ArcadeButton : public juce::Button
{
public:
    ArcadeButton() : juce::Button ("FINISH MY TRACK") {}
    enum class Mode { Idle, Listening, Ready };
    void setMode (Mode m, double seconds) { mode = m; listenSeconds = seconds; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    Mode mode = Mode::Idle;
    double listenSeconds = 0;
};

class FINISHAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FINISHAudioProcessorEditor (FINISHAudioProcessor&);
    ~FINISHAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int baseWidth = 1200, baseHeight = 800;

    // The whole faceplate is laid out at base size inside this component, then scaled as one piece.
    struct Content : public juce::Component
    {
        explicit Content (FINISHAudioProcessorEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        FINISHAudioProcessorEditor& editor;
    };

    void select (TrackEngine::Selection);
    Inspector& getInspector() noexcept { return inspector; }
    TrackPanel& getTrackPanel() noexcept { return trackPanel; }

private:
    void timerCallback() override;
    void refreshAll();
    void updateFinishButton();
    void chooseReference();
    void autoSelectBiggestOpportunity();

    FINISHAudioProcessor& proc;
    NightLookAndFeel lnf;
    Content content { *this };

    ArcadeButton finishButton;
    juce::TextButton refButton { "REFERENCE" }, refClear { "X" };
    juce::TextButton matchButton { "LEVEL MATCH" }, bypassButton { "A/B BYPASS" };

    TrackMapView trackMap;
    Inspector inspector;
    TrackPanel trackPanel;
    ControlDeck deck;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, matchAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image faceplate;
    int lastVersion = -1;
    double lastMapStart = -1.0;
    size_t lastSectionCount = 0;
    bool wasListening = false;
    juce::TooltipWindow tooltips { this, 500 };

    friend struct Content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FINISHAudioProcessorEditor)
};
