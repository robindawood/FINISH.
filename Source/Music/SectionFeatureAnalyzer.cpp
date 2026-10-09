#include "SectionFeatureAnalyzer.h"
#include "SectionDetector.h"
#include "../Analysis/AnalysisSession.h"
#include <algorithm>

AudioFeatures SectionFeatureAnalyzer::measure (const std::vector<AnalysisFrame>& grid, const MusicState& m, double from, double to)
{
    const auto a = (size_t) juce::jlimit (0.0, (double) grid.size(), std::floor ((from - m.startTime) / MusicState::frameSec));
    const auto b = (size_t) juce::jlimit (0.0, (double) grid.size(), std::ceil ((to - m.startTime) / MusicState::frameSec));
    std::vector<AnalysisFrame> sub;
    for (size_t i = a; i < b; ++i)
        if (grid[i].lufs() > AnalysisSession::silenceLufs) sub.push_back (grid[i]);
    return AnalysisSession::computeFeatures (sub);
}

std::vector<SectionDNA> SectionFeatureAnalyzer::analyze (const std::vector<AnalysisFrame>& grid, const MusicState& m)
{
    std::vector<SectionDNA> out;
    bool seenPeak = false;

    for (const auto& s : m.sections)
    {
        SectionDNA d;
        d.features = measure (grid, m, s.startTime, s.endTime);
        d.level = s.loudness;

        // Contour statistics inside the section.
        std::vector<float> c;
        for (double t = s.startTime; t < s.endTime; t += MusicState::frameSec)
        {
            const auto i = (size_t) std::max (0.0, (t - m.startTime) / MusicState::frameSec);
            if (i < m.energyCurve.size()) c.push_back (m.energyCurve[i]);
        }
        if (! c.empty())
        {
            auto sorted = c;
            std::sort (sorted.begin(), sorted.end());
            d.peakLevel = sorted[(size_t) ((double) (sorted.size() - 1) * 0.9)];
            const size_t q = std::max<size_t> (1, c.size() / 4);
            double head = 0, tail = 0;
            for (size_t i = 0; i < q; ++i) { head += c[i]; tail += c[c.size() - 1 - i]; }
            d.slopeDb = (float) ((tail - head) / (double) q);
        }
        d.density = d.features.valid ? juce::jlimit (0.0f, 1.0f, (14.0f - d.features.plr) / 10.0f) : 0.0f;

        switch (s.type)
        {
            case SectionType::Intro:  d.role = SectionRole::SetMood; break;
            case SectionType::Verse:  d.role = SectionRole::Carry; break;
            case SectionType::Build:  d.role = SectionRole::Anticipate; break;
            case SectionType::Chorus: d.role = SectionRole::Lift; seenPeak = true; break;
            case SectionType::Drop:   d.role = seenPeak ? SectionRole::NewPeak : SectionRole::FirstPeak; seenPeak = true; break;
            case SectionType::Break:
            case SectionType::Bridge: d.role = SectionRole::Breathe; break;
            case SectionType::Outro:  d.role = SectionRole::Conclude; break;
            case SectionType::Excerpt: d.role = SectionRole::Whole; break;
        }
        out.push_back (d);
    }

    // Normalise against the track so the DNA bars read as "relative to this song".
    float minL = 100, maxL = -100, maxPunch = 0.1f, maxLow = 0.01f, maxAir = 0.001f, maxWidth = 0.01f;
    for (auto& d : out)
    {
        minL = std::min (minL, d.level); maxL = std::max (maxL, d.level);
        maxPunch = std::max (maxPunch, d.features.punchIndex);
        maxLow = std::max (maxLow, d.features.lowRatio);
        maxAir = std::max (maxAir, d.features.highRatio);
        maxWidth = std::max (maxWidth, d.features.sideRatio);
    }
    for (auto& d : out)
    {
        d.nEnergy = maxL - minL > 0.5f ? (d.level - minL) / (maxL - minL) : 0.8f;
        d.nEnergy = 0.08f + 0.92f * juce::jlimit (0.0f, 1.0f, d.nEnergy);
        d.nDensity = d.density;
        d.nPunch = juce::jlimit (0.0f, 1.0f, d.features.punchIndex / maxPunch);
        d.nLow = juce::jlimit (0.0f, 1.0f, d.features.lowRatio / maxLow);
        d.nAir = juce::jlimit (0.0f, 1.0f, d.features.highRatio / maxAir);
        d.nWidth = juce::jlimit (0.0f, 1.0f, d.features.sideRatio / maxWidth);
    }
    return out;
}
