#pragma once
#include "NightTheme.h"
#include "../PluginProcessor.h"

namespace Night
{
    inline juce::Colour laneColour (Lane l)
    {
        static const juce::uint32 cols[] = { 0xffff5a8a, 0xff9a6bff, 0xff2de2ff, 0xffffc857, 0xff39ffb0, 0xffff7a3d };
        return juce::Colour (cols[(int) l]);
    }
}

// The arrangement as a little DAW timeline: one column per part (INTRO, BUILD...), one row per instrument.
// Click a part's name to hear it (loops), drag any clip straight into the DAW, right-click for moves.
class ArrangementView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ArrangementView (FINISHAudioProcessor&);
    std::function<void (int)> onSelect;
    int getSelected() const noexcept { return selected; }
    void setSelected (int s) { selected = s; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Hit { int section = -1; int lane = -1; bool header = false, lockIcon = false, diceIcon = false, laneLabel = false; };
    Hit hitAt (juce::Point<float>) const;
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> sectionColumn (int i) const;
    juce::Rectangle<float> cellRect (int section, int lane) const;
    void drawClip (juce::Graphics&, const GenSection&, Lane, juce::Rectangle<float>, bool selectedSection) const;
    void showSectionMenu (int section);

    FINISHAudioProcessor& proc;
    int selected = -1;
    Hit hover;
    Hit pressed;
    bool dragStarted = false;
};

// Right column on the COMPOSE page: what FINISH heard (key, tempo, style; click to correct),
// the three versions, and how far the new parts may stray from the original.
class ComposeColumn : public juce::Component
{
public:
    explicit ComposeColumn (FINISHAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();

private:
    FINISHAudioProcessor& proc;
    std::array<juce::TextButton, numFlavours> flavours;
    juce::Slider creativity { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<float> keyArea, styleArea, bpmArea;
};

// Console on the COMPOSE page: the selected part (what it does, its parts to drag) and the transport / delivery keys.
class ComposeConsole : public juce::Component
{
public:
    explicit ComposeConsole (FINISHAudioProcessor&);
    std::function<void()> onChange;
    void setSection (int s) { section = s; refresh(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> chipRect (int lane) const;
    FINISHAudioProcessor& proc;
    int section = -1;
    int dragLane = -1;
    bool dragStarted = false;
    juce::TextButton playPart { "PLAY PART" }, lock { "LOCK" }, newIdea { "NEW IDEA" };
    juce::TextButton playAll { "PLAY ALL" }, stop { "STOP" }, solo { "SOLO" };
    juce::TextButton sendLive { "SEND TO LIVE" }, exportFiles { "EXPORT" }, undo { "UNDO" };
};
