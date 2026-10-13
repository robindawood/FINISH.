#pragma once
#include <JuceHeader.h>
#include "Biquad.h"
#include "../Params.h"

// The WHALE mastering chain. Realtime-safe: no allocation, no locks after prepare().
//
//   in ─┬─ LOW-CUT → SPECTRAL (4 dynamic bells) → COMPRESSION (punch + 3-band leveller) → BASSES/AIR (shelves)
//       │   → LARGEUR (sides + mono bass) → [x4 oversampled: SATURATION → drive → CLIPPER] → band-limit
//       │   → LIMITEUR (look-ahead, 4x true-peak detection) → out
//       └──────────────── dry (latency-aligned) ─────── A/B bypass, optionally level-matched
class WhaleDSP
{
public:
    // Effective values (macros, dosage and switches already applied by the processor).
    struct Settings
    {
        bool bypass = false, matchAB = true;
        float specGainDb[4] {}, specDyn[4] {};
        float specHz[4] { 60.0f, 300.0f, 3500.0f, 8000.0f };
        float punch = 0, glue = 0, detail = 0.5f;
        float satDrive = 0;
        float sideDb = 0, monoHz = 0;
        float lowDb = 0, airDb = 0;
        float lowCutHz = 0;           // 0 = off
        float clip = 0;
        float driveDb = 0;            // gain into the clipper/limiter
        bool autoLoudness = false;    // closed loop: loud passages pulled onto targetLufs
        float targetLufs = -9.0f;
        float loudInputLufs = -100.0f; // input short-term level of the loud passages: the loop only learns there
        float ceilingDb = -1.0f;
    };

    static constexpr int numVisBands = 8;   // spectrum bands for the visual (whale lines)

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const Settings& s) noexcept;

    int getLatencySamples() const noexcept { return osLatency + lookahead; }

    // Meters (written on the audio thread, read anywhere).
    float getInputLoudness() const noexcept      { return inLufs.load(); }     // short-term (~3 s)
    float getOutputLoudness() const noexcept     { return outLufs.load(); }
    float getOutputMomentary() const noexcept    { return outMomentary.load(); }
    float getGainReductionDb() const noexcept    { return grDb.load(); }
    float getTruePeakDb() const noexcept         { return truePeakDb.load(); }
    float takeMaxPeakDb() noexcept               { return maxPeakDb.exchange (-100.0f); }
    float getMatchGainDb() const noexcept        { return matchDb.load(); }
    float getLoudnessCorrectionDb() const noexcept { return corrDbOut.load(); }
    float getBandGrDb (int b) const noexcept     { return bandGrOut[(size_t) juce::jlimit (0, 2, b)].load(); }
    float getSpecDynDb (int b) const noexcept    { return specDynOut[(size_t) juce::jlimit (0, 3, b)].load(); }
    float getClipDb() const noexcept             { return clipOut.load(); }
    float getCorrelation() const noexcept        { return corrOut.load(); }
    float getWidth() const noexcept              { return widthOut.load(); }      // side / (mid + side), output
    float getVisBand (int b) const noexcept      { return visOut[(size_t) juce::jlimit (0, numVisBands - 1, b)].load(); }   // dB
    float getKick() const noexcept               { return kickOut.load(); }       // 0..1 low-end hit envelope
    static float visBandHz (int b) noexcept;

private:
    void stageA (float* L, float* R, int n, const Settings& s) noexcept;
    void stageB (juce::dsp::AudioBlock<float>& block, const Settings& s) noexcept;
    void stageC (float* L, float* R, int n, const Settings& s) noexcept;

    double sr = 44100.0;
    int channels = 2, maxBlock = 512;

    juce::SmoothedValue<float> sPunch, sGlue, sLevOn, sSat, sSide, sMono, sClip, sDrive, sCeil, sBypass;
    juce::SmoothedValue<float> sSpecGain[4], sSpecDyn[4], sLow, sAir;

    // SPECTRAL: detector band-pass + bell per band
    Biquad specDet[4], specBell[4];
    float specEnv[4] {}, specAvg[4] {}, specCut[4] {}, lastSpecGain[4] {};
    float specHzNow[4] { 60.0f, 300.0f, 3500.0f, 8000.0f };   // glides to Settings::specHz
    bool specRedesign[4] {};
    float specAtt = 0, specRel = 0, specAvgCoef = 0;
    int specCounter = 0;
    std::array<std::atomic<float>, 4> specDynOut {};

    // COMPRESSION: transient shaper + 3-band leveller (LR4 at 120 Hz / 4 kHz, phase-coherent sum)
    float fastEnv = 0, slowEnv = 0, fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0;
    // PUNCH v2: kick-band detector (< 150 Hz) + broadband detector; boosts the attack and lowers the sustain
    Biquad punchLp, punchSplit;
    float kFast = 0, kSlow = 0, kFastAtt = 0, kFastRel = 0, kSlowAtt = 0, kSlowRel = 0, punchGain = 1.0f, punchGainHi = 1.0f, punchSmooth = 0;
    // SATURATION v2 (oversampled): pre-emphasis low shelf (lows saturate more than highs), inverse after
    Biquad satPre, satPost, satSplit;
    float satEnv = 0.0f, satEnvCoef = 0.0f;
    Biquad xLow1a, xLow1b, xHigh1a, xHigh1b, xLow2a, xLow2b, xHigh2a, xHigh2b, apLowA, apLowB, apHighA, apHighB;
    float bandEnv[3] {}, bandRms[3] {}, bandGr[3] {};
    float bandAtt = 0, bandRel = 0, bandRmsCoef = 0, bandGrSmooth = 0, lastDetail = -1.0f;
    std::array<std::atomic<float>, 3> bandGrOut {};

    // Low-cut: 6th-order Butterworth high-pass (36 dB/oct: removes rumble, leaves the kick an octave up untouched) in double precision (a 20-60 Hz corner at 44.1-192 kHz needs it),
    // cross-faded in/out so switching it is click-free.
    struct HpD
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
        void set (double sr, double f, double q) noexcept;
        inline double process (double x, int c) noexcept { const double y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return y; }
        void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };
    HpD lowCutA, lowCutB, lowCutC;
    float lowCutHz = 0.0f, lowCutTargetHz = 0.0f;
    juce::SmoothedValue<float> sLowCutOn;

    // BASSES / AIR
    Biquad shelfLow, shelfAir;
    float lastLow = 1000.0f, lastAir = 1000.0f;
    int toneCounter = 0;

    // LARGEUR
    Biquad sideHpA, sideHpB;
    float lastMonoHz = -1.0f;

    // x4 oversampled saturation + clipper
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    int osLatency = 0;
    std::atomic<float> clipOut { 0.0f };

    // Band-limit below Nyquist + true-peak detector + look-ahead limiter
    Biquad nyqA, nyqB, dcBlock;
    juce::SmoothedValue<float> sNl;
    static constexpr int tpTaps = 32;
    float tpCoef[4][tpTaps] {};
    float tpHist[2][tpTaps] {};
    int tpPos = 0;
    int lookahead = 64;
    std::vector<float> laBuf[2];
    std::vector<float> reqGain;
    std::vector<int> minQueue;
    int qHead = 0, qTail = 0, laPos = 0;
    float limGain = 1.0f, limRel = 0;
    std::vector<float> rampBuf;
    double rampSum = 0.0;
    int rampPos = 0, rampLen = 1;

    // Dry path (aligned with the full latency) for A/B and metering
    std::vector<float> dryRing[2];
    int dryPos = 0, dryLen = 1;
    std::vector<float> dryNow[2], monoWork;

    // Loudness + A/B match + output analysis for the visuals
    KWeighting kIn, kOut;
    float pIn = 0, pOut = 0, pInShort = 0, pOutShort = 0, pOutMom = 0, matchCoef = 0, shortCoef = 0, momCoef = 0;
    float matchGain = 1.0f, matchSmoothCoef = 0, peakHoldDb = -100.0f, corrDb = 0.0f;
    float accLR = 0, accLL = 0, accRR = 0, accMid = 0, accSide = 0, statCoef = 0;
    Biquad visBp[numVisBands];
    float visEnv[numVisBands] {}, visCoef = 0, kickEnv = 0, kickAvg = 0, kickCoef = 0, kickAvgCoef = 0;
    Biquad kickLp;
    std::array<std::atomic<float>, numVisBands> visOut {};
    std::atomic<float> kickOut { 0.0f }, corrOut { 1.0f }, widthOut { 0.0f };
    std::atomic<float> corrDbOut { 0.0f }, maxPeakDb { -100.0f };
    std::atomic<float> inLufs { -70.0f }, outLufs { -70.0f }, outMomentary { -70.0f }, grDb { 0.0f }, truePeakDb { -100.0f }, matchDb { 0.0f };
};
