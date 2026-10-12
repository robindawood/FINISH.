#pragma once
#include <JuceHeader.h>
#include "Theme.h"

class WhaleAudioProcessor;

class WhaleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WhaleLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::bold (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return Theme::bold (13.5f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool hl, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// Rotary macro with a tick ring, value under the name. Double-click resets to 50 %.
class MacroKnob : public juce::Component
{
public:
    MacroKnob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& name, juce::Colour);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::String name;
    juce::Colour colour;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// The AUTO button: ring = progress, three states (ready / running / done).
class AutoButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;
    void setState (int s, float progress, const juce::String& sub) { state = s; prog = progress; subText = sub; repaint(); }
    void tick (float dt) { phase += dt; if (state != 1) repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    int state = 0;   // 0 ready, 1 running, 2 done
    float prog = 0, phase = 0;
    bool hover = false;
    juce::String subText;
};

// Signal chain: 7 module chips with a power dot and an activity bar. Click = open in EXPERT, dot = on/off.
class ChainStrip : public juce::Component
{
public:
    explicit ChainStrip (juce::AudioProcessorValueTreeState&);
    std::function<void (int)> onOpenModule;
    void setActivity (int module, float a) { activity[(size_t) module] = a; }
    void setSelected (int m) { selected = m; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    static const juce::StringArray& names();
private:
    juce::Rectangle<float> chipBounds (int i) const;
    juce::AudioProcessorValueTreeState& state;
    std::array<float, 7> activity {};
    int selected = -1;
};

// Meter rows: true peak, dynamics (PLR), stereo correlation, mono low end.
class MeterPanel : public juce::Component
{
public:
    struct Row { juce::String label, value; float bar = 0; juce::Colour colour = Theme::green; };
    void setRows (const std::array<Row, 4>& r) { rows = r; repaint(); }
    void paint (juce::Graphics&) override;
private:
    std::array<Row, 4> rows;
};

// Preset bar: < * name  CATEGORY | PRESETS v | >
class PresetBar : public juce::Component
{
public:
    std::function<void (int)> onStep;
    std::function<void()> onOpen, onFavourite;
    void set (const juce::String& n, const juce::String& cat, bool fav, bool open) { name = n; category = cat; favourite = fav; isOpen = open; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::String name, category;
    bool favourite = false, isOpen = false;
};

// SANS / AVEC (A/B bypass, level-matched).
class ABSwitch : public juce::Component, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit ABSwitch (juce::AudioProcessorValueTreeState&);
    ~ABSwitch() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void parameterChanged (const juce::String&, float) override { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ABSwitch> (this)] { if (sp) sp->repaint(); }); }
    juce::AudioProcessorValueTreeState& state;
};

// Small text button (EXPERT, menu, etc.)
class TextLink : public juce::Component
{
public:
    TextLink (const juce::String& t) : textValue (t) {}
    std::function<void()> onClick;
    bool active = false;
    void setText (const juce::String& t) { textValue = t; repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (active ? Theme::text : (hover ? Theme::text.withAlpha (0.8f) : Theme::dim));
        g.setFont (Theme::bold (13.5f));
        g.drawText (textValue, getLocalBounds(), juce::Justification::centred);
        if (active) { g.setColour (Theme::magenta); g.fillRect (getWidth() / 2 - 14, getHeight() - 3, 28, 2); }
    }
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    juce::String textValue;
    bool hover = false;
};
