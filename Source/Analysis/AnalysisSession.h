#pragma once
#include "AudioAnalyzer.h"
#include "AudioFeatures.h"
#include <vector>

// Collects analysis frames while the user plays the track (message thread only).
class AnalysisSession
{
public:
    static constexpr float silenceLufs = -60.0f;
    static constexpr size_t maxFrames = 20 * 60 * 10;   // 20 minutes of 100 ms frames

    void clear() { frames.clear(); }
    void add (const AnalysisFrame& f)
    {
        if (f.lufs() > silenceLufs && frames.size() < maxFrames)
            frames.push_back (f);
    }

    double getDuration() const
    {
        double d = 0;
        for (auto& f : frames) d += f.seconds;
        return d;
    }

    const std::vector<AnalysisFrame>& getFrames() const { return frames; }
    AudioFeatures computeFeatures() const { return computeFeatures (frames); }

    static AudioFeatures computeFeatures (const std::vector<AnalysisFrame>& frames);
    // 3 s short-term loudness per frame (LUFS), used for loudness range and the timeline.
    static std::vector<float> shortTermLoudness (const std::vector<AnalysisFrame>& frames, int windowFrames = 30);

private:
    std::vector<AnalysisFrame> frames;
};
