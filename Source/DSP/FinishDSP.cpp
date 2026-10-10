#include "FinishDSP.h"

namespace
{
    inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float powerToLufs (float p) noexcept { return -0.691f + 10.0f * std::log10 (std::max (p, 1.0e-12f)); }
}

void FinishDSP::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate;
    channels = juce::jlimit (1, 2, numChannels);
    maxBlock = std::max (512, maxBlockSize);

    for (auto* s : { &sAmount, &sImpact, &sPunch, &sWidth, &sWarmth, &sLoudness, &sDrive, &sCeil })
        s->reset (sr, 0.05);
    for (auto* s : { &sHp, &sLp, &sGate, &sEcho, &sSpace })
        s->reset (sr, 0.08);
    for (auto* s : { &sEqLow, &sEqHigh, &sGlue, &sClip, &sEqMud, &sEqPres, &sSide, &sMono })
        s->reset (sr, 0.3);
    sLevOn.reset (sr, 0.05);
    msSideLoA.setHighPass (sr, 160.0, 0.7071);   // (high-passes the sides: mono bass)
    msSideLoB.setHighPass (sr, 160.0, 0.7071);
    sBypass.reset (sr, 0.02);
    sDry.reset (sr, 0.03);
    sTrim.reset (sr, 0.03);

    fastAtt = envCoeff (sr, 0.0005); fastRel = envCoeff (sr, 0.030);
    slowAtt = envCoeff (sr, 0.020);  slowRel = envCoeff (sr, 0.150);
    compAtt = envCoeff (sr, 0.010);  compRel = envCoeff (sr, 0.150);
    matchCoef = envCoeff (sr, 3.0);
    shortCoef = envCoeff (sr, 1.5);
    momCoef = envCoeff (sr, 0.2);
    matchSmoothCoef = envCoeff (sr, 0.3);
    gateCoef = envCoeff (sr, 0.0015);

    // 3-band split: Linkwitz-Riley 4 (two Butterworth sections) at 120 Hz and 4 kHz.
    for (auto* b : { &xLow1a, &xLow1b }) b->setLowPass (sr, 120.0, 0.7071);
    for (auto* b : { &xHigh1a, &xHigh1b }) b->setHighPass (sr, 120.0, 0.7071);
    for (auto* b : { &xLow2a, &xLow2b, &apLowA }) b->setLowPass (sr, 4000.0, 0.7071);
    for (auto* b : { &xHigh2a, &xHigh2b, &apHighA }) b->setHighPass (sr, 4000.0, 0.7071);
    apLowB = apLowA; apHighB = apHighA;
    nyqA.setLowPass (sr, std::min (20000.0, sr * 0.43), 0.5412);
    nyqB.setLowPass (sr, std::min (20000.0, sr * 0.43), 1.3066);
    bandAtt = envCoeff (sr, 0.015); bandRel = envCoeff (sr, 0.25);
    bandRmsCoef = envCoeff (sr, 2.0); bandGrSmooth = envCoeff (sr, 0.03);

    // True-peak interpolator: windowed sinc (Kaiser), split in 4 phases = the 4 points between two samples.
    {
        constexpr int L = 4, N = tpTaps * L;
        double h[N];
        const double beta = 9.0;
        auto bessel0 = [] (double x) { double sum = 1, t = 1; for (int k = 1; k < 25; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); sum += t; } return sum; };
        for (int n = 0; n < N; ++n)
        {
            const double m = n - (N - 1) / 2.0;
            const double x = m / L;
            // cut-off 10 % above Nyquist: flat passband up to Nyquist, so peaks are over- rather than under-estimated
            const double fcut = 1.1, ax = juce::MathConstants<double>::pi * x * fcut;
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (ax) / ax;
            const double r = 2.0 * n / (N - 1) - 1.0;
            h[n] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        }
        // With an even length the centre falls between taps: the 4 phases are the 4 in-between points.
        for (int ph = 0; ph < L; ++ph)
        {
            double sum = 0;
            for (int k = 0; k < tpTaps; ++k) sum += h[k * L + ph];
            for (int k = 0; k < tpTaps; ++k) tpCoef[ph][k] = (float) (h[k * L + ph] / sum);
        }
    }

    sideLowPass.setLowPass (sr, 140.0, 0.7071);
    sideLowPass2.setLowPass (sr, 140.0, 0.7071);
    kIn.prepare (sr); kOut.prepare (sr);

    // Look-ahead: 1.5 ms (reported to the host as latency).
    lookahead = std::max (8, (int) std::round (sr * 0.0015));
    limAtt = envCoeff (sr, (double) lookahead / sr / 4.0);
    rampLen = std::max (2, lookahead / 2);   // must stay below lookahead minus the detector delay (~12 samples)
    rampBuf.assign ((size_t) rampLen, 1.0f);
    limRel = envCoeff (sr, 0.080);
    for (int c = 0; c < 2; ++c)
    {
        laBuf[c].assign ((size_t) lookahead + 1, 0.0f);
        dryDelay[c].assign ((size_t) lookahead + 1, 0.0f);
        echoBuf[c].assign ((size_t) (sr * 2.5) + 1, 0.0f);
    }
    reqGain.assign ((size_t) lookahead + 2, 1.0f);
    minQueue.assign ((size_t) lookahead + 2, 0);

    wetL.assign ((size_t) maxBlock, 0.0f); wetR.assign ((size_t) maxBlock, 0.0f);
    dryL.assign ((size_t) maxBlock, 0.0f); dryR.assign ((size_t) maxBlock, 0.0f);

    reverb.setSampleRate (sr);
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.86f; rp.damping = 0.45f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    const MacroSettings neutral;
    sAmount.setCurrentAndTargetValue (neutral.amount);
    sImpact.setCurrentAndTargetValue (neutral.impact);
    sPunch.setCurrentAndTargetValue (neutral.punch);
    sWidth.setCurrentAndTargetValue (neutral.width);
    sWarmth.setCurrentAndTargetValue (neutral.warmth);
    sLoudness.setCurrentAndTargetValue (neutral.loudness);
    sDrive.setCurrentAndTargetValue (0.0f);
    sCeil.setCurrentAndTargetValue (defaultCeilingDb);
    for (auto* s : { &sHp, &sLp, &sGate, &sEcho, &sSpace }) s->setCurrentAndTargetValue (0.0f);
    sBypass.setCurrentAndTargetValue (0.0f);
    sDry.setCurrentAndTargetValue (0.0f);
    sTrim.setCurrentAndTargetValue (1.0f);
    for (auto* s : { &sEqLow, &sEqHigh, &sGlue, &sClip, &sEqMud, &sEqPres, &sSide, &sMono, &sLevOn }) s->setCurrentAndTargetValue (0.0f);
    lastPunchKeep = -1.0f;
    reset();
}

void FinishDSP::reset()
{
    fastEnv = slowEnv = 0.0f;
    compGr = 0.0f;
    lowShelf.reset(); highShelf.reset(); sideLowPass.reset(); sideLowPass2.reset();
    hpf.reset(); lpf.reset();
    hpf.set (sr, 20.0, 0.9f); lpf.set (sr, sr * 0.45, 0.9f);
    gateEnv = 1.0f; freePhase = 0;
    for (auto* b : { &mEqMud, &mEqPres, &msSideLoA, &msSideLoB }) b->reset();
    lastEqMud = lastEqPres = 1000.0f;
    for (auto* b : { &mEqLow, &mEqHigh, &nyqA, &nyqB, &xLow1a, &xLow1b, &xHigh1a, &xHigh1b, &xLow2a, &xLow2b, &xHigh2a, &xHigh2b, &apLowA, &apLowB, &apHighA, &apHighB })
        b->reset();
    for (int k = 0; k < 3; ++k) { bandEnv[k] = bandRms[k] = bandGr[k] = 0.0f; }
    lastEqLow = lastEqHigh = 1000.0f;
    eqCounter = 0;
    std::memset (tpHist, 0, sizeof (tpHist));
    tpPos = 0;
    for (int c = 0; c < 2; ++c)
    {
        std::fill (laBuf[c].begin(), laBuf[c].end(), 0.0f);
        std::fill (dryDelay[c].begin(), dryDelay[c].end(), 0.0f);
        std::fill (echoBuf[c].begin(), echoBuf[c].end(), 0.0f);
        echoLp[c] = 0;
    }
    echoPos = 0;
    reverb.reset();
    std::fill (reqGain.begin(), reqGain.end(), 1.0f);
    qHead = qTail = 0;
    laPos = 0;
    limGain = 1.0f;
    std::fill (rampBuf.begin(), rampBuf.end(), 1.0f);
    rampSum = (double) rampLen;
    rampPos = 0;
    kIn.reset(); kOut.reset();
    pIn = pOut = pInShort = pOutShort = pOutMom = 0.0f;
    matchGain = 1.0f;
    corrDb = 0.0f;
    peakHoldDb = -100.0f;
    lastWarmth = -1.0f;
    toneCounter = fxCounter = 0;
    updateToneFilters (sWarmth.getCurrentValue());
}

void FinishDSP::updateToneFilters (float warmth) noexcept
{
    const float w = juce::jlimit (-1.0f, 1.0f, (warmth - 0.5f) * 2.0f);
    const float lowDb  = w > 0 ?  w * 4.5f : w * 2.0f;   // warm: +4.5 dB lows, bright: -2 dB lows
    const float highDb = w > 0 ? -w * 2.5f : -w * 4.0f;  // warm: -2.5 dB top, bright: +4 dB top
    lowShelf.setLowShelf (sr, 120.0, lowDb);
    highShelf.setHighShelf (sr, 7500.0, highDb);
    lastWarmth = warmth;
}

void FinishDSP::process (juce::AudioBuffer<float>& buffer, const Settings& s) noexcept
{
    const int n = buffer.getNumSamples();
    const int nc = juce::jmin (channels, buffer.getNumChannels());
    if (n == 0 || nc == 0) return;

    sAmount.setTargetValue (s.macros.amount);
    sImpact.setTargetValue (s.macros.impact);
    sPunch.setTargetValue (s.macros.punch);
    sWidth.setTargetValue (s.macros.width);
    sWarmth.setTargetValue (s.macros.warmth);
    sLoudness.setTargetValue (s.macros.loudness);
    // Closed-loop loudness: while the loud passages play, nudge the drive until they sit on the target.
    // Slow (6 s), clamped to +-6 dB, and never pushes harder into a limiter already doing 8 dB.
    if (s.autoLoudness && ! s.bypass)
    {
        const float outShort = powerToLufs (pOutShort);
        const float err = s.targetLufs - outShort;
        if (outShort > s.targetLufs - 6.0f && powerToLufs (pInShort) > -50.0f && ! (err > 0 && -gainToDb (limGain) > 8.0f))
            corrDb = juce::jlimit (-6.0f, 6.0f, corrDb + err * (float) n / (float) (sr * 6.0));
    }
    else if (! s.autoLoudness)
        corrDb = 0.0f;
    corrDbOut.store (corrDb);
    sDrive.setTargetValue (juce::jlimit (-12.0f, 36.0f, s.driveDb + corrDb));
    sCeil.setTargetValue (juce::jlimit (-6.0f, -0.1f, s.ceilingDb));
    sBypass.setTargetValue (s.bypass ? 1.0f : 0.0f);
    sDry.setTargetValue (juce::jlimit (0.0f, 1.0f, s.sectionDry));
    sTrim.setTargetValue (dbToGain (juce::jlimit (-40.0f, 6.0f, s.trimDb)));
    sHp.setTargetValue (juce::jlimit (0.0f, 1.0f, s.fx.hp));
    sLp.setTargetValue (juce::jlimit (0.0f, 1.0f, s.fx.lp));
    sGate.setTargetValue (juce::jlimit (0.0f, 1.0f, s.fx.gate));
    sEcho.setTargetValue (juce::jlimit (0.0f, 1.0f, s.fx.echo));
    sSpace.setTargetValue (juce::jlimit (0.0f, 1.0f, s.fx.space));
    sEqLow.setTargetValue (juce::jlimit (-4.0f, 4.0f, s.eqLowDb));
    sEqHigh.setTargetValue (juce::jlimit (-4.0f, 4.0f, s.eqHighDb));
    sGlue.setTargetValue (juce::jlimit (0.0f, 1.0f, s.glue));
    sClip.setTargetValue (juce::jlimit (0.0f, 1.0f, s.clip));
    sEqMud.setTargetValue (juce::jlimit (-4.0f, 2.0f, s.eqMudDb));
    sEqPres.setTargetValue (juce::jlimit (-4.0f, 2.0f, s.eqPresDb));
    sSide.setTargetValue (juce::jlimit (-6.0f, 3.0f, s.sideDb));
    sMono.setTargetValue (juce::jlimit (0.0f, 1.0f, s.monoBass));
    sLevOn.setTargetValue (s.glue > 1.0e-3f ? 1.0f : 0.0f);
    if (std::abs (s.punchKeep - lastPunchKeep) > 0.01f)
    {
        lastPunchKeep = s.punchKeep;
        bandAtt = envCoeff (sr, 0.015 + 0.025 * juce::jlimit (0.0f, 1.0f, s.punchKeep));   // slower attack: transients pass
    }

    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    // meters: peak with a falling hold (about 12 dB/s), gain reduction with fast release
    const float fall = 12.0f * (float) n / (float) sr;
    peakHoldDb = std::max (-100.0f, peakHoldDb - fall);
    grDb.store (0.0f);
    truePeakDb.store (peakHoldDb);
    const double beatsPerSample = std::max (1.0, s.bpm) / 60.0 / sr;
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = std::min (maxBlock, n - pos);
        const double ppq = s.ppq >= 0.0 ? s.ppq + (double) pos * beatsPerSample : -1.0;
        processChunk (L + pos, R != nullptr ? R + pos : nullptr, len, s, ppq);
    }

    inLufs.store (powerToLufs (pInShort));
    outLufs.store (powerToLufs (pOutShort));
    outMomentary.store (powerToLufs (pOutMom));
    matchDb.store (gainToDb (matchGain));
}

void FinishDSP::processChunk (float* L, float* R, int n, const Settings& s, double ppqStart) noexcept
{
    const bool stereo = R != nullptr;
    float blockGr = 0.0f, blockTp = 0.0f, blockClip = 1.0f;
    const double beatsPerSample = std::max (1.0, s.bpm) / 60.0 / sr;

    // Echo time: dotted eighth, tempo-synced.
    const int echoLen = (int) echoBuf[0].size();
    const int echoDelay = juce::jlimit (1, echoLen - 1, (int) std::round (0.75 * 60.0 / std::max (40.0, s.bpm) * sr));

    // ---------------- Pass 1: tone shaping + creative FX ----------------
    for (int i = 0; i < n; ++i)
    {
        const float impact = sImpact.getNextValue();
        const float punch  = sPunch.getNextValue();
        const float width  = sWidth.getNextValue();
        const float warmth = sWarmth.getNextValue();
        const float loud   = sLoudness.getNextValue();
        const float hp = sHp.getNextValue(), lp = sLp.getNextValue();
        const float gate = sGate.getNextValue(), echo = sEcho.getNextValue();

        const float inL = L[i];
        const float inR = stereo ? R[i] : inL;
        dryL[(size_t) i] = inL; dryR[(size_t) i] = inR;
        float l = inL, r = inR;

        // 1. Transient shaper
        {
            const float det = std::max (std::abs (l), std::abs (r));
            fastEnv = det > fastEnv ? fastAtt * fastEnv + (1 - fastAtt) * det : fastRel * fastEnv + (1 - fastRel) * det;
            slowEnv = det > slowEnv ? slowAtt * slowEnv + (1 - slowAtt) * det : slowRel * slowEnv + (1 - slowRel) * det;
            if (punch > 1.0e-4f)
            {
                const float ratio = juce::jlimit (1.0f, 4.0f, (fastEnv + 1.0e-6f) / (slowEnv + 1.0e-6f));
                const float g = std::pow (ratio, punch * 1.1f);
                l *= g; r *= g;
            }
        }
        // 2. Glue compressor
        {
            const float thr = -6.0f - impact * 14.0f;
            const float ratio = 1.0f + impact * 2.5f;
            const float slope = 1.0f - 1.0f / ratio;
            const float knee = 6.0f;
            const float lvl = gainToDb (std::max (std::abs (l), std::abs (r)));
            const float over = lvl - thr;
            float gr = 0.0f;
            if (over >= knee * 0.5f)       gr = slope * over;
            else if (over > -knee * 0.5f)  gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            compGr = gr > compGr ? compAtt * compGr + (1 - compAtt) * gr : compRel * compGr + (1 - compRel) * gr;
            if (impact > 1.0e-4f)
            {
                const float g = dbToGain (-compGr + impact * 3.0f);
                l *= g; r *= g;
                blockGr = std::max (blockGr, compGr);
            }
        }
        // 3. Tone
        if (++toneCounter >= 32)
        {
            toneCounter = 0;
            if (std::abs (warmth - lastWarmth) > 1.0e-4f) updateToneFilters (warmth);
        }
        l = highShelf.process (lowShelf.process (l, 0), 0);
        r = stereo ? highShelf.process (lowShelf.process (r, 1), 1) : l;
        // 4. Width with mono bass
        if (stereo)
        {
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            const float sideLow = sideLowPass2.process (sideLowPass.process (side, 0), 0);
            const float sideHigh = side - sideLow;
            const float lowGain = width <= 0.5f ? width * 2.0f : 1.0f - (width - 0.5f) * 2.0f * 0.85f;
            const float newSide = sideLow * lowGain + sideHigh * width * 2.0f;
            l = mid + newSide; r = mid - newSide;
        }
        // 5. Density
        if (loud > 1.0e-4f)
        {
            const float d = 1.0f + loud * 0.9f;
            l = std::tanh (l * d) / d;
            r = std::tanh (r * d) / d;
        }

        // 6. Creative FX ------------------------------------------------------------
        if (++fxCounter >= 16)
        {
            fxCounter = 0;
            hpf.set (sr, 20.0 * std::pow (100.0, (double) hp), 0.9f);              // 20 Hz -> 2 kHz
            lpf.set (sr, 20000.0 * std::pow (0.0125, (double) lp), 0.9f);          // 20 kHz -> 250 Hz
        }
        {
            float lo, hi;
            hpf.tick (l, 0, lo, hi); if (hp > 5.0e-4f) l = hi;
            if (stereo) { hpf.tick (r, 1, lo, hi); if (hp > 5.0e-4f) r = hi; }
            lpf.tick (l, 0, lo, hi); if (lp > 5.0e-4f) l = lo;
            if (stereo) { lpf.tick (r, 1, lo, hi); if (lp > 5.0e-4f) r = lo; }
            if (! stereo) r = l;
        }
        // Gate: 1/16 trance gate synced to the host
        {
            double q = 0;
            if (ppqStart >= 0.0) q = ppqStart + (double) i * beatsPerSample;
            else { freePhase += beatsPerSample; q = freePhase; }
            const double frac = q * 4.0 - std::floor (q * 4.0);
            const float target = (gate > 1.0e-4f && frac >= 0.5) ? 1.0f - gate : 1.0f;
            gateEnv = gateCoef * gateEnv + (1 - gateCoef) * target;
            if (gateEnv < 0.99999f) { l *= gateEnv; r *= gateEnv; }
        }
        // Echo: ping-pong dotted eighth with darkened feedback
        {
            int rd = echoPos - echoDelay; if (rd < 0) rd += echoLen;
            const float yL = echoBuf[0][(size_t) rd], yR = echoBuf[1][(size_t) rd];
            echoLp[0] += 0.35f * (yR - echoLp[0]);
            echoLp[1] += 0.35f * (yL - echoLp[1]);
            const float send = 0.5f * (l + r) * echo;
            echoBuf[0][(size_t) echoPos] = send + 0.45f * echoLp[0];
            echoBuf[1][(size_t) echoPos] = 0.45f * echoLp[1];
            if (++echoPos >= echoLen) echoPos = 0;
            if (echo > 1.0e-5f || std::abs (yL) + std::abs (yR) > 1.0e-6f)
            {
                l += 0.8f * yL; r += 0.8f * (stereo ? yR : yL);
            }
        }
        L[i] = l;
        if (stereo) R[i] = r;
        wetL[(size_t) i] = l; wetR[(size_t) i] = r;
    }

    // Space: plate-like reverb on a copy, blended in pass 2.
    reverb.processStereo (wetL.data(), wetR.data(), n);

    // ---------------- Pass 2: amount, trims, drive, look-ahead limiter, A/B ----------------
    const int W = lookahead + 1;           // ring sizes
    for (int i = 0; i < n; ++i)
    {
        const float amount = sAmount.getNextValue();
        const float space  = sSpace.getNextValue();
        const float trim   = sTrim.getNextValue();
        const float drive  = dbToGain (sDrive.getNextValue());
        const float ceil   = dbToGain (sCeil.getNextValue());
        const float keep   = sDry.getNextValue();
        const float byp    = sBypass.getNextValue();

        float l = L[i] + space * 0.55f * wetL[(size_t) i];
        float r = (stereo ? R[i] : L[i]) + space * 0.55f * (stereo ? wetR[(size_t) i] : wetL[(size_t) i]);
        const float dl = dryL[(size_t) i], dr = dryR[(size_t) i];
        l = dl + amount * (l - dl);
        r = dr + amount * (r - dr);

        // ---- Mastering: tonal balance shelves
        const float eqLo = sEqLow.getNextValue(), eqHi = sEqHigh.getNextValue();
        if (++eqCounter >= 64)
        {
            eqCounter = 0;
            if (std::abs (eqLo - lastEqLow) > 0.01f) { mEqLow.setLowShelf (sr, 110.0, eqLo); lastEqLow = eqLo; }
            if (std::abs (eqHi - lastEqHigh) > 0.01f) { mEqHigh.setHighShelf (sr, 9000.0, eqHi); lastEqHigh = eqHi; }
        }
        if (std::abs (lastEqLow) > 0.01f || std::abs (lastEqHigh) > 0.01f)
        {
            l = mEqHigh.process (mEqLow.process (l, 0), 0);
            r = stereo ? mEqHigh.process (mEqLow.process (r, 1), 1) : l;
        }
        // ---- Mastering: corrective bells (low-mid build-up, presence)
        {
            const float mud = sEqMud.getNextValue(), pres = sEqPres.getNextValue();
            if (eqCounter == 0)
            {
                if (std::abs (mud - lastEqMud) > 0.01f) { mEqMud.setPeak (sr, 300.0, 0.9, mud); lastEqMud = mud; }
                if (std::abs (pres - lastEqPres) > 0.01f) { mEqPres.setPeak (sr, 3200.0, 1.0, pres); lastEqPres = pres; }
            }
            if (std::abs (lastEqMud) > 0.01f || std::abs (lastEqPres) > 0.01f)
            {
                l = mEqPres.process (mEqMud.process (l, 0), 0);
                r = stereo ? mEqPres.process (mEqMud.process (r, 1), 1) : l;
            }
        }
        // ---- Mastering: width (sides above 120 Hz) and mono bass (sides below)
        {
            const float sideDb = sSide.getNextValue(), mono = sMono.getNextValue();
            const float sideHp = msSideLoB.process (msSideLoA.process (0.5f * (l - r), 0), 0);   // always run: no state jump
            if (stereo && (std::abs (sideDb) > 0.01f || mono > 0.001f))
            {
                const float m = 0.5f * (l + r), sd = 0.5f * (l - r);
                const float ns = dbToGain (sideDb) * (sd + mono * (sideHp - sd));
                l = m + ns; r = m - ns;
            }
        }

        // ---- Mastering: 3-band leveller. Each band's peaks are held near its own average (+3 dB crest),
        // so a loud kick or a harsh hat no longer drives the whole limiter.
        const float glue = sGlue.getNextValue();
        const float levOn = sLevOn.getNextValue();
        if (levOn > 1.0e-4f)
        {
            float bl[3], br[3];
            for (int c = 0; c < (stereo ? 2 : 1); ++c)
            {
                const float x = c == 0 ? l : r;
                const float lo = xLow1b.process (xLow1a.process (x, c), c);
                const float rest = xHigh1b.process (xHigh1a.process (x, c), c);
                const float mid = xLow2b.process (xLow2a.process (rest, c), c);
                const float hi = xHigh2b.process (xHigh2a.process (rest, c), c);
                const float loAligned = apLowB.process (apLowA.process (lo, c), c) + apHighB.process (apHighA.process (lo, c), c);
                float* dst = c == 0 ? bl : br;
                dst[0] = loAligned; dst[1] = mid; dst[2] = hi;
            }
            if (! stereo) for (int k = 0; k < 3; ++k) br[k] = bl[k];
            float yl = 0, yr = 0;
            for (int k = 0; k < 3; ++k)
            {
                // Level (not waveform) leveller: the band's short-term energy against its own long-term average.
                // A noisy band (hats) is not turned down for its natural crest, only for real bursts.
                const float e = 0.5f * (bl[k] * bl[k] + br[k] * br[k]);
                bandEnv[k] = e > bandEnv[k] ? bandAtt * bandEnv[k] + (1 - bandAtt) * e : bandRel * bandEnv[k] + (1 - bandRel) * e;
                bandRms[k] = bandRmsCoef * bandRms[k] + (1 - bandRmsCoef) * e;
                const float over = 10.0f * std::log10 (bandEnv[k] + 1.0e-12f) - (10.0f * std::log10 (bandRms[k] + 1.0e-12f) + 3.0f);
                const float want = over > 0 ? std::min (4.0f, 0.5f * over) * glue : 0.0f;
                bandGr[k] = bandGrSmooth * bandGr[k] + (1 - bandGrSmooth) * want;
                const float g = dbToGain (-bandGr[k]);
                yl += bl[k] * g; yr += br[k] * g;
            }
            // The band sum is an allpass version of the input: once on, the output is the band sum alone (blending it
            // with the dry signal would comb-filter around the crossovers). The crossfade only covers switching on/off.
            l += levOn * (yl - l);
            r += levOn * (yr - r);
        }

        l *= trim * drive; r *= trim * drive;

        // ---- Mastering: soft clipper in front of the limiter (only the sharpest peaks, smooth knee).
        const float clipAmt = sClip.getNextValue();
        if (clipAmt > 1.0e-4f)
        {
            const float knee = ceil * dbToGain (-3.0f * clipAmt), top = ceil * dbToGain (0.5f);
            auto soft = [&] (float x)
            {
                const float ax = std::abs (x);
                if (ax <= knee) return x;
                const float y = knee + (top - knee) * std::tanh ((ax - knee) / (top - knee));
                blockClip = std::max (blockClip, ax / y);
                return x < 0 ? -y : y;
            };
            l = soft (l); r = soft (r);
        }

        // ---- Mastering: band-limit just below Nyquist (the clipper and saturation fold energy up there).
        if (glue > 1.0e-4f)
        {
            l = nyqB.process (nyqA.process (l, 0), 0);
            r = stereo ? nyqB.process (nyqA.process (r, 1), 1) : l;
        }

        // True-peak estimate: 4x oversampled (polyphase FIR) on the incoming signal.
        float pk = std::max (std::abs (l), std::abs (r));
        for (int c = 0; c < (stereo ? 2 : 1); ++c)
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
        const float tpCeil = ceil * 0.977f;   // -0.2 dB detector margin for the residual inter-sample error
        const float req = pk > tpCeil ? tpCeil / pk : 1.0f;

        // Sliding minimum of the required gain over the look-ahead window (monotonic queue).
        // The slot being reused holds the sample that just left the window: expire it first.
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

        // Held minimum -> moving average (linear ramp, fully down before the peak leaves the look-ahead)
        rampSum += (double) gmin - (double) rampBuf[(size_t) rampPos];
        rampBuf[(size_t) rampPos] = gmin;
        if (++rampPos >= rampLen) rampPos = 0;
        const float ramped = std::min (1.0f, (float) (rampSum / rampLen));
        if (ramped < limGain) limGain = ramped;
        else                  limGain = limRel * limGain + (1 - limRel) * ramped;

        // Delay lines (signal + dry), read the sample that entered `lookahead` samples ago.
        laBuf[0][(size_t) laPos] = l; laBuf[1][(size_t) laPos] = r;
        dryDelay[0][(size_t) laPos] = dl; dryDelay[1][(size_t) laPos] = dr;
        const int rd = (laPos + 1) % W;
        laPos = rd;
        float ol = laBuf[0][(size_t) rd] * limGain, orr = laBuf[1][(size_t) rd] * limGain;
        const float ddl = dryDelay[0][(size_t) rd], ddr = dryDelay[1][(size_t) rd];
        ol = juce::jlimit (-ceil, ceil, ol);
        orr = juce::jlimit (-ceil, ceil, orr);
        blockGr = std::max (blockGr, -gainToDb (limGain));
        blockTp = std::max (blockTp, std::max (std::abs (ol), std::abs (orr)));

        // Loudness (output = processed, input = aligned dry) and the A/B match gain.
        {
            const float ki0 = kIn.process (ddl, 0), ki1 = stereo ? kIn.process (ddr, 1) : 0.0f;
            const float ko0 = kOut.process (ol, 0), ko1 = stereo ? kOut.process (orr, 1) : 0.0f;
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

        // Keep Original (untouched, aligned) and A/B bypass (optionally level-matched).
        ol += keep * (ddl - ol);
        orr += keep * (ddr - orr);
        const float gm = s.matchAB ? matchGain : 1.0f;
        ol += byp * (ddl * gm - ol);
        orr += byp * (ddr * gm - orr);

        L[i] = ol;
        if (stereo) R[i] = orr;
    }

    grDb.store (std::max (grDb.load(), blockGr));
    clipOut.store (gainToDb (blockClip));
    for (int k = 0; k < 3; ++k) bandGrOut[(size_t) k].store (bandGr[k]);
    peakHoldDb = std::max (peakHoldDb, gainToDb (blockTp));
    truePeakDb.store (std::max (truePeakDb.load(), peakHoldDb));
    {
        const float blockDb = gainToDb (blockTp);
        float prev = maxPeakDb.load();
        while (blockDb > prev && ! maxPeakDb.compare_exchange_weak (prev, blockDb)) {}
    }
}
