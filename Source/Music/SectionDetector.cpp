#include "SectionDetector.h"
#include <algorithm>
#include <map>

namespace
{
    float percentile (std::vector<float> v, float p)
    {
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (float) (v.size() - 1)))];
    }

    struct Run { size_t begin, end; int cls; };

    float mean (const std::vector<float>& c, size_t a, size_t b)
    {
        a = std::min (a, c.size()); b = std::min (b, c.size());
        if (b <= a) return -70.0f;
        double acc = 0;
        for (size_t i = a; i < b; ++i) acc += c[i];
        return (float) (acc / (double) (b - a));
    }

    // Texture novelty: how different the 4 s before a frame are from the 4 s after it
    // (level, low end, top end, attacks, width). Peaks = places where the arrangement changes.
    std::vector<float> novelty (const std::vector<AnalysisFrame>& f, int W)
    {
        const size_t n = f.size();
        std::vector<double> pk (n + 1), pm (n + 1), ps (n + 1), pl (n + 1), ph (n + 1), po (n + 1), pc (n + 1);
        for (size_t i = 0; i < n; ++i)
        {
            const bool audible = f[i].kPower > 1.0e-7f;
            pk[i + 1] = pk[i] + f[i].kPower;
            pm[i + 1] = pm[i] + f[i].midPow;
            ps[i + 1] = ps[i] + f[i].sidePow;
            pl[i + 1] = pl[i] + f[i].lowPow + f[i].lowSidePow;
            ph[i + 1] = ph[i] + f[i].highPow + f[i].highSidePow;
            po[i + 1] = po[i] + (audible ? f[i].onsetDb : 0.0f);
            pc[i + 1] = pc[i] + (audible ? 1.0 : 0.0);
        }
        struct Win { double level, low, high, onset, side; };
        auto win = [&] (size_t a, size_t b) -> Win
        {
            const double cnt = (double) (b - a);
            const double tot = (pm[b] - pm[a]) + (ps[b] - ps[a]) + 1.0e-12;
            const double aud = std::max (1.0, pc[b] - pc[a]);
            return { 10.0 * std::log10 ((pk[b] - pk[a]) / cnt + 1.0e-12), (pl[b] - pl[a]) / tot,
                     std::log10 ((ph[b] - ph[a]) / tot + 1.0e-5), (po[b] - po[a]) / aud, (ps[b] - ps[a]) / tot };
        };
        std::vector<float> out (n, 0.0f);
        for (size_t i = (size_t) W; i + (size_t) W <= n; ++i)
        {
            const auto a = win (i - (size_t) W, i), b = win (i, i + (size_t) W);
            const double d = std::abs (a.level - b.level) / 3.0 + std::abs (a.low - b.low) / 0.1
                           + std::abs (a.high - b.high) / 0.3 + std::abs (a.onset - b.onset) / 1.5
                           + std::abs (a.side - b.side) / 0.05;
            out[i] = (float) d;
        }
        return out;
    }

    SectionType alternativeFor (SectionType t)
    {
        switch (t)
        {
            case SectionType::Drop:   return SectionType::Chorus;
            case SectionType::Chorus: return SectionType::Drop;
            case SectionType::Break:  return SectionType::Bridge;
            case SectionType::Bridge: return SectionType::Break;
            case SectionType::Verse:  return SectionType::Build;
            case SectionType::Build:  return SectionType::Verse;
            case SectionType::Intro:  return SectionType::Verse;
            case SectionType::Outro:  return SectionType::Break;
            case SectionType::Excerpt: break;
        }
        return t;
    }
}

std::vector<float> SectionDetector::contour (const std::vector<AnalysisFrame>& frames)
{
    const int n = (int) frames.size();
    std::vector<float> c ((size_t) n);
    // Running sum for a centred 3 s window (±15 frames).
    std::vector<double> prefix ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + frames[(size_t) i].kPower;
    for (int i = 0; i < n; ++i)
    {
        const int a = std::max (0, i - 15), b = std::min (n, i + 16);
        const double p = (prefix[(size_t) b] - prefix[(size_t) a]) / (double) (b - a);
        c[(size_t) i] = std::max (-70.0f, -0.691f + 10.0f * (float) std::log10 (std::max (p, 1.0e-12)));
    }
    return c;
}

float SectionDetector::meanLevel (const MusicState& m, double from, double to)
{
    const auto a = (size_t) std::max (0.0, std::floor ((from - m.startTime) / MusicState::frameSec));
    const auto b = (size_t) std::max (0.0, std::ceil ((to - m.startTime) / MusicState::frameSec));
    return mean (m.energyCurve, a, std::max (a + 1, b));
}

void SectionDetector::updateLoudness (MusicState& m)
{
    for (auto& s : m.sections) s.loudness = meanLevel (m, s.startTime, s.endTime);
}

void SectionDetector::relabel (std::vector<Section>& sections)
{
    std::map<SectionType, int> counts, totals;
    for (auto& s : sections) totals[s.type]++;
    for (auto& s : sections)
    {
        const int k = ++counts[s.type];
        s.label = sectionTypeName (s.type);
        if (totals[s.type] > 1 && s.type != SectionType::Intro && s.type != SectionType::Outro)
            s.label << " " << k;
    }
}

MusicState SectionDetector::analyze (const std::vector<AnalysisFrame>& frames, double startTime) const
{
    MusicState m;
    m.startTime = startTime;
    if (frames.empty()) return m;

    m.durationSec = (double) frames.size() * MusicState::frameSec;
    m.energyCurve = contour (frames);
    const int n = (int) frames.size();
    auto timeOf = [&] (size_t i) { return startTime + (double) i * MusicState::frameSec; };

    // Only audible frames shape the thresholds.
    std::vector<float> audible;
    for (auto v : m.energyCurve) if (v > -60.0f) audible.push_back (v);
    if (audible.empty()) audible = m.energyCurve;

    m.contrastDb = percentile (audible, 0.90f) - percentile (audible, 0.20f);

    if (m.durationSec < 30.0)
    {
        Section s; s.startTime = startTime; s.endTime = timeOf ((size_t) n);
        s.type = s.alternative = SectionType::Excerpt; s.label = "EXCERPT";
        s.confidence = 1.0f;
        m.sections.push_back (s);
        updateLoudness (m);
        return m;
    }

    // Thresholds relative to the track's own loud level, so long drops are still "high".
    const float top = percentile (audible, 0.90f);
    const float bottom = percentile (audible, 0.15f);
    const float range = top - bottom;
    const float hi = top - std::max (1.5f, 0.3f * range);
    const float lo = bottom + 0.35f * range;
    const bool flat = range < 2.5f;

    std::vector<Run> runs;
    for (int i = 0; i < n; ++i)
    {
        const float v = m.energyCurve[(size_t) i];
        const int cls = flat ? 1 : (v >= hi ? 2 : (v <= lo ? 0 : 1));
        if (runs.empty() || runs.back().cls != cls) runs.push_back ({ (size_t) i, (size_t) i + 1, cls });
        else runs.back().end = (size_t) i + 1;
    }

    // Merge runs shorter than 8 s into the neighbour with the closest level, then fuse identical neighbours.
    const size_t minLen = 80;
    for (bool changed = true; changed && runs.size() > 1;)
    {
        changed = false;
        size_t shortest = 0, shortestLen = SIZE_MAX;
        for (size_t i = 0; i < runs.size(); ++i)
            if (runs[i].end - runs[i].begin < shortestLen) { shortestLen = runs[i].end - runs[i].begin; shortest = i; }
        if (shortestLen < minLen)
        {
            const size_t i = shortest;
            const float self = mean (m.energyCurve, runs[i].begin, runs[i].end);
            const float prevD = i > 0 ? std::abs (self - mean (m.energyCurve, runs[i - 1].begin, runs[i - 1].end)) : 1.0e9f;
            const float nextD = i + 1 < runs.size() ? std::abs (self - mean (m.energyCurve, runs[i + 1].begin, runs[i + 1].end)) : 1.0e9f;
            if (prevD <= nextD) { runs[i - 1].end = runs[i].end; runs.erase (runs.begin() + (long) i); }
            else               { runs[i + 1].begin = runs[i].begin; runs.erase (runs.begin() + (long) i); }
            changed = true;
        }
        for (size_t i = 1; i < runs.size();)
        {
            if (runs[i].cls == runs[i - 1].cls) { runs[i - 1].end = runs[i].end; runs.erase (runs.begin() + (long) i); changed = true; }
            else ++i;
        }
    }

    // ---- Split long runs where the texture changes (drums stop, bass drops out...) even if loudness barely moves.
    const auto nov = novelty (frames, 40);
    const float noveltyThreshold = 2.5f;
    for (bool split = true; split;)
    {
        split = false;
        for (size_t r = 0; r < runs.size() && ! split; ++r)
        {
            const auto run = runs[r];
            if (run.end - run.begin < 2 * minLen) continue;
            size_t best = 0; float bestV = 0;
            for (size_t i = run.begin + minLen; i + minLen <= run.end; ++i)
            {
                // Only abrupt changes count: a gradual ramp gives a plateau, not a peak.
                const float around = std::max (i >= 40 ? nov[i - 40] : 0.0f, i + 40 < nov.size() ? nov[i + 40] : 0.0f);
                if (nov[i] > bestV && nov[i] - around >= 0.5f * nov[i]) { bestV = nov[i]; best = i; }
            }
            if (bestV < noveltyThreshold) continue;
            runs[r].end = best;
            runs.insert (runs.begin() + (long) r + 1, Run { best, run.end, run.cls });
            split = true;
        }
    }
    // Re-classify every run from its own mean level.
    for (auto& run : runs)
    {
        const float lv = mean (m.energyCurve, run.begin, run.end);
        run.cls = flat ? 1 : (lv >= hi ? 2 : (lv <= lo ? 0 : 1));
    }
    // Strength of the real hits in a run (80th percentile of frame attacks): drums vs no drums.
    auto onsetOf = [&] (const Run& run)
    {
        std::vector<float> v;
        for (size_t i = run.begin; i < run.end; ++i) if (frames[i].kPower > 1.0e-7f) v.push_back (frames[i].onsetDb);
        return percentile (v, 0.8f);
    };

    for (size_t r = 0; r < runs.size(); ++r)
    {
        const auto& run = runs[r];
        Section s;
        s.startTime = timeOf (run.begin);
        s.endTime = timeOf (run.end);
        const size_t len = run.end - run.begin;
        const float level = mean (m.energyCurve, run.begin, run.end);
        const float startLevel = mean (m.energyCurve, run.begin, run.begin + std::max<size_t> (1, len / 4));
        const float endLevel = mean (m.energyCurve, run.end - std::max<size_t> (1, len / 4), run.end);
        const bool nextIsPeak = r + 1 < runs.size() && runs[r + 1].cls == 2;

        if (run.cls == 2)                                             s.type = SectionType::Drop;
        else if (run.cls == 0)                                        s.type = SectionType::Break;
        else if (endLevel - startLevel > 2.0f || (nextIsPeak && endLevel - startLevel > 1.0f)) s.type = SectionType::Build;
        else                                                          s.type = SectionType::Verse;

        bool positional = false;
        if (r == 0 && run.cls != 2 && runs.size() > 1)                    { s.type = SectionType::Intro; positional = true; }
        else if (r == runs.size() - 1 && run.cls != 2 && runs.size() > 2) { s.type = SectionType::Outro; positional = true; }

        // ---- Confidence: sharp boundaries, clear level margin and enough length make a confident reading.
        const size_t edge = 30;   // 3 s each side
        float sharp = 0.0f; int edges = 0;
        if (r > 0)
        {
            sharp += std::abs (mean (m.energyCurve, run.begin, run.begin + edge) - mean (m.energyCurve, run.begin - std::min (edge, run.begin), run.begin));
            ++edges;
        }
        if (r + 1 < runs.size())
        {
            sharp += std::abs (mean (m.energyCurve, run.end - std::min (edge, len), run.end) - mean (m.energyCurve, run.end, run.end + edge));
            ++edges;
        }
        const float sharpScore = edges > 0 ? juce::jlimit (0.0f, 1.0f, (sharp / (float) edges) / 6.0f) : 0.6f;
        float margin = 0.0f;
        if (flat)               margin = 0.2f;
        else if (run.cls == 2)  margin = (level - hi) / 3.0f;
        else if (run.cls == 0)  margin = (lo - level) / 3.0f;
        else                    margin = std::min (level - lo, hi - level) / 3.0f;
        margin = juce::jlimit (0.0f, 1.0f, margin);
        const float lengthScore = juce::jlimit (0.0f, 1.0f, (float) (s.endTime - s.startTime) / 16.0f);
        s.confidence = juce::jlimit (0.2f, 0.97f, 0.25f + 0.4f * sharpScore + 0.2f * margin + 0.15f * lengthScore + (positional ? 0.1f : 0.0f));
        // Boundary novelty also counts: a clear change of texture is a confident boundary.
        float edgeNovelty = 0;
        if (r > 0) edgeNovelty = std::max (edgeNovelty, nov[std::min (run.begin, nov.size() - 1)]);
        if (r + 1 < runs.size()) edgeNovelty = std::max (edgeNovelty, nov[std::min (run.end, nov.size() - 1)]);
        s.confidence = juce::jlimit (0.2f, 0.97f, s.confidence + 0.05f * juce::jlimit (0.0f, 2.0f, edgeNovelty - 2.0f));
        s.alternative = alternativeFor (s.type);

        // A "high" section sitting between two louder peaks, or a high section without the drums of its
        // neighbours, is most likely a dense break. FINISH says so but asks the producer to confirm.
        if (run.cls == 2 && r > 0 && r + 1 < runs.size() && runs[r - 1].cls == 2 && runs[r + 1].cls == 2)
        {
            const float lp = mean (m.energyCurve, runs[r - 1].begin, runs[r - 1].end);
            const float ln = mean (m.energyCurve, runs[r + 1].begin, runs[r + 1].end);
            const float on = onsetOf (run), onN = 0.5f * (onsetOf (runs[r - 1]) + onsetOf (runs[r + 1]));
            if (level < std::min (lp, ln) - 0.6f || on < onN - 1.5f)
            {
                s.type = SectionType::Break;
                s.alternative = SectionType::Drop;
                s.confidence = std::min (s.confidence, 0.55f);
            }
        }
        m.sections.push_back (s);
    }

    relabel (m.sections);
    updateLoudness (m);
    return m;
}
