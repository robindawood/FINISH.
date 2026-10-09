#include "FinishDSP.h"

namespace
{
    inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float powerToLufs (float p) noexcept { return -0.691f + 10.0f * std::log10 (std::max (p, 1.0e-12f)); }
}

void FinishDSP::prepare (double sampleRate, int, int numChannels)
{
    sr = sampleRate;
    channels = juce::jlimit (1, 2, numChannels);

    for (auto* s : { &sAmount, &sImpact, &sPunch, &sWidth, &sWarmth, &sLoudness })
        s->reset (sr, 0.05);
    sBypass.reset (sr, 0.02);
    sMatch.reset (sr, 0.1);
    sDry.reset (sr, 0.03);
    sTrim.reset (sr, 0.03);

    fastAtt = envCoeff (sr, 0.0005); fastRel = envCoeff (sr, 0.030);
    slowAtt = envCoeff (sr, 0.020);  slowRel = envCoeff (sr, 0.150);
    compAtt = envCoeff (sr, 0.010);  compRel = envCoeff (sr, 0.150);
    limRel  = envCoeff (sr, 0.080);
    matchCoef = envCoeff (sr, 3.0);
    shortCoef = envCoeff (sr, 0.4);
    matchSmoothCoef = envCoeff (sr, 0.3);

    sideLowPass.setLowPass (sr, 140.0, 0.7071);
    sideLowPass2.setLowPass (sr, 140.0, 0.7071);
    kIn.prepare (sr); kOut.prepare (sr);

    const MacroSettings neutral;
    sAmount.setCurrentAndTargetValue (neutral.amount);
    sImpact.setCurrentAndTargetValue (neutral.impact);
    sPunch.setCurrentAndTargetValue (neutral.punch);
    sWidth.setCurrentAndTargetValue (neutral.width);
    sWarmth.setCurrentAndTargetValue (neutral.warmth);
    sLoudness.setCurrentAndTargetValue (neutral.loudness);
    sBypass.setCurrentAndTargetValue (0.0f);
    sMatch.setCurrentAndTargetValue (1.0f);
    sDry.setCurrentAndTargetValue (0.0f);
    sTrim.setCurrentAndTargetValue (1.0f);
    reset();
}

void FinishDSP::reset()
{
    fastEnv = slowEnv = 0.0f;
    compGr = 0.0f;
    limGain = 1.0f;
    lowShelf.reset(); highShelf.reset(); sideLowPass.reset(); sideLowPass2.reset();
    kIn.reset(); kOut.reset();
    pIn = pOut = pInShort = pOutShort = 0.0f;
    matchGain = 1.0f;
    lastWarmth = -1.0f;
    toneCounter = 0;
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
    sBypass.setTargetValue (s.bypass ? 1.0f : 0.0f);
    sMatch.setTargetValue (s.levelMatch ? 1.0f : 0.0f);
    sDry.setTargetValue (juce::jlimit (0.0f, 1.0f, s.sectionDry));
    sTrim.setTargetValue (dbToGain (juce::jlimit (-30.0f, 3.0f, s.trimDb)));

    float* L = buffer.getWritePointer (0);
    float* R = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    const float ceiling = dbToGain (ceilingDb);
    float blockGr = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float amount = sAmount.getNextValue();
        const float impact = sImpact.getNextValue();
        const float punch  = sPunch.getNextValue();
        const float width  = sWidth.getNextValue();
        const float warmth = sWarmth.getNextValue();
        const float loud   = sLoudness.getNextValue();
        const float byp    = std::max (sBypass.getNextValue(), sDry.getNextValue());
        const float matchOn = sMatch.getNextValue();
        const float trim   = sTrim.getNextValue();

        const float dryL = L[i];
        const float dryR = R != nullptr ? R[i] : dryL;
        float l = dryL, r = dryR;

        // 1. Transient shaper (stereo-linked): boosts attacks by the fast/slow envelope ratio.
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

        // 2. Glue compressor (stereo-linked, soft knee, slow attack keeps the transients).
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
            }
            blockGr = std::max (blockGr, impact > 1.0e-4f ? compGr : 0.0f);
        }

        // 3. Tone (warmth / brightness shelves).
        if (++toneCounter >= 32)
        {
            toneCounter = 0;
            if (std::abs (warmth - lastWarmth) > 1.0e-4f) updateToneFilters (warmth);
        }
        l = highShelf.process (lowShelf.process (l, 0), 0);
        if (R != nullptr) r = highShelf.process (lowShelf.process (r, 1), 1);
        else r = l;

        // 4. Mid/side width with mono-bass protection below ~140 Hz.
        if (R != nullptr)
        {
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r);
            const float sideLow = sideLowPass2.process (sideLowPass.process (side, 0), 0);
            const float sideHigh = side - sideLow;
            const float highGain = width * 2.0f;
            const float lowGain = width <= 0.5f ? width * 2.0f : 1.0f - (width - 0.5f) * 2.0f * 0.85f;
            const float newSide = sideLow * lowGain + sideHigh * highGain;
            l = mid + newSide;
            r = mid - newSide;
        }

        // 5. Density: gentle soft saturation that rounds peaks before the limiter.
        if (loud > 1.0e-4f)
        {
            const float d = 1.0f + loud * 0.9f;
            l = std::tanh (l * d) / d;
            r = std::tanh (r * d) / d;
        }

        // 6. Finish amount = dry/wet (zero latency, so the blend is phase-coherent).
        l = dryL + amount * (l - dryL);
        r = dryR + amount * (r - dryR);

        // 7. Peak limiter with -1 dBFS ceiling (instant attack, smooth release, never overshoots).
        {
            const float drive = dbToGain (loud * amount * 10.0f);
            l *= drive; r *= drive;
            const float peak = std::max (std::abs (l), std::abs (r));
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            const float released = limRel * limGain + (1 - limRel);
            limGain = std::min (target, released);
            l *= limGain; r *= limGain;
            blockGr = std::max (blockGr, -gainToDb (limGain));
        }

        // 8. Loudness measurement + A/B level match (output loudness follows input loudness).
        {
            const float ki0 = kIn.process (dryL, 0), ki1 = R != nullptr ? kIn.process (dryR, 1) : 0.0f;
            const float ko0 = kOut.process (l, 0),   ko1 = R != nullptr ? kOut.process (r, 1) : 0.0f;
            const float powIn = ki0 * ki0 + ki1 * ki1;
            const float powOut = ko0 * ko0 + ko1 * ko1;
            pInShort = shortCoef * pInShort + (1 - shortCoef) * powIn;
            pOutShort = shortCoef * pOutShort + (1 - shortCoef) * powOut;

            if (pInShort > 1.0e-7f)   // freeze while silent so the match does not drift
            {
                pIn = matchCoef * pIn + (1 - matchCoef) * powIn;
                pOut = matchCoef * pOut + (1 - matchCoef) * powOut;
            }
            float target = 1.0f;
            if (pIn > 1.0e-9f && pOut > 1.0e-9f)
                target = juce::jlimit (dbToGain (-18.0f), dbToGain (6.0f), std::sqrt (pIn / pOut));
            matchGain = matchSmoothCoef * matchGain + (1 - matchSmoothCoef) * target;

            // Section trims come after the match: they are deliberate contrast, not loudness to compensate.
            const float g = (1.0f + matchOn * (matchGain - 1.0f)) * trim;
            l = juce::jlimit (-ceiling, ceiling, l * g);
            r = juce::jlimit (-ceiling, ceiling, r * g);
        }

        // 9. Bypass crossfade (click-free A/B).
        L[i] = l + byp * (dryL - l);
        if (R != nullptr) R[i] = r + byp * (dryR - r);
    }

    inLufs.store (powerToLufs (pInShort));
    outLufs.store (powerToLufs (pOutShort * (1.0f + sMatch.getCurrentValue() * (matchGain * matchGain - 1.0f))));
    grDb.store (blockGr);
    matchDb.store (gainToDb (matchGain));
}
