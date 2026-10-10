#pragma once
#include "NightTheme.h"
#include "../PluginProcessor.h"

// ======================================================================================
// PROJECT page: the user's own Live set and what FINISH plans to add to it.
// One row per track of the set, one column per section (the existing ones, hatched, then the new ones).
// Click a new section's name to hear an approximate preview, a cell to switch that track off there,
// a track name to lock it (exact copies only).
class ProjectView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ProjectView (FINISHAudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onChange;

private:
    struct Hit { int section = -1, track = -1; bool header = false, label = false; };
    Hit hitAt (juce::Point<float>) const;
    juce::Rectangle<float> grid() const;
    std::vector<int> rows() const;               // tracks shown
    float xOfBar (double bar) const;
    void paintEmpty (juce::Graphics&, juce::Rectangle<float>);
    void showSectionMenu (int section);

    FINISHAudioProcessor& proc;
    Hit hover;
};

// Right column: connection LED + diagnosis, intervention level, versions.
class ProjectColumn : public juce::Component
{
public:
    explicit ProjectColumn (FINISHAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onChange;

private:
    FINISHAudioProcessor& proc;
    std::array<juce::TextButton, 4> levels;
    std::array<juce::TextButton, 3> versions;
    juce::TextButton install { "INSTALLER" };
};

// Console: FINISH's answers (analysis, plan, reports, journal) and the Live keys.
class ProjectConsole : public juce::Component
{
public:
    explicit ProjectConsole (FINISHAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void confirmAndSend();
    std::function<void()> onChange;

private:
    FINISHAudioProcessor& proc;
    juce::TextEditor text;
    juce::TextButton read, listen, stop, previous, send, undoSend, journal;
    int shownAnswer = -1;
    bool showingJournal = false;
};
