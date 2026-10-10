#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/NightLookAndFeel.h"
#include "UI/TrackMapView.h"
#include "UI/Inspector.h"
#include "UI/Panels.h"
#include "UI/ComposeViews.h"
#include "UI/MasterViews.h"

// Big round illuminated key (FINISH MY TRACK).
class ArcadeButton : public juce::Button
{
public:
    ArcadeButton() : juce::Button ("FINISH MY TRACK") {}
    enum class Mode { Idle, Listening, Working, Ready };
    void setMode (Mode m, double seconds) { mode = m; listenSeconds = seconds; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    Mode mode = Mode::Idle;
    double listenSeconds = 0;
};

// The header LCD: shows what FINISH is doing; click and type to ask for anything (FR / EN).
class CommandStrip : public juce::Component
{
public:
    explicit CommandStrip (FINISHAudioProcessor&);
    std::function<void()> onSubmit;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::TextEditor editor;
private:
    FINISHAudioProcessor& proc;
};

class FINISHAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit FINISHAudioProcessorEditor (FINISHAudioProcessor&);
    ~FINISHAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int baseWidth = 1200, baseHeight = 800;
    enum Page { compose = 0, sections = 1, master = 2 };

    struct Content : public juce::Component
    {
        explicit Content (FINISHAudioProcessorEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        FINISHAudioProcessorEditor& editor;
    };

    void select (TrackEngine::Selection);
    void setPage (int page);
    int getPage() const noexcept { return page; }
    Inspector& getInspector() noexcept { return inspector; }
    TrackPanel& getTrackPanel() noexcept { return trackPanel; }
    CommandStrip& getCommandStrip() noexcept { return command; }
    ArrangementView& getArrangementView() noexcept { return arrangement; }
    ComposeConsole& getComposeConsole() noexcept { return composeConsole; }
    void selectArrangementSection (int s);

private:
    void timerCallback() override;
    void refreshAll();
    void updateFinishButton();
    void chooseReference();
    void autoSelectBiggestOpportunity();

    FINISHAudioProcessor& proc;
    NightLookAndFeel lnf;
    Content content { *this };
    int page = compose;

    ArcadeButton finishButton;
    std::array<juce::TextButton, 3> tabs;
    juce::TextButton refButton { "REFERENCE" }, refClear { "X" };
    juce::TextButton matchButton { "MATCH" }, bypassButton { "A/B" };
    CommandStrip command;

    // SECTIONS page
    TrackMapView trackMap;
    Inspector inspector;
    TrackPanel trackPanel;
    // COMPOSE page
    ArrangementView arrangement;
    ComposeColumn composeColumn;
    ComposeConsole composeConsole;
    // MASTER page
    MasterView masterView;
    MasterColumn masterColumn;
    MasterConsole masterConsole;

    ControlDeck deck;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, matchAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image faceplate;
    int lastVersion = -1;
    double lastMapStart = -1.0;
    size_t lastSectionCount = 0;
    bool wasListening = false;
    bool hadArrangement = false;
    juce::TooltipWindow tooltips { this, 500 };

    friend struct Content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FINISHAudioProcessorEditor)
};
