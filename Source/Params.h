#pragma once
#include <JuceHeader.h>

// All automatable parameters. IDs are part of saved projects: never rename them.
namespace ParamIDs
{
    inline constexpr const char* amount   = "amount";
    inline constexpr const char* impact   = "impact";
    inline constexpr const char* punch    = "punch";
    inline constexpr const char* width    = "width";
    inline constexpr const char* warmth   = "warmth";
    inline constexpr const char* loudness = "loudness";
    inline constexpr const char* bypass   = "bypass";
    inline constexpr const char* match    = "levelMatch";
    inline constexpr const char* coherence = "coherence";
    inline constexpr const char* target   = "target";
    inline constexpr const char* ceiling  = "ceiling";

    inline constexpr int version = 1;
}

// Loudness targets (integrated LUFS). Index 0 = manual (DRIVE knob).
struct LoudnessTarget { const char* name; const char* shortName; float lufs; };
inline constexpr LoudnessTarget loudnessTargets[] = {
    { "Manual",          "MANUAL",  0.0f },
    { "Streaming -14",   "STREAM",  -14.0f },
    { "Balanced -11",    "BALANCED", -11.0f },
    { "Club -9",         "CLUB",    -9.0f },
    { "Loud -7",         "LOUD",    -7.0f } };
inline constexpr int numLoudnessTargets = 5;

// Neutral values: with these, FINISH is transparent (except the -1 dBFS safety ceiling).
struct MacroSettings
{
    float amount = 1.0f, impact = 0.0f, punch = 0.0f, width = 0.5f, warmth = 0.5f, loudness = 0.0f;
};

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const MacroSettings neutral;

    auto percent = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto parsePercent = [] (const String& t) { return jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f); };

    auto addFloat = [&] (const char* id, const char* name, float def,
                         std::function<String (float, int)> toText,
                         std::function<float (const String&)> fromText)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, ParamIDs::version }, name, NormalisableRange<float> (0.0f, 1.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText))
                                           .withValueFromStringFunction (std::move (fromText))));
    };

    addFloat (ParamIDs::amount,   "Finish Amount", neutral.amount,   percent, parsePercent);
    addFloat (ParamIDs::impact,   "Impact",        neutral.impact,   percent, parsePercent);
    addFloat (ParamIDs::punch,    "Punch",         neutral.punch,    percent, parsePercent);

    // Width: 0.5 = original image (100 %), 1.0 = 200 %, 0.0 = mono.
    addFloat (ParamIDs::width, "Width", neutral.width,
              [] (float v, int) { return String (roundToInt (v * 200.0f)) + " %"; },
              [] (const String& t) { return jlimit (0.0f, 1.0f, t.getFloatValue() / 200.0f); });

    // Warmth: 0.5 = neutral, above = warmer (more low end, softer top), below = brighter.
    addFloat (ParamIDs::warmth, "Warmth", neutral.warmth,
              [] (float v, int)
              {
                  const int x = roundToInt ((v - 0.5f) * 200.0f);
                  if (x == 0) return String ("Neutral");
                  return x > 0 ? "Warm " + String (x) : "Bright " + String (-x);
              },
              [] (const String& t)
              {
                  const float x = t.retainCharacters ("-0123456789.").getFloatValue();
                  const bool bright = t.containsIgnoreCase ("bright");
                  return jlimit (0.0f, 1.0f, 0.5f + (bright ? -std::abs (x) : x) / 200.0f);
              });

    addFloat (ParamIDs::loudness, "Loudness", neutral.loudness, percent, parsePercent);

    // FINISH MY TRACK: how strongly section edits are pulled towards one consistent sound.
    addFloat (ParamIDs::coherence, "Coherence", 0.5f, percent, parsePercent);

    // Mastering: loudness target + true-peak ceiling.
    {
        StringArray names;
        for (auto& t : loudnessTargets) names.add (t.name);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::target, ParamIDs::version }, "Loudness Target", names, 0));
    }
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::ceiling, ParamIDs::version }, "Ceiling", NormalisableRange<float> (-3.0f, -0.1f, 0.1f), -1.0f,
        AudioParameterFloatAttributes().withLabel ("dBTP")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dBTP"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, ParamIDs::version }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::match,  ParamIDs::version }, "A/B Level Match", true));   // only affects the original while A/B bypassing
    return layout;
}
