#pragma once
#include <JuceHeader.h>
#include "../DSP/Biquad.h"

// One 100 ms measurement frame. Powers are mean squared values over the frame,
// so frames can be summed/weighted later on the message thread.
struct AnalysisFrame
{
    double hostTime = -1.0;    // song position (s) of the frame start, -1 if the host gave none
    float seconds = 0;
    float kPower = 0;          // K-weighted power, summed over channels (BS.1770)
    float peak = 0;            // sample peak, linear
    float midPow = 0, sidePow = 0;
    float lowPow = 0;          // mid < 150 Hz
    float highPow = 0;         // mid > 4 kHz
    float lowSidePow = 0;      // side < 150 Hz (stereo bass)
    float highSidePow = 0;     // side > 4 kHz
    float lPow = 0, rPow = 0, lrCross = 0;
    float onsetDb = 0;         // strongest attack in the frame (fast/slow envelope ratio)
    float subPow = 0;          // mid < 60 Hz
    float lowMidPow = 0;       // mid 200-500 Hz ("mud")
    float presPow = 0;         // mid 2-5 kHz (presence / harshness)
    float clips = 0;           // samples at full scale (|x| >= 0.9995)

    float lufs() const noexcept { return -0.691f + 10.0f * std::log10 (std::max (kPower, 1.0e-12f)); }
};

// Audio-thread analyzer: cheap per-sample measurements, published through lock-free FIFOs.
class AudioAnalyzer
{
public:
    AudioAnalyzer();

    void prepare (double sampleRate);
    void reset();

    // Audio thread. R may be nullptr for mono. blockTime = host song position of the first sample (-1 if unknown).
    void process (const float* L, const float* R, int numSamples, double blockTime = -1.0) noexcept;

    // Consumer side (one consumer thread).
    int readFrames (AnalysisFrame* dest, int maxFrames) noexcept;
    int readSamples (float* dest, int maxSamples) noexcept;

    void setPublishSamples (bool shouldPublish) noexcept { publishSamples = shouldPublish; }

private:
    void finishFrame() noexcept;

    double sr = 44100.0;
    int frameLength = 4410, frameCount = 0;
    double frameStartTime = -1.0;
    bool publishSamples = true;

    KWeighting kw;
    Biquad lowLp, lowLp2, highHp, highHp2, sideLowLp, sideLowLp2, sideHighHp, sideHighHp2;   // 4th-order band splits
    Biquad subLp, subLp2, lmHp, lmLp, prHp, prLp;
    float fastEnv = 0, slowEnv = 0, fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0;

    double accK = 0, accMid = 0, accSide = 0, accLow = 0, accHigh = 0, accLowSide = 0, accHighSide = 0, accL = 0, accR = 0, accLR = 0;
    float accPeak = 0, accOnset = 0;
    double accSub = 0, accLowMid = 0, accPres = 0;
    int accClips = 0;

    static constexpr int frameCapacity = 512;
    juce::AbstractFifo frameFifo { frameCapacity };
    std::array<AnalysisFrame, frameCapacity> frameStorage {};

    static constexpr int sampleCapacity = 1 << 15;
    juce::AbstractFifo sampleFifo { sampleCapacity };
    std::vector<float> sampleStorage;
};
