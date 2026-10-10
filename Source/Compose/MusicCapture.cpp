#include "MusicCapture.h"

void MusicCapture::prepare (double sampleRate)
{
    hostRate = sampleRate;
    decim = std::max (1, (int) std::round (sampleRate / 11025.0));
    const double newRate = sampleRate / decim;
    const size_t needed = (size_t) (maxSeconds * newRate) + 16;
    if (data.size() != needed || std::abs (newRate - rate) > 1.0e-6)
    {
        rate = newRate;
        data.assign (needed, 0.0f);
        numChunks = (size_t) (maxSeconds / chunkSeconds) + 2;
        covered.reset (new std::atomic<juce::uint8>[numChunks]);
        for (size_t i = 0; i < numChunks; ++i) covered[i].store (0);
        runStart = runEnd = 0.0;
    }
    accIndex = -1; accSum = 0; accCount = 0; lastEndTime = -1.0;
    // 4th-order Butterworth low-pass at 0.42 x the capture rate's Nyquist band edge
    const double fc = std::min (0.42 * rate, 0.45 * sampleRate);
    const double qs[2] = { 0.5412, 1.3066 };
    Biq* bq[2] = { &aa1, &aa2 };
    for (int k = 0; k < 2; ++k)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * fc / sampleRate, cw = std::cos (w), al = std::sin (w) / (2.0 * qs[k]);
        const double a0 = 1.0 + al;
        bq[k]->b0 = (float) ((1.0 - cw) / 2.0 / a0); bq[k]->b1 = (float) ((1.0 - cw) / a0); bq[k]->b2 = bq[k]->b0;
        bq[k]->a1 = (float) (-2.0 * cw / a0); bq[k]->a2 = (float) ((1.0 - al) / a0);
        bq[k]->z1 = bq[k]->z2 = 0.0f;
    }
}

void MusicCapture::clear()
{
    for (size_t i = 0; i < numChunks; ++i) covered[i].store (0);
    runStart = runEnd = 0.0;
}

void MusicCapture::push (const float* L, const float* R, int n, double songTime, double ppq, double bpm, bool playing) noexcept
{
    if (! playing || songTime < 0.0 || data.empty() || n <= 0)
    {
        lastEndTime = -1.0;
        accIndex = -1;
        return;
    }
    if (bpm > 20.0)
    {
        bpmA.store (bpm);
        if (ppq > -1.0e8 && ppq >= 0.0)
        {
            beatOffset.store (ppq - songTime * bpm / 60.0);
            tempoKnown.store (true);
        }
    }

    // Contiguous run tracking (a jump of more than 0.25 s starts a new run).
    if (lastEndTime < 0.0 || std::abs (songTime - lastEndTime) > 0.25)
    {
        runStart.store (songTime);
        accIndex = -1;
    }
    const double endTime = songTime + n / hostRate;
    runEnd.store (endTime);
    lastEndTime = endTime;

    const double startSample = songTime * hostRate;
    const long long maxIndex = (long long) data.size() - 1;
    for (int i = 0; i < n; ++i)
    {
        const long long idx = (long long) ((startSample + i) / decim);
        if (idx != accIndex)
        {
            if (accIndex >= 0 && accIndex <= maxIndex && accCount > 0)
                data[(size_t) accIndex] = accSum;
            accIndex = idx; accSum = 0.0f; accCount = 0;
        }
        accSum = aa2.tick (aa1.tick (R != nullptr ? 0.5f * (L[i] + R[i]) : L[i]));   // sample-and-hold of the filtered signal
        ++accCount;
    }
    const size_t c0 = (size_t) std::max (0.0, songTime / chunkSeconds);
    const size_t c1 = (size_t) std::max (0.0, endTime / chunkSeconds);
    for (size_t c = c0; c <= c1 && c < numChunks; ++c) covered[c].store (1);
}

MusicCapture::Snapshot MusicCapture::snapshot (double from, double to) const
{
    Snapshot s;
    s.rate = rate;
    s.bpm = bpmA.load();
    s.tempoKnown = tempoKnown.load();
    if (data.empty()) return s;
    if (from < 0.0 || to <= from) { from = runStart.load(); to = runEnd.load(); }
    from = juce::jlimit (0.0, maxSeconds, from);
    to = juce::jlimit (0.0, maxSeconds, to);
    if (to - from < 1.0) return s;

    // Keep only the covered part (trim uncovered edges).
    auto isCovered = [this] (double t) { const size_t c = (size_t) (t / chunkSeconds); return c < numChunks && covered[c].load() != 0; };
    while (from < to && ! isCovered (from)) from += chunkSeconds;
    while (to > from && ! isCovered (to - 1.0e-3)) to -= chunkSeconds;
    if (to - from < 1.0) return s;

    const size_t a = (size_t) (from * rate), b = std::min (data.size(), (size_t) (to * rate));
    s.audio.assign (data.begin() + (long) a, data.begin() + (long) b);
    s.startTime = (double) a / rate;
    s.beatAtStart = beatOffset.load() + s.startTime * s.bpm / 60.0;
    return s;
}
