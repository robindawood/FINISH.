#pragma once
#include <JuceHeader.h>
#include "Theme.h"

class WhaleAudioProcessor;

// EXPERT: the detailed settings of each module (the "base" that AUTO / presets write), the gain-reduction meters
// and the AUTO report ("what AUTO did and why").
class ExpertPanel : public juce::Component
{
public:
    explicit ExpertPanel (WhaleAudioProcessor&);
    void showModule (int m);              // 0..6 modules, 7 = AUTO report
    int getModule() const { return module; }
    std::function<void()> onClose;
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Row
    {
        juce::String label, hint;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    void build();
    void addRow (const char* id, const juce::String& label, const juce::String& hint, juce::Colour c, bool bipolar = false);
    juce::Rectangle<float> tabBounds (int i) const;
    juce::Rectangle<float> graphBounds() const;
    void drawSpectral (juce::Graphics&, juce::Rectangle<float>);
    void drawReport (juce::Graphics&, juce::Rectangle<float>);

    WhaleAudioProcessor& proc;
    int module = 0;
    std::vector<std::unique_ptr<Row>> rows;
    juce::ComboBox targetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAtt;
    float meterGr[3] {}, meterSpec[4] {};
};
