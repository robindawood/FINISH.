#pragma once
#include <JuceHeader.h>
#include "StyleProfilesData.h"

// Spectral measurement for AUTO, computed EXACTLY like the reference profiles (Tools/profiles/analyze.py):
//   mid = (L+R)/2, side = (L-R)/2, Hann-windowed FFT frames of ~0.37 s with 50 % overlap, power spectra summed over the
//   "loud" frames only (frame RMS of the mid >= 40th percentile of all frames), then the same band ratios and the same
//   1/3-octave curve (band power / total mid power, in dB).
// Audio thread: push() into a lock-free FIFO while active. Message thread: pull() builds the frames, result() sums them.
class SpectralCapture
{
public:
    static constexpr int numThirds = 29;

    struct Result
    {
        bool valid = false;
        double seconds = 0, loudSeconds = 0;
        float lowRatio = 0, subRatio = 0, lowMidRatio = 0, presenceRatio = 0, highRatio = 0, sideRatio = 0, lowSideRatio = 0;
        float thirdOctDb[numThirds] {};
        float truePeakDb = -100.0f, samplePeakDb = -100.0f;
    };

    void prepare (double sampleRate);
    void start();                 // message thread: clears and arms the capture
    void stop() { active.store (false); }                 // also used as 'pause': resume() continues the same measurement
    void resume();                // message thread: continue after a pause (the audio before the gap is not joined to the next)
    bool isActive() const { return active.load(); }

    void push (const float* L, const float* R, int n) noexcept;   // audio thread
    void markGap() noexcept;                                      // audio thread: skipped audio (silence) -> discontinuity
    void pull();                                                  // message thread
    Result result() const;                                        // message thread
    double capturedSeconds() const { return (double) totalSamples / sr; }
    void keepLastSeconds (double seconds);                       // message thread: rolling window (live meters)

    // Shared analysis of one finished stereo buffer (offline use and tests).
    static Result analyse (const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    struct FrameRecord
    {
        float rms = 0;
        double midTotal = 0, sideTotal = 0, midLow = 0, sideLow = 0, midSub = 0, midUpper = 0, midLowMid = 0, midPres = 0, midHigh = 0, sideHigh = 0;
        double thirds[numThirds] {};
    };
    void processFrame();
    void processReadyFrames();
    void breakContinuity();
    void trackTruePeak (float l, float r) noexcept;

    double sr = 44100.0;
    int fftOrder = 14, fftSize = 1 << 14, hop = 1 << 13;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, work, midBuf, sideBuf;
    std::vector<int> binBand;                 // per bin: index of its 1/3-octave band, -1 = none
    std::vector<float> binHz;
    std::vector<FrameRecord> frames;
    juce::int64 totalSamples = 0;

    std::atomic<bool> active { false };
    bool gapWritten = false;                  // audio thread only
    static constexpr int fifoSize = 1 << 17;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoL, fifoR;

    // true peak: 4x polyphase sinc interpolation (same design as the limiter's detector)
    static constexpr int tpTaps = 32;
    float tpCoef[4][tpTaps] {};
    float tpHist[2][tpTaps] {};
    int tpPos = 0, tpWarm = 0;              // after a (re)start the interpolator must fill before it measures
    float tpMax = 0, spMax = 0;
};
