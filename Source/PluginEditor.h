#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Widgets.h"
#include "UI/WhaleScene.h"
#include "UI/PresetBrowser.h"
#include "UI/ExpertPanel.h"
#include "UI/LicensePanel.h"
#include "UI/WelcomeOverlay.h"

// Fixed 1200 x 750 design, scaled as a whole when the window is resized.
class WhaleEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WhaleEditor (WhaleAudioProcessor&);
    ~WhaleEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int designW = 1200, designH = 750;

    friend struct TestAccess;
private:
    class Content : public juce::Component
    {
    public:
        explicit Content (WhaleEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        WhaleEditor& owner;
    };

    void timerCallback() override;
    void layout();
    void openExpert (int module);
    void showMenu();
    bool reducedAnimations() const;
    void setReducedAnimations (bool);

    WhaleAudioProcessor& proc;
    WhaleLookAndFeel lnf;
    Content content { *this };
   #if JUCE_MODULE_AVAILABLE_juce_opengl
    juce::OpenGLContext gl;
   #endif
    juce::TooltipWindow tooltips { this, 600 };

    WhaleScene scene;
    PresetBar presetBar;
    ABSwitch ab;
    TextLink expertLink { "EXPERT" }, menuLink { juce::String::fromUTF8 ("\xe2\x89\xa1") };
    TextLink undoAutoLink { juce::String::fromUTF8 ("\xe2\x86\xb6  UNDO AUTO") };
    ChainStrip chain;
    AutoButton autoButton;
    juce::ComboBox styleBox, targetBox;
    juce::Slider dosage;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt, targetAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dosageAtt;
    std::vector<std::unique_ptr<MacroKnob>> knobs;
    MeterPanel meters;
    ExpertPanel expert;
    PresetBrowser browser;
    LicensePanel licencePanel;
    WelcomeOverlay welcome;
public:
    void showWelcome (bool show);     // first launch / menu > Quick tour (tests hide it before rendering)
private:
    TextLink licenceLink { "" };
    void showLicence (bool);
    void updateLicenceLink();

    double lastTime = 0;
    float tpHold = -100, tpHoldAge = 0, peak10 = -100, peak10Age = 0, lufsSmooth = -70;
    int frameCounter = 0;
    float forcedDt = 0.0f;   // tests: simulated frame time
};
