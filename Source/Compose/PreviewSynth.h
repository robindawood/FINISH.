#pragma once
#include "ComposeTypes.h"

// Small built-in instrument rack so generated parts can be heard inside FINISH before they go to the DAW:
// drum kit (GM map), bass, stab chords, lead, pad, and procedural transition FX (risers, impacts...).
// Realtime-safe: the sequence is handed over with a try-lock, voices are preallocated.
class PreviewSynth
{
public:
    struct SeqNote { double beat, length; juce::uint8 lane, pitch; float velocity; };
    struct SeqFx { double beat, length; FxKind kind; float level; };
    struct Sequence
    {
        std::vector<SeqNote> notes;                     // sorted by beat, absolute beats
        std::vector<SeqFx> fx;
        std::array<std::vector<CCPoint>, numLanes> cc;  // absolute beats
        double lengthBeats = 0.0;
    };
    static Sequence compile (const Arrangement& arr, int onlySection = -1);   // -1 = whole arrangement

    void prepare (double sampleRate, int maxBlock);
    void setSequence (const Sequence& s);                // message thread

    // Transport (message thread)
    void play (double fromBeat, double loopFrom, double loopTo);
    void stop();
    bool isPlaying() const noexcept { return playing.load(); }
    double getPositionBeats() const noexcept { return position.load(); }
    void setLaneMuted (Lane l, bool muted) { laneMute[(size_t) l].store (muted); }
    bool isLaneMuted (Lane l) const noexcept { return laneMute[(size_t) l].load(); }

    // Audio thread: adds the preview into L/R.
    void render (float* L, float* R, int n, double bpm) noexcept;

    // Offline helpers (message/worker thread)
    static juce::AudioBuffer<float> renderOffline (const Sequence& s, double bpm, double sampleRate, double fromBeat, double toBeat);
    static juce::AudioBuffer<float> renderFx (FxKind kind, double lengthBeats, double bpm, double sampleRate, float level);

    static constexpr int maxVoices = 64;

private:
    struct Filter
    {
        float ic1[2] {}, ic2[2] {};
        inline float lp (float x, int ch, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2[ch], v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2 * v1 - ic1[ch]; ic2[ch] = 2 * v2 - ic2[ch];
            return v2;
        }
        inline float bp (float x, int ch, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2[ch], v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2 * v1 - ic1[ch]; ic2[ch] = 2 * v2 - ic2[ch];
            return v1;
        }
        inline float hp (float x, int ch, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2[ch], v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2 * v1 - ic1[ch]; ic2[ch] = 2 * v2 - ic2[ch];
            return x - k * v1 - v2;
        }
        void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
    };

    struct Voice
    {
        bool active = false;
        int lane = 0, pitch = 0;
        FxKind fx = FxKind::None;
        float vel = 0;
        double t = 0;              // seconds since start
        double gateLen = 0;        // seconds held
        double fxLen = 0;
        double phase[3] {};
        float env = 0;
        bool released = false;
        float relLevel = 0; double relT = 0;
        juce::uint32 noise = 22222;
        Filter f1, f2;
        float pan = 0;
    };

    void startNote (int lane, int pitch, float vel, double lengthSec) noexcept;
    void startFx (FxKind k, double lengthSec, float level) noexcept;
    void renderVoice (Voice& v, float* L, float* R, float* SL, float* SR, int n, const float* cutoff) noexcept;
    float ccAt (int lane, double beat) const noexcept;

    double sr = 44100.0;
    std::array<Voice, maxVoices> voices;
    juce::SpinLock seqLock;
    Sequence pending, active;
    std::atomic<int> pendingVersion { 0 };
    int activeVersion = -1;
    size_t nextNote = 0, nextFx = 0;

    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 }, loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<int> jumpRequest { 0 };
    std::atomic<double> jumpTo { 0.0 };
    int jumpSeen = 0;
    double pos = 0.0;
    std::array<std::atomic<bool>, numLanes> laneMute {};
    juce::Reverb reverb;
    std::vector<float> sendL, sendR;
};
