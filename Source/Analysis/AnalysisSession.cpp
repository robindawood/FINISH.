#include "AnalysisSession.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
    inline float toLufs (double p) { return -0.691f + 10.0f * (float) std::log10 (std::max (p, 1.0e-12)); }

    float percentile (std::vector<float> v, float p)
    {
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        const float idx = p * (float) (v.size() - 1);
        const size_t lo = (size_t) std::floor (idx), hi = std::min (lo + 1, v.size() - 1);
        const float t = idx - (float) lo;
        return v[lo] + t * (v[hi] - v[lo]);
    }
}

std::vector<float> AnalysisSession::shortTermLoudness (const std::vector<AnalysisFrame>& frames, int windowFrames)
{
    std::vector<float> out;
    out.reserve (frames.size());
    double acc = 0;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        acc += frames[i].kPower;
        if (i >= (size_t) windowFrames) acc -= frames[i - (size_t) windowFrames].kPower;
        const auto count = (double) std::min (i + 1, (size_t) windowFrames);
        out.push_back (toLufs (acc / count));
    }
    return out;
}

AudioFeatures AnalysisSession::computeFeatures (const std::vector<AnalysisFrame>& frames)
{
    AudioFeatures f;
    if (frames.empty()) return f;

    // Gated integrated loudness: absolute gate -70 LUFS, then relative gate -10 LU.
    double sumP = 0, sumW = 0;
    for (auto& fr : frames)
        if (fr.lufs() > -70.0f) { sumP += fr.kPower * fr.seconds; sumW += fr.seconds; }
    if (sumW <= 0) return f;
    const float ungated = toLufs (sumP / sumW);

    double gP = 0, gW = 0, mid = 0, side = 0, low = 0, high = 0, lowSide = 0, highSide = 0, l = 0, r = 0, lr = 0;
    double dur = 0;
    std::vector<float> onsets;
    float peak = 0;
    for (auto& fr : frames)
    {
        dur += fr.seconds;
        peak = std::max (peak, fr.peak);
        if (fr.lufs() <= ungated - 10.0f) continue;
        gP += fr.kPower * fr.seconds; gW += fr.seconds;
        mid += fr.midPow; side += fr.sidePow; low += fr.lowPow; high += fr.highPow; lowSide += fr.lowSidePow; highSide += fr.highSidePow;
        l += fr.lPow; r += fr.rPow; lr += fr.lrCross;
        onsets.push_back (fr.onsetDb);
    }

    f.valid = true;
    f.durationSec = (float) dur;
    f.integratedLufs = gW > 0 ? toLufs (gP / gW) : ungated;
    f.peakDb = 20.0f * std::log10 (std::max (peak, 1.0e-6f));
    f.plr = f.peakDb - f.integratedLufs;
    const double total = mid + side;
    f.lowRatio = total > 0 ? (float) ((low + lowSide) / total) : 0.0f;
    f.highRatio = total > 0 ? (float) ((high + highSide) / total) : 0.0f;
    f.sideRatio = (mid + side) > 0 ? (float) (side / (mid + side)) : 0.0f;
    f.lowSideRatio = (low + lowSide) > 0 ? (float) (lowSide / (low + lowSide)) : 0.0f;
    f.correlation = (l > 0 && r > 0) ? (float) (lr / std::sqrt (l * r)) : 1.0f;
    // Punch = strength of the real hits: mean of the strongest 30 % of frame onsets.
    if (! onsets.empty())
    {
        std::sort (onsets.begin(), onsets.end(), std::greater<float>());
        const size_t top = std::max<size_t> (1, onsets.size() * 3 / 10);
        double acc = 0;
        for (size_t i = 0; i < top; ++i) acc += onsets[i];
        f.punchIndex = (float) (acc / (double) top);
    }

    // Loudness range from 3 s short-term values (absolute -70, relative -20 gates).
    auto st = shortTermLoudness (frames);
    std::vector<float> gated;
    for (auto v : st) if (v > -70.0f && v > f.integratedLufs - 20.0f) gated.push_back (v);
    f.loudnessRange = gated.size() > 10 ? percentile (gated, 0.95f) - percentile (gated, 0.10f) : 0.0f;
    return f;
}
