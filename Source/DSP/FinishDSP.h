#pragma once
#include <JuceHeader.h>
#include "Biquad.h"
#include "../Params.h"

// Creative performance FX driven by FINISH MY TRACK (0 = off for every field).
struct CreativeFX
{
    float hp = 0, lp = 0, gate = 0, echo = 0, space = 0;
};

// The FINISH processing chain. Realtime-safe: no allocation, no locks after prepare().
//
//   in ─┬─ punch → glue → tone → width (mono bass) → density → creative FX (filters, gate, echo, space)
//       │                                                         │
//       └──────────────── amount (dry/wet) ───────────────────────┴→ section trim → drive → look-ahead
//                                                                       true-peak limiter (ceiling) → out
//   A/B bypass plays the (latency-aligned) dry signal, optionally level-matched to the output.
class FinishDSP
{
public:
    struct Settings
    {
        MacroSettings macros;
        bool bypass = false;
        bool matchAB = true;         // level-match the original while A/B bypassing
        float driveDb = 0.0f;        // gain into the limiter (target loudness estimate or manual)
        bool autoLoudness = false;   // closed loop: trim the drive so the loud passages sit at targetLufs
        float targetLufs = -14.0f;
        float ceilingDb = -1.0f;     // true-peak ceiling
        float trimDb = 0.0f;         // FINISH MY TRACK level move (dips, fades, silence beats)
        float sectionDry = 0.0f;     // 1 = "Keep Original": untouched audio
        CreativeFX fx;
        double bpm = 120.0;          // host tempo (for gate / echo)
        double ppq = -1.0;           // host position in quarter notes at block start (-1 = free running)
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept;

    int getLatencySamples() const noexcept { return lookahead; }

    // Meters (written on audio thread, read anywhere).
    float getInputLoudness() const noexcept      { return inLufs.load(); }    // short-term (3 s)
    float getOutputLoudness() const noexcept     { return outLufs.load(); }   // short-term (3 s)
    float getOutputMomentary() const noexcept    { return outMomentary.load(); }
    float getGainReductionDb() const noexcept    { return grDb.load(); }
    float getTruePeakDb() const noexcept         { return truePeakDb.load(); }
    float getMatchGainDb() const noexcept        { return matchDb.load(); }
    float getLoudnessCorrectionDb() const noexcept { return corrDbOut.load(); }

    static constexpr float defaultCeilingDb = -1.0f;
    static constexpr float ceilingDb = defaultCeilingDb;   // kept for older callers / tests

private:
    void processChunk (float* L, float* R, int n, const Settings& s, double ppqStart) noexcept;
    void updateToneFilters (float warmth) noexcept;

    struct SVF   // TPT state-variable filter, one channel pair
    {
        float g = 0, k = 1.4f, a1 = 0, a2 = 0, a3 = 0;
        float ic1[2] {}, ic2[2] {};
        void set (double sr, double f, float q) noexcept
        {
            g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, f) / sr);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
        inline void tick (float x, int ch, float& lowOut, float& highOut) noexcept
        {
            const float v3 = x - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2 * v1 - ic1[ch];
            ic2[ch] = 2 * v2 - ic2[ch];
            lowOut = v2;
            highOut = x - k * v1 - v2;
        }
    };

    double sr = 44100.0;
    int channels = 2, maxBlock = 512;

    juce::SmoothedValue<float> sAmount, sImpact, sPunch, sWidth, sWarmth, sLoudness, sBypass, sDry, sDrive, sCeil;
    juce::SmoothedValue<float> sHp, sLp, sGate, sEcho, sSpace;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sTrim;

    // Dynamics / tone / width
    float fastEnv = 0, slowEnv = 0, fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0;
    float compGr = 0, compAtt = 0, compRel = 0;
    Biquad lowShelf, highShelf;
    float lastWarmth = -1.0f;
    int toneCounter = 0;
    Biquad sideLowPass, sideLowPass2;

    // Creative FX
    SVF hpf, lpf;
    int fxCounter = 0;
    float gateEnv = 1.0f, gateCoef = 0;
    double freePhase = 0;
    std::vector<float> echoBuf[2];
    int echoPos = 0;
    float echoLp[2] {};
    juce::Reverb reverb;
    std::vector<float> wetL, wetR, dryL, dryR;

    // Look-ahead true-peak limiter (+ matching dry delay)
    int lookahead = 64;
    std::vector<float> laBuf[2], dryDelay[2];
    std::vector<float> reqGain;          // ring of required gains
    std::vector<int> minQueue;           // monotonic queue (ring positions) for the sliding minimum
    int qHead = 0, qTail = 0;
    int laPos = 0;
    float hist[2][3] {};
    float limGain = 1.0f, limAtt = 0, limRel = 0;

    // Loudness meters + A/B match
    KWeighting kIn, kOut;
    float pIn = 0, pOut = 0, pInShort = 0, pOutShort = 0, pOutMom = 0, matchCoef = 0, shortCoef = 0, momCoef = 0;
    float matchGain = 1.0f, matchSmoothCoef = 0;
    float corrDb = 0.0f;                 // closed-loop loudness correction (audio time, deterministic)
    std::atomic<float> corrDbOut { 0.0f };

    std::atomic<float> inLufs { -70.0f }, outLufs { -70.0f }, outMomentary { -70.0f }, grDb { 0.0f }, truePeakDb { -100.0f }, matchDb { 0.0f };
};
