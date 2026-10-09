#pragma once
#include "NightTheme.h"
#include "../PluginProcessor.h"

struct CardData
{
    juce::String letter, name, target, description, reason;
    float score = 0;
    bool recommended = false, refused = false, applied = false, previewing = false, selected = false;
};

class VariantCard : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;
    void setData (const CardData& d) { data = d; setTooltip (d.refused ? d.reason : d.description); repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    CardData data;
    bool hover = false;
};

// The right-hand panel: what the selected section / transition / whole track is, what FINISH thinks,
// and three directions to compare (Preview A/B/C, Amount, Keep Original, Regenerate, Apply, Undo).
class Inspector : public juce::Component
{
public:
    using Selection = TrackEngine::Selection;
    explicit Inspector (FINISHAudioProcessor&);

    std::function<void (Selection)> onSelect;
    void setSelection (Selection);
    Selection getSelection() const noexcept { return selection; }
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Mode { Idle, Track, Section, Transition };
    Mode mode() const;
    void cardClicked (int index);
    void paintDNA (juce::Graphics&, juce::Rectangle<float>, const SectionDNA&);
    void paintTransitionMeter (juce::Graphics&, juce::Rectangle<float>, const Transition&);
    void paintTrackSummary (juce::Graphics&, juce::Rectangle<float>);
    void showTypeMenu();

    FINISHAudioProcessor& proc;
    Selection selection;
    int selectedCard = -1;
    float amount = 1.0f;

    std::array<VariantCard, 3> cards;
    juce::TextButton keepButton { "KEEP ORIGINAL" }, regenButton { "REGENERATE" }, undoButton { "UNDO" };
    juce::TextButton applyButton { "APPLY" }, hearOriginal { "HEAR ORIGINAL" };
    juce::TextButton confirmButton { "CONFIRM" }, changeButton { "CHANGE" }, linkButton;
    juce::Slider amountSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
};
