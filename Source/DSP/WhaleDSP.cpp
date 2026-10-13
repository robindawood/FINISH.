#include "WhaleDSP.h"

namespace
{
    inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float powerToLufs (float p) noexcept { return -0.691f + 10.0f * std::log10 (std::max (p, 1.0e-12f)); }
    inline float powDb (float p) noexcept { return 10.0f * std::log10 (p + 1.0e-12f); }

    constexpr float visHz[WhaleDSP::numVisBands] = { 45.0f, 90.0f, 180.0f, 400.0f, 900.0f, 2200.0f, 5000.0f, 11000.0f };
}

float WhaleDSP::visBandHz (int b) noexcept { return visHz[juce::jlimit (0, numVisBands - 1, b)]; }

void WhaleDSP::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate;
    channels = juce::jlimit (1, 2, numChannels);
    maxBlock = std::max (512, maxBlockSize);

    for (auto* s : { &sPunch, &sDrive, &sCeil }) s->reset (sr, 0.05);
    for (auto* s : { &sGlue, &sSat, &sSide, &sMono, &sClip, &sLow, &sAir }) s->reset (sr, 0.25);
    sLowCutOn.reset (sr, 0.1);
    for (auto& s : sSpecGain) s.reset (sr, 0.25);
    for (auto& s : sSpecDyn) s.reset (sr, 0.25);
    sLevOn.reset (sr, 0.05);
    sBypass.reset (sr, 0.02);

    // SPECTRAL detectors (on the mid signal) and bells
    for (int b = 0; b < 4; ++b)
    {
        specHzNow[b] = specBands[b].hz;
        specDet[b].setBandPass (sr, specHzNow[b], 1.2);
        specBell[b].setPeak (sr, specHzNow[b], specBands[b].q, 0.0);
    }
    specAtt = envCoeff (sr, 0.005); specRel = envCoeff (sr, 0.12); specAvgCoef = envCoeff (sr, 2.0);

    // COMPRESSION
    fastAtt = envCoeff (sr, 0.0005); fastRel = envCoeff (sr, 0.030);
    slowAtt = envCoeff (sr, 0.020);  slowRel = envCoeff (sr, 0.150);
    for (auto* b : { &xLow1a, &xLow1b }) b->setLowPass (sr, 120.0, 0.7071);
    for (auto* b : { &xHigh1a, &xHigh1b }) b->setHighPass (sr, 120.0, 0.7071);
    for (auto* b : { &xLow2a, &xLow2b, &apLowA }) b->setLowPass (sr, 4000.0, 0.7071);
    for (auto* b : { &xHigh2a, &xHigh2b, &apHighA }) b->setHighPass (sr, 4000.0, 0.7071);
    apLowB = apLowA; apHighB = apHighA;
    bandAtt = envCoeff (sr, 0.0275); bandRel = envCoeff (sr, 0.25);
    bandRmsCoef = envCoeff (sr, 2.0); bandGrSmooth = envCoeff (sr, 0.03);
    lastDetail = -1.0f;

    // Oversampling x4 (polyphase IIR, integer latency) for saturation + clipper
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);   // linear phase: transparent when idle
    os->initProcessing ((size_t) maxBlock);
    osLatency = (int) std::round (os->getLatencyInSamples());

    // True-peak interpolator: windowed sinc (Kaiser), 4 phases.
    {
        constexpr int L = 4, N = tpTaps * L;
        double h[N];
        const double beta = 9.0;
        auto bessel0 = [] (double x) { double sum = 1, t = 1; for (int k = 1; k < 25; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); sum += t; } return sum; };
        for (int n = 0; n < N; ++n)
        {
            const double m = n - (N - 1) / 2.0;
            const double x = m / L;
            const double fcut = 1.1, ax = juce::MathConstants<double>::pi * x * fcut;
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (ax) / ax;
            const double r = 2.0 * n / (N - 1) - 1.0;
            h[n] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        }
        for (int ph = 0; ph < L; ++ph)
        {
            double sum = 0;
            for (int k = 0; k < tpTaps; ++k) sum += h[k * L + ph];
            for (int k = 0; k < tpTaps; ++k) tpCoef[ph][k] = (float) (h[k * L + ph] / sum);
        }
    }
    // After the non-linear stages: band-limit just below Nyquist (folded energy makes unpredictable inter-sample
    // peaks) + DC blocker (asymmetric saturation). Cross-faded in only while saturation / clipper are active.
    nyqA.setLowPass (sr, std::min (20000.0, sr * 0.43), 0.5412);
    nyqB.setLowPass (sr, std::min (20000.0, sr * 0.43), 1.3066);
    dcBlock.setHighPass (sr, 5.0, 0.7071);
    sNl.reset (sr, 0.05);

    // Look-ahead limiter: 1.5 ms
    lookahead = std::max (8, (int) std::round (sr * 0.0015));
    rampLen = std::max (2, lookahead / 2);
    rampBuf.assign ((size_t) rampLen, 1.0f);
    limRel = envCoeff (sr, 0.080);
    for (int c = 0; c < 2; ++c)
    {
        laBuf[c].assign ((size_t) lookahead + 1, 0.0f);
        dryNow[c].assign ((size_t) maxBlock, 0.0f);
    }
    monoWork.assign ((size_t) maxBlock, 0.0f);
    reqGain.assign ((size_t) lookahead + 2, 1.0f);
    minQueue.assign ((size_t) lookahead + 2, 0);

    dryLen = osLatency + lookahead + 1;
    for (int c = 0; c < 2; ++c) dryRing[c].assign ((size_t) dryLen, 0.0f);

    kIn.prepare (sr); kOut.prepare (sr);
    matchCoef = envCoeff (sr, 3.0);
    shortCoef = envCoeff (sr, 1.5);
    momCoef = envCoeff (sr, 0.2);
    matchSmoothCoef = envCoeff (sr, 0.3);
    statCoef = envCoeff (sr, 0.4);
    for (int b = 0; b < numVisBands; ++b) visBp[b].setBandPass (sr, visHz[b], 1.4);
    visCoef = envCoeff (sr, 0.06);
    kickLp.setLowPass (sr, 120.0, 0.7071);
    kickCoef = envCoeff (sr, 0.04); kickAvgCoef = envCoeff (sr, 1.5);

    sDrive.setCurrentAndTargetValue (0.0f);
    sCeil.setCurrentAndTargetValue (-1.0f);
    for (auto* s : { &sPunch, &sGlue, &sLevOn, &sSat, &sSide, &sMono, &sClip, &sLow, &sAir, &sBypass, &sNl }) s->setCurrentAndTargetValue (0.0f);
    for (auto& s : sSpecGain) s.setCurrentAndTargetValue (0.0f);
    for (auto& s : sSpecDyn) s.setCurrentAndTargetValue (0.0f);
    reset();
}

void WhaleDSP::reset()
{
    for (int b = 0; b < 4; ++b)
    {
        specDet[b].reset(); specBell[b].reset();
        specEnv[b] = specAvg[b] = specCut[b] = 0.0f;
        lastSpecGain[b] = 0.0f;
        specBell[b].setPeak (sr, specHzNow[b], specBands[b].q, 0.0);
    }
    specCounter = 0;
    fastEnv = slowEnv = 0.0f;
    for (auto* b : { &xLow1a, &xLow1b, &xHigh1a, &xHigh1b, &xLow2a, &xLow2b, &xHigh2a, &xHigh2b, &apLowA, &apLowB, &apHighA, &apHighB,
                     &shelfLow, &shelfAir, &sideHpA, &sideHpB, &nyqA, &nyqB, &dcBlock, &kickLp })
        b->reset();
    for (int k = 0; k < 3; ++k) bandEnv[k] = bandRms[k] = bandGr[k] = 0.0f;
    lastLow = lastAir = 1000.0f;
    sLowCutOn.setCurrentAndTargetValue (0.0f);
    lowCutHz = lowCutTargetHz = 20.0f;
    lowCutA.set (sr, lowCutHz, 0.51763809); lowCutB.set (sr, lowCutHz, 0.70710678); lowCutC.set (sr, lowCutHz, 1.93185165);
    lowCutA.reset(); lowCutB.reset(); lowCutC.reset();
    lastMonoHz = -1.0f;
    toneCounter = 0;
    if (os != nullptr) os->reset();
    std::memset (tpHist, 0, sizeof (tpHist));
    tpPos = 0;
    for (int c = 0; c < 2; ++c)
    {
        std::fill (laBuf[c].begin(), laBuf[c].end(), 0.0f);
        std::fill (dryRing[c].begin(), dryRing[c].end(), 0.0f);
    }
    dryPos = 0;
    std::fill (reqGain.begin(), reqGain.end(), 1.0f);
    qHead = qTail = laPos = 0;
    limGain = 1.0f;
    std::fill (rampBuf.begin(), rampBuf.end(), 1.0f);
    rampSum = (double) rampLen;
    rampPos = 0;
    kIn.reset(); kOut.reset();
    pIn = pOut = pInShort = pOutShort = pOutMom = 0.0f;
    matchGain = 1.0f;
    corrDb = 0.0f;
    peakHoldDb = -100.0f;
    accLR = accLL = accRR = accMid = accSide = 0.0f;
    for (int b = 0; b < numVisBands; ++b) { visBp[b].reset(); visEnv[b] = 0.0f; }
    kickEnv = kickAvg = 0.0f;
}

void WhaleDSP::process (juce::AudioBuffer<float>& buffer, const Settings& s) noexcept
{
    const int n = buffer.getNumSamples();
    const int nc = juce::jmin (channels, buffer.getNumChannels());
    if (n == 0 || nc == 0) return;

    // Closed-loop loudness: while the loud passages play, nudge the drive until they sit on the target.
    // Only the loud passages of the INPUT drive the loop: otherwise a breakdown pulls the gain up (up to +8 dB), the
    // next drop comes out several dB too loud and the song's sections get flattened.
    if (s.autoLoudness && ! s.bypass)
    {
        const float outShort = powerToLufs (pOutShort), inShort = powerToLufs (pInShort);
        const float err = s.targetLufs - outShort;
        const bool loudPassage = inShort > -50.0f && inShort >= s.loudInputLufs - 1.5f;
        if (loudPassage && outShort > s.targetLufs - 6.0f && ! (err > 0 && -gainToDb (limGain) > 8.0f))
            corrDb = juce::jlimit (-14.0f, 14.0f, corrDb + err * (float) n / (float) (sr * 6.0));
    }
    else if (! s.autoLoudness)
        corrDb = 0.0f;
    corrDbOut.store (corrDb);

    sPunch.setTargetValue (juce::jlimit (0.0f, 1.0f, s.punch));
    sGlue.setTargetValue (juce::jlimit (0.0f, 1.0f, s.glue));
    sLevOn.setTargetValue (s.glue > 1.0e-3f ? 1.0f : 0.0f);
    sSat.setTargetValue (juce::jlimit (0.0f, 1.0f, s.satDrive));
    sSide.setTargetValue (juce::jlimit (-6.0f, 4.0f, s.sideDb));
    sMono.setTargetValue (s.monoHz >= 20.0f ? 1.0f : 0.0f);
    sClip.setTargetValue (juce::jlimit (0.0f, 1.0f, s.clip));
    sNl.setTargetValue ((s.clip > 1.0e-3f || s.satDrive > 1.0e-3f) ? 1.0f : 0.0f);
    sLow.setTargetValue (juce::jlimit (-6.0f, 6.0f, s.lowDb));
    {
        const bool cutOn = s.lowCutHz >= 15.0f;
        sLowCutOn.setTargetValue (cutOn ? 1.0f : 0.0f);
        if (cutOn) lowCutTargetHz = juce::jlimit (15.0f, 60.0f, s.lowCutHz);
    }
    sAir.setTargetValue (juce::jlimit (-6.0f, 6.0f, s.airDb));
    sDrive.setTargetValue (juce::jlimit (-12.0f, 30.0f, s.driveDb + corrDb));
    sCeil.setTargetValue (juce::jlimit (-6.0f, -0.1f, s.ceilingDb));
    sBypass.setTargetValue (s.bypass ? 1.0f : 0.0f);
    for (int b = 0; b < 4; ++b)
    {
        sSpecGain[b].setTargetValue (juce::jlimit (-6.0f, 6.0f, s.specGainDb[b]));
        sSpecDyn[b].setTargetValue (juce::jlimit (0.0f, 1.0f, s.specDyn[b]));
        // centre frequency: glide in the log domain (a few blocks), redesign detector + bell when it moved
        const float want = juce::jlimit (specBands[b].lo, specBands[b].hi, s.specHz[b]);
        if (std::abs (std::log (want / specHzNow[b])) > 0.002f)
        {
            specHzNow[b] *= std::pow (want / specHzNow[b], 0.35f);
            if (std::abs (std::log (want / specHzNow[b])) <= 0.002f) specHzNow[b] = want;
            specDet[b].setBandPass (sr, specHzNow[b], 1.2);
            specRedesign[b] = true;
        }
    }
    if (std::abs (s.detail - lastDetail) > 0.01f)
    {
        lastDetail = s.detail;
        bandAtt = envCoeff (sr, 0.012 + 0.03 * juce::jlimit (0.0f, 1.0f, s.detail));   // more detail = slower attack: transients pass
    }
    if (s.monoHz >= 20.0f && std::abs (s.monoHz - lastMonoHz) > 1.0f)
    {
        lastMonoHz = s.monoHz;
        sideHpA.setHighPass (sr, s.monoHz, 0.7071);
        sideHpB.setHighPass (sr, s.monoHz, 0.7071);
    }

    peakHoldDb = std::max (-100.0f, peakHoldDb - 12.0f * (float) n / (float) sr);
    grDb.store (0.0f);
    truePeakDb.store (peakHoldDb);

    float* chL = buffer.getWritePointer (0);
    float* chR = nc > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = std::min (maxBlock, n - pos);
        float* L = chL + pos;
        float* R = chR != nullptr ? chR + pos : nullptr;

        // Work in stereo: a mono input is processed as L = R.
        if (R == nullptr) std::copy (L, L + len, monoWork.data());
        float* work[2] = { L, R != nullptr ? R : monoWork.data() };

        // Dry, delayed by the full latency (ring of latency + 1 samples).
        for (int i = 0; i < len; ++i)
        {
            const int rd = (dryPos + 1) % dryLen;
            for (int c = 0; c < 2; ++c)
            {
                dryRing[c][(size_t) dryPos] = work[c][i];
                dryNow[c][(size_t) i] = dryRing[c][(size_t) rd];
            }
            dryPos = rd;
        }

        stageA (work[0], work[1], len, s);
        {
            juce::dsp::AudioBlock<float> block (work, 2, (size_t) len);
            stageB (block, s);
        }
        stageC (work[0], work[1], len, s);
    }

    inLufs.store (powerToLufs (pInShort));
    outLufs.store (powerToLufs (pOutShort));
    outMomentary.store (powerToLufs (pOutMom));
    matchDb.store (gainToDb (matchGain));
}

void WhaleDSP::HpD::set (double sampleRate, double f, double q) noexcept
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sampleRate, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (1.0 + cw) / 2.0 / a0; b1 = -(1.0 + cw) / a0; b2 = b0;
    a1 = -2.0 * cw / a0; a2 = (1.0 - alpha) / a0;
}

// LOW-CUT -> SPECTRAL -> COMPRESSION -> BASSES/AIR -> LARGEUR
void WhaleDSP::stageA (float* L, float* R, int n, const Settings&) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];

        // ---- LOW-CUT (first: the dynamic stages must not react to rumble that is removed anyway)
        {
            const float on = sLowCutOn.getNextValue();
            if ((i & 31) == 0 && std::abs (lowCutTargetHz - lowCutHz) > 0.05f)
            {
                lowCutHz += 0.2f * (lowCutTargetHz - lowCutHz);    // glide: no zipper when AUTO moves it
                lowCutA.set (sr, lowCutHz, 0.51763809);
                lowCutB.set (sr, lowCutHz, 0.70710678);
                lowCutC.set (sr, lowCutHz, 1.93185165);
            }
            const float hl = (float) lowCutC.process (lowCutB.process (lowCutA.process (l, 0), 0), 0);
            const float hr = (float) lowCutC.process (lowCutB.process (lowCutA.process (r, 1), 1), 1);
            if (on > 1.0e-4f) { l += on * (hl - l); r += on * (hr - r); }
        }

        // ---- SPECTRAL: static bell + dynamic cut when the band bursts above its own 2 s average
        {
            const float mid = 0.5f * (l + r);
            const bool update = ++specCounter >= 16;
            if (update) specCounter = 0;
            for (int b = 0; b < 4; ++b)
            {
                const float g = sSpecGain[b].getNextValue(), dyn = sSpecDyn[b].getNextValue();
                const float d = specDet[b].process (mid, 0);
                const float e = d * d;
                specEnv[b] = e > specEnv[b] ? specAtt * specEnv[b] + (1 - specAtt) * e : specRel * specEnv[b] + (1 - specRel) * e;
                specAvg[b] = specAvgCoef * specAvg[b] + (1 - specAvgCoef) * e;
                if (update)
                {
                    const float over = powDb (specEnv[b]) - (powDb (specAvg[b]) + 3.0f);
                    const float want = (over > 0 && dyn > 1.0e-3f) ? std::min (6.0f, 0.6f * over) * dyn : 0.0f;
                    specCut[b] = 0.7f * specCut[b] + 0.3f * want;
                    const float total = g - specCut[b];
                    if (specRedesign[b] || std::abs (total - lastSpecGain[b]) > 0.05f || (std::abs (total) < 0.05f && lastSpecGain[b] != 0.0f))
                    {
                        specRedesign[b] = false;
                        lastSpecGain[b] = std::abs (total) < 0.05f ? 0.0f : total;
                        specBell[b].setPeak (sr, specHzNow[b], specBands[b].q, lastSpecGain[b]);
                    }
                }
                if (lastSpecGain[b] != 0.0f)
                {
                    l = specBell[b].process (l, 0);
                    r = specBell[b].process (r, 1);
                }
                else
                {
                    specBell[b].process (l, 0); specBell[b].process (r, 1);   // keep the state warm (no click when it starts)
                }
            }
        }

        // ---- COMPRESSION: punch (transient shaper)
        {
            const float punch = sPunch.getNextValue();
            const float det = std::max (std::abs (l), std::abs (r));
            fastEnv = det > fastEnv ? fastAtt * fastEnv + (1 - fastAtt) * det : fastRel * fastEnv + (1 - fastRel) * det;
            slowEnv = det > slowEnv ? slowAtt * slowEnv + (1 - slowAtt) * det : slowRel * slowEnv + (1 - slowRel) * det;
            if (punch > 1.0e-4f)
            {
                const float ratio = juce::jlimit (1.0f, 4.0f, (fastEnv + 1.0e-6f) / (slowEnv + 1.0e-6f));
                const float g = std::pow (ratio, punch * 0.8f);
                l *= g; r *= g;
            }
        }
        // ---- COMPRESSION: 3-band leveller (glue). Band peaks are held near their own average (+3 dB).
        {
            const float glue = sGlue.getNextValue();
            const float levOn = sLevOn.getNextValue();
            if (levOn > 1.0e-4f)
            {
                float bl[3], br[3];
                for (int c = 0; c < 2; ++c)
                {
                    const float x = c == 0 ? l : r;
                    const float lo = xLow1b.process (xLow1a.process (x, c), c);
                    const float rest = xHigh1b.process (xHigh1a.process (x, c), c);
                    const float md = xLow2b.process (xLow2a.process (rest, c), c);
                    const float hi = xHigh2b.process (xHigh2a.process (rest, c), c);
                    const float loAligned = apLowB.process (apLowA.process (lo, c), c) + apHighB.process (apHighA.process (lo, c), c);
                    float* dst = c == 0 ? bl : br;
                    dst[0] = loAligned; dst[1] = md; dst[2] = hi;
                }
                float yl = 0, yr = 0;
                for (int k = 0; k < 3; ++k)
                {
                    const float e = 0.5f * (bl[k] * bl[k] + br[k] * br[k]);
                    bandEnv[k] = e > bandEnv[k] ? bandAtt * bandEnv[k] + (1 - bandAtt) * e : bandRel * bandEnv[k] + (1 - bandRel) * e;
                    bandRms[k] = bandRmsCoef * bandRms[k] + (1 - bandRmsCoef) * e;
                    const float over = powDb (bandEnv[k]) - (powDb (bandRms[k]) + 3.0f);
                    const float want = over > 0 ? std::min (5.0f, 0.55f * over) * glue : 0.0f;
                    bandGr[k] = bandGrSmooth * bandGr[k] + (1 - bandGrSmooth) * want;
                    const float g = dbToGain (-bandGr[k]);
                    yl += bl[k] * g; yr += br[k] * g;
                }
                l += levOn * (yl - l);
                r += levOn * (yr - r);
            }
        }

        // ---- BASSES / AIR
        {
            const float lo = sLow.getNextValue(), air = sAir.getNextValue();
            if (++toneCounter >= 32)
            {
                toneCounter = 0;
                if (std::abs (lo - lastLow) > 0.01f) { shelfLow.setLowShelf (sr, 110.0, lo); lastLow = lo; }
                if (std::abs (air - lastAir) > 0.01f) { shelfAir.setHighShelf (sr, 10000.0, air); lastAir = air; }
            }
            if (std::abs (lastLow) > 0.01f) { l = shelfLow.process (l, 0); r = shelfLow.process (r, 1); }
            if (std::abs (lastAir) > 0.01f) { l = shelfAir.process (l, 0); r = shelfAir.process (r, 1); }
        }

        // ---- LARGEUR: side gain + mono bass (4th-order high-pass on the sides)
        {
            const float sideDb = sSide.getNextValue(), mono = sMono.getNextValue();
            const float sd = 0.5f * (l - r), m = 0.5f * (l + r);
            const float sideHp = sideHpB.process (sideHpA.process (sd, 0), 0);   // always run: no state jump
            if (std::abs (sideDb) > 0.01f || mono > 0.001f)
            {
                const float ns = dbToGain (sideDb) * (sd + mono * (sideHp - sd));
                l = m + ns; r = m - ns;
            }
        }
        L[i] = l; R[i] = r;
    }
}

// x4 oversampled: SATURATION -> drive -> CLIPPER
void WhaleDSP::stageB (juce::dsp::AudioBlock<float>& block, const Settings&) noexcept
{
    const int n = (int) block.getNumSamples();
    const float sat0 = sSat.getCurrentValue(), clip0 = sClip.getCurrentValue();
    const float drv0 = sDrive.getCurrentValue(), ceil0 = sCeil.getCurrentValue();
    sSat.skip (n); sClip.skip (n); sDrive.skip (n); sCeil.skip (n);
    const float sat1 = sSat.getCurrentValue(), clip1 = sClip.getCurrentValue();
    const float drv1 = sDrive.getCurrentValue(), ceil1 = sCeil.getCurrentValue();

    auto up = os->processSamplesUp (block);
    const int m = (int) up.getNumSamples();
    float maxRatio = 1.0f;
    for (int c = 0; c < 2; ++c)
    {
        float* x = up.getChannelPointer ((size_t) c);
        for (int i = 0; i < m; ++i)
        {
            const float t = (float) i / (float) m;
            const float sat = sat0 + t * (sat1 - sat0);
            const float clip = clip0 + t * (clip1 - clip0);
            const float drive = dbToGain (drv0 + t * (drv1 - drv0));
            const float ceil = dbToGain (ceil0 + t * (ceil1 - ceil0));
            float v = x[i];
            if (sat > 1.0e-4f)
            {
                // Smooth tanh with a small bias (even harmonics = warmth), unity gain at low level.
                const float k = 1.0f + 2.5f * sat, bias = 0.12f * sat;
                const float tb = std::tanh (k * bias);
                v = (std::tanh (k * (v + bias)) - tb) / (k * (1.0f - tb * tb));
            }
            v *= drive;
            if (clip > 1.0e-4f)
            {
                const float knee = ceil * dbToGain (-3.5f * clip), top = ceil * dbToGain (0.5f);
                const float av = std::abs (v);
                if (av > knee)
                {
                    const float y = knee + (top - knee) * std::tanh ((av - knee) / (top - knee));
                    maxRatio = std::max (maxRatio, av / y);
                    v = v < 0 ? -y : y;
                }
            }
            x[i] = v;
        }
    }
    os->processSamplesDown (block);
    clipOut.store (gainToDb (maxRatio));
}

// DC blocker -> true-peak look-ahead limiter -> meters -> A/B
void WhaleDSP::stageC (float* L, float* R, int n, const Settings& s) noexcept
{
    const int W = lookahead + 1;
    float blockGr = 0.0f, blockTp = 0.0f;
    const float ceil = dbToGain (sCeil.getCurrentValue());
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        {
            const float nl = sNl.getNextValue();
            const float fl = nyqB.process (nyqA.process (dcBlock.process (l, 0), 0), 0);
            const float fr = nyqB.process (nyqA.process (dcBlock.process (r, 1), 1), 1);
            if (nl > 1.0e-4f) { l += nl * (fl - l); r += nl * (fr - r); }
        }

        float pk = std::max (std::abs (l), std::abs (r));
        for (int c = 0; c < 2; ++c)
        {
            tpHist[c][tpPos] = c == 0 ? l : r;
            for (int ph = 0; ph < 4; ++ph)
            {
                float acc = 0;
                int idx = tpPos;
                for (int k = 0; k < tpTaps; ++k)
                {
                    acc += tpCoef[ph][k] * tpHist[c][idx];
                    if (--idx < 0) idx = tpTaps - 1;
                }
                pk = std::max (pk, std::abs (acc));
            }
        }
        if (++tpPos >= tpTaps) tpPos = 0;
        const float tpCeil = ceil * 0.96f;   // -0.35 dB margin for the residual inter-sample error of the 4-phase detector
        const float req = pk > tpCeil ? tpCeil / pk : 1.0f;

        const int slot = laPos;
        const int Q = (int) minQueue.size();
        if (qHead != qTail && minQueue[(size_t) qHead] == slot) qHead = (qHead + 1) % Q;
        reqGain[(size_t) slot] = req;
        while (qHead != qTail)
        {
            const int back = (qTail - 1 + Q) % Q;
            if (reqGain[(size_t) minQueue[(size_t) back]] >= req) qTail = back; else break;
        }
        minQueue[(size_t) qTail] = slot; qTail = (qTail + 1) % Q;
        const float gmin = reqGain[(size_t) minQueue[(size_t) qHead]];
        rampSum += (double) gmin - (double) rampBuf[(size_t) rampPos];
        rampBuf[(size_t) rampPos] = gmin;
        if (++rampPos >= rampLen) rampPos = 0;
        const float ramped = std::min (1.0f, (float) (rampSum / rampLen));
        if (ramped < limGain) limGain = ramped;
        else                  limGain = limRel * limGain + (1 - limRel) * ramped;

        laBuf[0][(size_t) laPos] = l; laBuf[1][(size_t) laPos] = r;
        const int rd = (laPos + 1) % W;
        laPos = rd;
        float ol = juce::jlimit (-ceil, ceil, laBuf[0][(size_t) rd] * limGain);
        float orr = juce::jlimit (-ceil, ceil, laBuf[1][(size_t) rd] * limGain);
        blockGr = std::max (blockGr, -gainToDb (limGain));
        blockTp = std::max (blockTp, std::max (std::abs (ol), std::abs (orr)));

        const float ddl = dryNow[0][(size_t) i], ddr = dryNow[1][(size_t) i];

        // Loudness (output = processed, input = aligned dry) and the A/B match gain.
        {
            const float ki0 = kIn.process (ddl, 0), ki1 = kIn.process (ddr, 1);
            const float ko0 = kOut.process (ol, 0), ko1 = kOut.process (orr, 1);
            const float powIn = ki0 * ki0 + ki1 * ki1, powOut = ko0 * ko0 + ko1 * ko1;
            pInShort = shortCoef * pInShort + (1 - shortCoef) * powIn;
            pOutShort = shortCoef * pOutShort + (1 - shortCoef) * powOut;
            pOutMom = momCoef * pOutMom + (1 - momCoef) * powOut;
            if (pInShort > 1.0e-7f)
            {
                pIn = matchCoef * pIn + (1 - matchCoef) * powIn;
                pOut = matchCoef * pOut + (1 - matchCoef) * powOut;
            }
            float target = 1.0f;
            if (pIn > 1.0e-9f && pOut > 1.0e-9f)
                target = juce::jlimit (dbToGain (-24.0f), dbToGain (24.0f), std::sqrt (pOut / pIn));
            matchGain = matchSmoothCoef * matchGain + (1 - matchSmoothCoef) * target;
        }

        const float byp = sBypass.getNextValue();
        const float gm = s.matchAB ? matchGain : 1.0f;
        ol += byp * (ddl * gm - ol);
        orr += byp * (ddr * gm - orr);

        // Output analysis for the visuals and meters
        {
            const float m = 0.5f * (ol + orr), sd = 0.5f * (ol - orr);
            accLR = statCoef * accLR + (1 - statCoef) * ol * orr;
            accLL = statCoef * accLL + (1 - statCoef) * ol * ol;
            accRR = statCoef * accRR + (1 - statCoef) * orr * orr;
            accMid = statCoef * accMid + (1 - statCoef) * m * m;
            accSide = statCoef * accSide + (1 - statCoef) * sd * sd;
            for (int b = 0; b < numVisBands; ++b)
            {
                const float y = visBp[b].process (m, 0);
                visEnv[b] = visCoef * visEnv[b] + (1 - visCoef) * y * y;
            }
            const float k = kickLp.process (m, 0);
            const float e = k * k;
            kickEnv = e > kickEnv ? 0.6f * kickEnv + 0.4f * e : kickCoef * kickEnv + (1 - kickCoef) * e;
            kickAvg = kickAvgCoef * kickAvg + (1 - kickAvgCoef) * e;
        }

        L[i] = ol;
        R[i] = orr;
    }

    grDb.store (std::max (grDb.load(), blockGr));
    for (int k = 0; k < 3; ++k) bandGrOut[(size_t) k].store (bandGr[k]);
    for (int b = 0; b < 4; ++b) specDynOut[(size_t) b].store (specCut[b]);
    peakHoldDb = std::max (peakHoldDb, gainToDb (blockTp));
    truePeakDb.store (std::max (truePeakDb.load(), peakHoldDb));
    {
        const float blockDb = gainToDb (blockTp);
        float prev = maxPeakDb.load();
        while (blockDb > prev && ! maxPeakDb.compare_exchange_weak (prev, blockDb)) {}
    }
    const float den = std::sqrt (accLL * accRR);
    corrOut.store (den > 1.0e-10f ? juce::jlimit (-1.0f, 1.0f, accLR / den) : 1.0f);
    widthOut.store ((accMid + accSide) > 1.0e-10f ? accSide / (accMid + accSide) : 0.0f);
    for (int b = 0; b < numVisBands; ++b) visOut[(size_t) b].store (powDb (visEnv[b]));
    kickOut.store (juce::jlimit (0.0f, 1.0f, (powDb (kickEnv) - powDb (kickAvg)) / 8.0f));
}
