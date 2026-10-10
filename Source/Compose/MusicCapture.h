#pragma once
#include <JuceHeader.h>

// Records what the DAW plays, indexed by song position, at a reduced rate (~11 kHz mono) for musical analysis
// (key, groove, bass, melody). Audio thread: push() only (no locks, no allocation). Message thread: snapshot().
class MusicCapture
{
public:
    static constexpr double maxSeconds = 600.0;   // 10 minutes of song timeline
    static constexpr double chunkSeconds = 0.05;

    void prepare (double sampleRate);              // allocates; call from prepareToPlay
    void clear();                                  // message thread

    // Audio thread. songTime < 0 or !playing = not recorded.
    void push (const float* L, const float* R, int n, double songTime, double ppq, double bpm, bool playing) noexcept;

    struct Snapshot
    {
        std::vector<float> audio;
        double rate = 11025.0;
        double startTime = 0.0;    // song time of audio[0]
        double bpm = 120.0;
        double beatAtStart = 0.0;  // quarter-note position of audio[0] (bar grid = multiples of 4)
        bool tempoKnown = false;
        bool valid() const noexcept { return audio.size() > (size_t) (rate * 2.0); }
        double seconds() const noexcept { return (double) audio.size() / rate; }
    };

    // The last contiguous stretch the user played (or [from, to] if given).
    Snapshot snapshot (double from = -1.0, double to = -1.0) const;
    double getLastRunSeconds() const noexcept { return std::max (0.0, runEnd.load() - runStart.load()); }
    double getLastRunStart() const noexcept { return runStart.load(); }
    double getLastRunEnd() const noexcept { return runEnd.load(); }
    double getRate() const noexcept { return rate; }

private:
    std::vector<float> data;
    std::unique_ptr<std::atomic<juce::uint8>[]> covered;
    size_t numChunks = 0;
    double rate = 11025.0, hostRate = 44100.0;
    int decim = 4;

    // anti-alias low-pass (2 cascaded biquads, Butterworth) before decimation
    struct Biq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                 inline float tick (float x) noexcept { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Biq aa1, aa2;

    // audio-thread accumulator
    long long accIndex = -1;
    float accSum = 0.0f;
    int accCount = 0;
    double lastEndTime = -1.0;

    std::atomic<double> runStart { 0.0 }, runEnd { 0.0 }, bpmA { 120.0 }, beatOffset { 0.0 };
    std::atomic<bool> tempoKnown { false };
};
