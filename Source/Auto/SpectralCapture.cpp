#include "SpectralCapture.h"

void SpectralCapture::prepare (double sampleRate)
{
    sr = sampleRate;
    // Same frame duration as the profiles (16384 samples at 48 kHz): 16384 up to 64 kHz, 32768 above.
    fftOrder = sr > 64000.0 ? 15 : 14;
    fftSize = 1 << fftOrder;
    hop = fftSize / 2;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)   // numpy.hanning (symmetric)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
    work.assign ((size_t) fftSize * 2, 0.0f);
    binHz.resize ((size_t) fftSize / 2 + 1);
    binBand.assign ((size_t) fftSize / 2 + 1, -1);
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const float f = (float) k * (float) sr / (float) fftSize;
        binHz[(size_t) k] = f;
        for (int b = 0; b < numThirds; ++b)
        {
            const float c = profileThirdOctHz[b], lo = c / std::pow (2.0f, 1.0f / 6.0f), hi = c * std::pow (2.0f, 1.0f / 6.0f);
            if (f >= lo && f < hi) { binBand[(size_t) k] = b; break; }
        }
    }
    fifoL.assign ((size_t) fifoSize, 0.0f);
    fifoR.assign ((size_t) fifoSize, 0.0f);

    // True-peak interpolator (windowed sinc, Kaiser beta 9, 4 phases)
    constexpr int L = 4, N = tpTaps * L;
    double h[N];
    auto bessel0 = [] (double x) { double sum = 1, t = 1; for (int k = 1; k < 25; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); sum += t; } return sum; };
    for (int n = 0; n < N; ++n)
    {
        const double m = n - (N - 1) / 2.0, x = m / L, ax = juce::MathConstants<double>::pi * x * 1.1;
        const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (ax) / ax;
        const double r = 2.0 * n / (N - 1) - 1.0;
        h[n] = sinc * bessel0 (9.0 * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (9.0);
    }
    for (int ph = 0; ph < L; ++ph)
    {
        double sum = 0;
        for (int k = 0; k < tpTaps; ++k) sum += h[k * L + ph];
        for (int k = 0; k < tpTaps; ++k) tpCoef[ph][k] = (float) (h[k * L + ph] / sum);
    }
    start();
    stop();
}

void SpectralCapture::start()
{
    active.store (false);
    fifo.reset();
    frames.clear();
    midBuf.clear(); sideBuf.clear();
    totalSamples = 0;
    std::memset (tpHist, 0, sizeof (tpHist));
    tpPos = 0; tpMax = spMax = 0; tpWarm = tpTaps;
    active.store (true);
}

void SpectralCapture::resume()
{
    // A pause leaves a gap: joining both sides in one FFT frame or in the true-peak interpolator would create a step
    // (spectral leakage, phantom inter-sample peaks). Flush what was captured, drop the incomplete frame, restart clean.
    pull();
    breakContinuity();
    active.store (true);
}

void SpectralCapture::breakContinuity()
{
    processReadyFrames();
    midBuf.clear(); sideBuf.clear();
    std::memset (tpHist, 0, sizeof (tpHist));
    tpPos = 0; tpWarm = tpTaps;
}

void SpectralCapture::markGap() noexcept
{
    if (! active.load (std::memory_order_relaxed) || gapWritten) return;
    if (fifo.getFreeSpace() < 1) return;
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    const int at = n1 > 0 ? s1 : s2;
    fifoL[(size_t) at] = std::numeric_limits<float>::quiet_NaN();   // marker, never a real sample
    fifoR[(size_t) at] = 0.0f;
    fifo.finishedWrite (1);
    gapWritten = true;
}

void SpectralCapture::push (const float* L, const float* R, int n) noexcept
{
    if (! active.load (std::memory_order_relaxed)) return;
    if (fifo.getFreeSpace() < n) { markGap(); return; }   // consumer late: drop (statistics, not audio), mark the hole
    gapWritten = false;
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) { fifoL[(size_t) (s1 + i)] = L[i]; fifoR[(size_t) (s1 + i)] = R != nullptr ? R[i] : L[i]; }
    for (int i = 0; i < n2; ++i) { fifoL[(size_t) (s2 + i)] = L[n1 + i]; fifoR[(size_t) (s2 + i)] = R != nullptr ? R[n1 + i] : L[n1 + i]; }
    fifo.finishedWrite (n1 + n2);
}

void SpectralCapture::trackTruePeak (float l, float r) noexcept
{
    spMax = std::max (spMax, std::max (std::abs (l), std::abs (r)));
    tpHist[0][tpPos] = l; tpHist[1][tpPos] = r;
    for (int c = 0; c < 2; ++c)
        for (int ph = 0; ph < 4; ++ph)
        {
            float acc = 0;
            int idx = tpPos;
            for (int k = 0; k < tpTaps; ++k) { acc += tpCoef[ph][k] * tpHist[c][idx]; if (--idx < 0) idx = tpTaps - 1; }
            if (tpWarm <= 1) tpMax = std::max (tpMax, std::abs (acc));
        }
    if (++tpPos >= tpTaps) tpPos = 0;
    if (tpWarm > 0) { --tpWarm; tpMax = std::max (tpMax, spMax); }
}

void SpectralCapture::pull()
{
    const int ready = fifo.getNumReady();
    if (ready <= 0) return;
    int s1, n1, s2, n2;
    fifo.prepareToRead (ready, s1, n1, s2, n2);
    int gaps = 0;
    auto take = [this, &gaps] (int start, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = fifoL[(size_t) (start + i)], r = fifoR[(size_t) (start + i)];
            if (std::isnan (l)) { breakContinuity(); ++gaps; continue; }
            midBuf.push_back (0.5f * (l + r));
            sideBuf.push_back (0.5f * (l - r));
            trackTruePeak (l, r);
        }
    };
    take (s1, n1);
    take (s2, n2);
    fifo.finishedRead (n1 + n2);
    totalSamples += n1 + n2 - gaps;
    processReadyFrames();
}

void SpectralCapture::processReadyFrames()
{
    while ((int) midBuf.size() >= fftSize)
    {
        processFrame();
        midBuf.erase (midBuf.begin(), midBuf.begin() + hop);
        sideBuf.erase (sideBuf.begin(), sideBuf.begin() + hop);
    }
}

void SpectralCapture::processFrame()
{
    FrameRecord fr;
    double ms = 0;
    for (int i = 0; i < fftSize; ++i) ms += (double) midBuf[(size_t) i] * midBuf[(size_t) i];
    fr.rms = (float) std::sqrt (ms / fftSize);

    for (int pass = 0; pass < 2; ++pass)
    {
        const auto& src = pass == 0 ? midBuf : sideBuf;
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i) work[(size_t) i] = src[(size_t) i] * window[(size_t) i];
        fft->performFrequencyOnlyForwardTransform (work.data(), true);
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const double p = (double) work[(size_t) k] * work[(size_t) k];
            const float f = binHz[(size_t) k];
            if (pass == 0)
            {
                fr.midTotal += p;
                if (f < 150.0f) fr.midLow += p; else fr.midUpper += p;
                if (f < 60.0f) fr.midSub += p;
                if (f >= 200.0f && f < 500.0f) fr.midLowMid += p;
                if (f >= 2000.0f && f < 5000.0f) fr.midPres += p;
                if (f >= 4000.0f) fr.midHigh += p;
                const int b = binBand[(size_t) k];
                if (b >= 0) fr.thirds[b] += p;
            }
            else
            {
                fr.sideTotal += p;
                if (f < 150.0f) fr.sideLow += p;
                if (f >= 4000.0f) fr.sideHigh += p;
            }
        }
    }
    frames.push_back (fr);
}

SpectralCapture::Result SpectralCapture::result() const
{
    Result r;
    r.seconds = (double) totalSamples / sr;
    r.truePeakDb = 20.0f * std::log10 (std::max (tpMax, 1.0e-9f));
    r.samplePeakDb = 20.0f * std::log10 (std::max (spMax, 1.0e-9f));
    if (frames.size() < 4) return r;

    // loud frames: RMS >= 40th percentile (numpy.quantile, linear interpolation)
    std::vector<float> rms;
    for (auto& f : frames) rms.push_back (f.rms);
    std::sort (rms.begin(), rms.end());
    const double pos = 0.4 * (double) (rms.size() - 1);
    const size_t i0 = (size_t) pos;
    const float thr = rms[i0] + (float) (pos - (double) i0) * (rms[std::min (i0 + 1, rms.size() - 1)] - rms[i0]);

    FrameRecord s;
    int used = 0;
    for (auto& f : frames)
    {
        if (f.rms < thr) continue;
        ++used;
        s.midTotal += f.midTotal; s.sideTotal += f.sideTotal; s.midLow += f.midLow; s.sideLow += f.sideLow; s.midSub += f.midSub;
        s.midUpper += f.midUpper; s.midLowMid += f.midLowMid; s.midPres += f.midPres; s.midHigh += f.midHigh; s.sideHigh += f.sideHigh;
        for (int b = 0; b < numThirds; ++b) s.thirds[b] += f.thirds[b];
    }
    if (used == 0 || s.midTotal <= 0) return r;
    const double all = s.midTotal + s.sideTotal;
    r.valid = true;
    r.loudSeconds = (double) used * hop / sr;
    r.lowRatio = (float) ((s.midLow + s.sideLow) / all);
    r.subRatio = (float) (s.midSub / s.midTotal);
    r.lowMidRatio = (float) (s.midLowMid / std::max (1.0e-20, s.midUpper));
    r.presenceRatio = (float) (s.midPres / std::max (1.0e-20, s.midUpper));
    r.highRatio = (float) ((s.midHigh + s.sideHigh) / all);
    r.sideRatio = (float) (s.sideTotal / all);
    r.lowSideRatio = (float) (s.sideLow / std::max (1.0e-20, s.midLow + s.sideLow));
    for (int b = 0; b < numThirds; ++b)
        r.thirdOctDb[b] = (float) (10.0 * std::log10 (std::max (s.thirds[b], 1.0e-20 * s.midTotal) / s.midTotal));
    return r;
}

SpectralCapture::Result SpectralCapture::analyse (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    SpectralCapture c;
    c.prepare (sampleRate);
    c.start();
    const int n = buffer.getNumSamples();
    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
    for (int pos = 0; pos < n; pos += 4096)
    {
        const int len = std::min (4096, n - pos);
        c.push (L + pos, R != nullptr ? R + pos : nullptr, len);
        c.pull();
    }
    return c.result();
}
