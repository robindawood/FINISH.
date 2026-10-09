#include "AudioAnalyzer.h"

AudioAnalyzer::AudioAnalyzer()
{
    sampleStorage.resize ((size_t) sampleCapacity, 0.0f);
}

void AudioAnalyzer::prepare (double sampleRate)
{
    sr = sampleRate;
    frameLength = juce::jmax (1, (int) std::round (sr * 0.1));
    kw.prepare (sr);
    for (auto* b : { &lowLp, &lowLp2, &sideLowLp, &sideLowLp2 })   b->setLowPass (sr, 150.0, 0.7071);
    for (auto* b : { &highHp, &highHp2, &sideHighHp, &sideHighHp2 }) b->setHighPass (sr, 4000.0, 0.7071);
    fastAtt = envCoeff (sr, 0.0005); fastRel = envCoeff (sr, 0.030);
    slowAtt = envCoeff (sr, 0.020);  slowRel = envCoeff (sr, 0.150);
    reset();
}

void AudioAnalyzer::reset()
{
    kw.reset();
    for (auto* b : { &lowLp, &lowLp2, &highHp, &highHp2, &sideLowLp, &sideLowLp2, &sideHighHp, &sideHighHp2 }) b->reset();
    fastEnv = slowEnv = 0;
    accK = accMid = accSide = accLow = accHigh = accLowSide = accHighSide = accL = accR = accLR = 0;
    accPeak = accOnset = 0;
    frameCount = 0;
}

void AudioAnalyzer::process (const float* L, const float* R, int n, double blockTime) noexcept
{
    const bool stereo = R != nullptr;

    for (int i = 0; i < n; ++i)
    {
        if (frameCount == 0)
            frameStartTime = blockTime >= 0.0 ? blockTime + (double) i / sr : -1.0;

        const float l = L[i];
        const float r = stereo ? R[i] : l;
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r);

        const float k0 = kw.process (l, 0);
        const float k1 = stereo ? kw.process (r, 1) : 0.0f;
        accK += (double) (k0 * k0 + k1 * k1);

        accMid += (double) (mid * mid);
        accSide += (double) (side * side);
        const float lo = lowLp2.process (lowLp.process (mid, 0), 0);
        const float hi = highHp2.process (highHp.process (mid, 0), 0);
        const float ls = sideLowLp2.process (sideLowLp.process (side, 0), 0);
        accLow += (double) (lo * lo);
        accHigh += (double) (hi * hi);
        accLowSide += (double) (ls * ls);
        const float hs = sideHighHp2.process (sideHighHp.process (side, 0), 0);
        accHighSide += (double) (hs * hs);
        accL += (double) (l * l);
        accR += (double) (r * r);
        accLR += (double) (l * r);
        accPeak = std::max (accPeak, std::max (std::abs (l), std::abs (r)));

        const float det = std::abs (mid);
        fastEnv = det > fastEnv ? fastAtt * fastEnv + (1 - fastAtt) * det : fastRel * fastEnv + (1 - fastRel) * det;
        slowEnv = det > slowEnv ? slowAtt * slowEnv + (1 - slowAtt) * det : slowRel * slowEnv + (1 - slowRel) * det;
        if (slowEnv > 1.0e-4f)
            accOnset = std::max (accOnset, 20.0f * std::log10 ((fastEnv + 1.0e-6f) / (slowEnv + 1.0e-6f)));

        if (++frameCount >= frameLength)
            finishFrame();
    }

    if (publishSamples && sampleFifo.getFreeSpace() >= n)
    {
        int s1, n1, s2, n2;
        sampleFifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) sampleStorage[(size_t) (s1 + i)] = stereo ? 0.5f * (L[i] + R[i]) : L[i];
        for (int i = 0; i < n2; ++i) sampleStorage[(size_t) (s2 + i)] = stereo ? 0.5f * (L[n1 + i] + R[n1 + i]) : L[n1 + i];
        sampleFifo.finishedWrite (n1 + n2);
    }
}

void AudioAnalyzer::finishFrame() noexcept
{
    const double inv = 1.0 / (double) frameCount;
    AnalysisFrame f;
    f.hostTime = frameStartTime;
    f.seconds = (float) (frameCount / sr);
    f.kPower = (float) (accK * inv);
    f.peak = accPeak;
    f.midPow = (float) (accMid * inv);
    f.sidePow = (float) (accSide * inv);
    f.lowPow = (float) (accLow * inv);
    f.highPow = (float) (accHigh * inv);
    f.lowSidePow = (float) (accLowSide * inv);
    f.highSidePow = (float) (accHighSide * inv);
    f.lPow = (float) (accL * inv);
    f.rPow = (float) (accR * inv);
    f.lrCross = (float) (accLR * inv);
    f.onsetDb = accOnset;

    if (frameFifo.getFreeSpace() > 0)
    {
        int s1, n1, s2, n2;
        frameFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) frameStorage[(size_t) s1] = f;
        else if (n2 > 0) frameStorage[(size_t) s2] = f;
        frameFifo.finishedWrite (n1 + n2);
    }

    accK = accMid = accSide = accLow = accHigh = accLowSide = accHighSide = accL = accR = accLR = 0;
    accPeak = accOnset = 0;
    frameCount = 0;
}

int AudioAnalyzer::readFrames (AnalysisFrame* dest, int maxFrames) noexcept
{
    int s1, n1, s2, n2;
    frameFifo.prepareToRead (maxFrames, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) dest[i] = frameStorage[(size_t) (s1 + i)];
    for (int i = 0; i < n2; ++i) dest[n1 + i] = frameStorage[(size_t) (s2 + i)];
    frameFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

int AudioAnalyzer::readSamples (float* dest, int maxSamples) noexcept
{
    int s1, n1, s2, n2;
    sampleFifo.prepareToRead (maxSamples, s1, n1, s2, n2);
    if (n1 > 0) std::copy_n (sampleStorage.data() + s1, n1, dest);
    if (n2 > 0) std::copy_n (sampleStorage.data() + s2, n2, dest + n1);
    sampleFifo.finishedRead (n1 + n2);
    return n1 + n2;
}
