#pragma once
#include <JuceHeader.h>
#include "Biquad.h"
#include "../Params.h"

// The FINISH processing chain. Realtime-safe: no allocation, no locks after prepare().
//
//   dry ──┬──────────────────────────────────────────────┐
//         └─ transient shaper → glue comp → tone → M/S width (mono bass) → density ─ mix(amount) → limiter → level match → bypass xfade
//
// Every stage is mathematically transparent at its neutral setting.
class FinishDSP
{
public:
    struct Settings
    {
        MacroSettings macros;
        bool bypass = false;
        bool levelMatch = true;
        float trimDb = 0.0f;       // FINISH MY TRACK section/transition level move (after level match: deliberate contrast)
        float sectionDry = 0.0f;   // 1 = "Keep Original" section: untouched audio
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept;

    // Meters (written on audio thread, read anywhere).
    float getInputLoudness() const noexcept   { return inLufs.load(); }
    float getOutputLoudness() const noexcept  { return outLufs.load(); }
    float getGainReductionDb() const noexcept { return grDb.load(); }
    float getMatchGainDb() const noexcept     { return matchDb.load(); }

    static constexpr float ceilingDb = -1.0f;

private:
    void updateToneFilters (float warmth) noexcept;

    double sr = 44100.0;
    int channels = 2;

    juce::SmoothedValue<float> sAmount, sImpact, sPunch, sWidth, sWarmth, sLoudness, sBypass, sMatch, sDry;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sTrim;

    // Transient shaper
    float fastEnv = 0, slowEnv = 0, fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0;
    // Glue compressor
    float compGr = 0, compAtt = 0, compRel = 0;
    // Tone
    Biquad lowShelf, highShelf;
    float lastWarmth = -1.0f;
    int toneCounter = 0;
    // Width (mono-bass crossover on the side channel)
    Biquad sideLowPass, sideLowPass2;   // 4th-order (Linkwitz-Riley) crossover
    // Limiter
    float limGain = 1.0f, limRel = 0;
    // Level match (K-weighted power of input vs output)
    KWeighting kIn, kOut;
    float pIn = 0, pOut = 0, pInShort = 0, pOutShort = 0, matchCoef = 0, shortCoef = 0;
    float matchGain = 1.0f, matchSmoothCoef = 0;

    std::atomic<float> inLufs { -70.0f }, outLufs { -70.0f }, grDb { 0.0f }, matchDb { 0.0f };
};
