#pragma once
#include <JuceHeader.h>

// WHALE parameters. IDs are stored in projects and presets: never rename them.
//
// Two layers:
//   - "base" module parameters (spec*, comp*, sat*, width*, tone*, clip*): what AUTO (or a preset, or the user in
//     EXPERT) decided. DOSAGE scales their distance from neutral.
//   - 8 macros (0..1, 0.5 = leave the base alone): quick nudges on top of the base, for the main screen.
namespace PID
{
    // global
    inline constexpr const char* style    = "style";
    inline constexpr const char* target   = "target";
    inline constexpr const char* ceiling  = "ceiling";
    inline constexpr const char* dosage   = "dosage";
    inline constexpr const char* bypass   = "bypass";
    inline constexpr const char* match    = "levelMatch";
    inline constexpr const char* manualDrive = "manualDrive";
    // macros
    inline constexpr const char* mGlue    = "mGlue";
    inline constexpr const char* mPunch   = "mPunch";
    inline constexpr const char* mWarmth  = "mWarmth";
    inline constexpr const char* mAir     = "mAir";
    inline constexpr const char* mWidth   = "mWidth";
    inline constexpr const char* mBass    = "mBass";
    inline constexpr const char* mDetail  = "mDetail";
    inline constexpr const char* mLoud    = "mLoud";
    // module switches
    inline constexpr const char* specOn   = "specOn";
    inline constexpr const char* compOn   = "compOn";
    inline constexpr const char* satOn    = "satOn";
    inline constexpr const char* widthOn  = "widthOn";
    inline constexpr const char* toneOn   = "toneOn";
    inline constexpr const char* clipOn   = "clipOn";
    // module bases
    inline constexpr const char* specGain[4] = { "specGain0", "specGain1", "specGain2", "specGain3" };
    inline constexpr const char* specDyn[4]  = { "specDyn0", "specDyn1", "specDyn2", "specDyn3" };
    inline constexpr const char* specFreq[4] = { "specFreq0", "specFreq1", "specFreq2", "specFreq3" };
    inline constexpr const char* compGlue   = "compGlue";
    inline constexpr const char* compPunch  = "compPunch";
    inline constexpr const char* compDetail = "compDetail";
    inline constexpr const char* satDrive   = "satDrive";
    inline constexpr const char* widthSide  = "widthSide";
    inline constexpr const char* widthMono  = "widthMono";
    inline constexpr const char* toneLow    = "toneLow";
    inline constexpr const char* toneAir    = "toneAir";
    inline constexpr const char* clipAmt    = "clipAmt";
    inline constexpr const char* lowCut     = "lowCut";

    inline constexpr int version = 1;
}

// Styles: index 0 lets AUTO detect the style. The others match the reference profiles (Resources/Profiles).
inline const juce::StringArray& styleNames()
{
    static const juce::StringArray n { juce::String (juce::CharPointer_UTF8 ("Auto (d\xc3\xa9tection)")), "Tech House / House", "Afro / Latin House",
                                       "Hard Techno / Rave", "Drum & Bass", "UK Garage / Bass", "Lo-fi / Chill", "Afro Tech / Riddim", "Afro / Dancehall" };
    return n;
}
inline const juce::StringArray& styleShortNames()
{
    static const juce::StringArray n { "AUTO", "TECH HOUSE", "AFRO HOUSE", "HARD TECHNO", "DNB", "UK GARAGE", "LO-FI", "AFRO TECH", "DANCEHALL" };
    return n;
}
inline constexpr int numStyles = 9;   // append only: projects and presets store the index
inline constexpr int styleAuto = 0, styleTechHouse = 1, styleAfro = 2, styleHardTechno = 3, styleDnb = 4, styleUkg = 5, styleLofi = 6, styleAfroTech = 7, styleDancehall = 8;

// Loudness targets. "Style" = the loud-passage loudness measured on the style's reference masters.
struct LoudnessTarget { const char* name; const char* shortName; float lufs; };
inline constexpr LoudnessTarget loudnessTargets[] = {
    { "Style",         "STYLE",       0.0f },
    { "Streaming -14", "STREAM -14",  -14.0f },
    { "Balanced -11",  "BALANCED -11", -11.0f },
    { "Club -8",       "CLUB -8",     -8.0f },
    { "Loud -6",       "LOUD -6",     -6.0f },
    { "Same level",    "SAME LEVEL",  0.0f },
    { "Manual",        "MANUAL",      0.0f } };
inline constexpr int numLoudnessTargets = 7;
inline constexpr int styleTarget = 0, sameLevelTarget = 5, manualTarget = 6;

// Dynamic EQ bands of the SPECTRAL module.
// hz = default centre; lo..hi = range the centre can be moved in (AUTO places it where the mix needs it).
struct SpecBand { const char* name; float hz; float q; float lo; float hi; };
inline constexpr SpecBand specBands[4] = { { "SUB", 60.0f, 0.9f, 35.0f, 160.0f }, { "LOW-MID", 300.0f, 1.0f, 150.0f, 800.0f },
                                           { "PRESENCE", 3500.0f, 1.1f, 1500.0f, 6000.0f }, { "HIGHS", 8000.0f, 1.0f, 5000.0f, 14000.0f } };

// Base values of the modules (what AUTO / presets / EXPERT write). Default = neutral: WHALE is transparent apart
// from the safety limiter.
struct ModuleBase
{
    float specGain[4] {}, specDyn[4] {};
    float specFreq[4] { 60.0f, 300.0f, 3500.0f, 8000.0f };   // = specBands[].hz
    float compGlue = 0, compPunch = 0, compDetail = 0.5f, satDrive = 0;
    float widthSide = 0, widthMono = 0, toneLow = 0, toneAir = 0, clipAmt = 0;
    float lowCut = 0;   // Hz, 0 = off (low-cut of the BASSES/AIR module: rumble under the kick)
};

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const ModuleBase neutral;

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto parsePct = [] (const String& t) { return jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f); };
    auto db = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
    auto unit01 = [&] (const char* id, const String& name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, PID::version }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (parsePct)));
    };
    auto dbParam = [&] (const char* id, const String& name, float lo, float hi, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, PID::version }, name, NormalisableRange<float> (lo, hi, 0.01f), def,
                        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    };
    auto onOff = [&] (const char* id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, PID::version }, name, def));
    };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::style, PID::version }, "Style", styleNames(), styleAuto));
    {
        StringArray names;
        for (auto& t : loudnessTargets) names.add (t.name);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::target, PID::version }, "Loudness Target", names, styleTarget));
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { PID::ceiling, PID::version }, "Ceiling",
                    NormalisableRange<float> (-3.0f, -0.1f, 0.1f), -1.0f,
                    AudioParameterFloatAttributes().withLabel ("dBTP").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dBTP"; })));
    unit01 (PID::dosage, "Dosage", 1.0f);   // 100 % = the correction AUTO / the preset recommends
    dbParam (PID::manualDrive, "Manual Drive", -6.0f, 24.0f, 0.0f);
    onOff (PID::bypass, "Bypass (A/B)", false);
    onOff (PID::match, "A/B Level Match", true);

    unit01 (PID::mGlue,   "Glue", 0.5f);
    unit01 (PID::mPunch,  "Punch", 0.5f);
    unit01 (PID::mWarmth, "Warmth", 0.5f);
    unit01 (PID::mAir,    "Air", 0.5f);
    unit01 (PID::mWidth,  "Width", 0.5f);
    unit01 (PID::mBass,   "Bass", 0.5f);
    unit01 (PID::mDetail, "Detail", 0.5f);
    unit01 (PID::mLoud,   "Loudness", 0.5f);

    onOff (PID::specOn,  "Spectral On", true);
    onOff (PID::compOn,  "Compression On", true);
    onOff (PID::satOn,   "Saturation On", true);
    onOff (PID::widthOn, "Width On", true);
    onOff (PID::toneOn,  "Bass/Air On", true);
    onOff (PID::clipOn,  "Clipper On", true);

    const char* specNames[4] = { "Spectral Sub", "Spectral Low-Mid", "Spectral Presence", "Spectral Highs" };
    for (int b = 0; b < 4; ++b)
    {
        dbParam (PID::specGain[b], specNames[b], -6.0f, 6.0f, neutral.specGain[b]);
        unit01 (PID::specDyn[b], String (specNames[b]) + " Dynamic", neutral.specDyn[b]);
        {
            NormalisableRange<float> range (specBands[b].lo, specBands[b].hi, 1.0f);
            range.setSkewForCentre (specBands[b].hz);
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { PID::specFreq[b], PID::version }, String (specNames[b]) + " Freq", range, neutral.specFreq[b],
                            AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
                            { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; })));
        }
    }
    unit01 (PID::compGlue,   "Compression Glue", neutral.compGlue);
    unit01 (PID::compPunch,  "Compression Punch", neutral.compPunch);
    unit01 (PID::compDetail, "Compression Detail", neutral.compDetail);
    unit01 (PID::satDrive,   "Saturation Drive", neutral.satDrive);
    dbParam (PID::widthSide, "Width Sides", -6.0f, 4.0f, neutral.widthSide);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { PID::widthMono, PID::version }, "Mono Bass Below",
                    NormalisableRange<float> (0.0f, 250.0f, 1.0f), neutral.widthMono,
                    AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v < 1 ? String ("Off") : String (roundToInt (v)) + " Hz"; })));
    dbParam (PID::toneLow, "Bass Shelf", -6.0f, 6.0f, neutral.toneLow);
    dbParam (PID::toneAir, "Air Shelf", -6.0f, 6.0f, neutral.toneAir);
    unit01 (PID::clipAmt, "Clipper", neutral.clipAmt);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { PID::lowCut, PID::version }, "Low Cut",
                    NormalisableRange<float> (0.0f, 60.0f, 1.0f), neutral.lowCut,
                    AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v < 1 ? String ("Off") : String (roundToInt (v)) + " Hz"; })));
    return layout;
}
