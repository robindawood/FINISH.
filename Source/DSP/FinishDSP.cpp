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

    sideLowPass.setLowPass (sr, 140.0, 0.7071);
    sideLowPass2.setLowPass (sr, 140.0, 0.7071);
    kIn.prepare (sr); kOut.prepare (sr);

    // Look-ahead: 1.5 ms (reported to the host as latency).
    lookahead = std::max (8, (int) std::round (sr * 0.0015));
    limAtt = envCoeff (sr, (double) lookahead / sr / 4.0);
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
    for (int c = 0; c < 2; ++c)
    {
        std::fill (laBuf[c].begin(), laBuf[c].end(), 0.0f);
        std::fill (dryDelay[c].begin(), dryDelay[c].end(), 0.0f);
        std::fill (echoBuf[c].begin(), echoBuf[c].end(), 0.0f);
        echoLp[c] = 0;
        hist[c][0] = hist[c][1] = hist[c][2] = 0;
    }
    echoPos = 0;
    reverb.reset();
    std::fill (reqGain.begin(), reqGain.end(), 1.0f);
    qHead = qTail = 0;
    laPos = 0;
    limGain = 1.0f;
    kIn.reset(); kOut.reset();
    pIn = pOut = pInShort = pOutShort = pOutMom = 0.0f;
    matchGain = 1.0f;
    corrDb = 0.0f;
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

    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    grDb.store (0.0f);
    truePeakDb.store (-100.0f);
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
    float blockGr = 0.0f, blockTp = 0.0f;
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
        l *= trim * drive; r *= trim * drive;

        // True-peak estimate (4-tap interpolation of the inter-sample point) on the incoming signal.
        float pk = std::max (std::abs (l), std::abs (r));
        for (int c = 0; c < (stereo ? 2 : 1); ++c)
        {
            const float x = c == 0 ? l : r;
            const float mid = 0.5625f * (hist[c][1] + hist[c][2]) - 0.0625f * (hist[c][0] + x);
            pk = std::max (pk, std::abs (mid));
            hist[c][0] = hist[c][1]; hist[c][1] = hist[c][2]; hist[c][2] = x;
        }
        const float req = pk > ceil ? ceil / pk : 1.0f;

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

        if (gmin < limGain) limGain = gmin + (limGain - gmin) * limAtt;
        else                limGain = limRel * limGain + (1 - limRel) * gmin;

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
    truePeakDb.store (std::max (truePeakDb.load(), gainToDb (blockTp)));
}
