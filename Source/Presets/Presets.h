#pragma once
#include <JuceHeader.h>
#include "../Params.h"

// Factory presets (starting points written from mastering practice per style, tuned on the reference profiles)
// and user presets (XML files in the user's application data folder). A preset sets the module bases, the style
// and the loudness target; the macros go back to neutral.
struct WhalePreset
{
    juce::String name, description;
    int style = styleTechHouse;     // category
    int target = styleTarget;
    float ceiling = -1.0f;
    ModuleBase base;
    float colour[29] {};            // factory presets: the tonal colour they add (dB per 1/3 octave)
    bool hasColour = false;
    juce::String tags;
    float loudOffset = 0.0f;        // dB added to the style's loudness target (dynamic presets lower, dense ones higher)              // one line: what it does (+ basses · punch fort · dense ...)
    float lowCut = -1.0f;           // < 0: the preset leaves the low-cut alone (it corrects the mix, not the character)
    bool factory = true;
    juce::File file;                // user presets
};

class PresetManager
{
public:
    PresetManager();

    const juce::Array<WhalePreset>& getAll() const { return all; }
    void refreshUserPresets();

    void apply (const WhalePreset& p, juce::AudioProcessorValueTreeState& state) const;
    WhalePreset capture (const juce::AudioProcessorValueTreeState& state, const juce::String& name) const;
    bool saveUserPreset (const WhalePreset& p);
    static juce::File userFolder();

    bool isFavourite (const juce::String& name) const;
    void setFavourite (const juce::String& name, bool fav);

    int getCurrent() const { return current; }
    void setCurrent (int index) { current = index; }
    juce::String getCurrentName() const { return juce::isPositiveAndBelow (current, all.size()) ? all[current].name : juce::String ("Init"); }

private:
    juce::Array<WhalePreset> factory, all;
    juce::StringArray favourites;
    int current = -1;
    void loadFavourites();
    void saveFavourites() const;
};
