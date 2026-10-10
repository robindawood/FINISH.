#include "MusicAnalyzer.h"
#include <iostream>
#include <map>

namespace
{
    // Krumhansl-Kessler key profiles
    constexpr float majorProfile[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float minorProfile[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    inline double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

    float percentile (std::vector<float> v, float p)
    {
        if (v.empty()) return 0.0f;
        const size_t k = (size_t) juce::jlimit (0.0f, (float) v.size() - 1.0f, p * (float) (v.size() - 1));
        std::nth_element (v.begin(), v.begin() + (long) k, v.end());
        return v[k];
    }

    struct Spectrum
    {
        // order = FFT size; windowLen < size = zero-padded (finer interpolation, same time resolution)
        explicit Spectrum (int order, int windowLen = 0) : fft (order), N (1 << order), W (windowLen > 0 ? windowLen : (1 << order)), win ((size_t) W), buf ((size_t) N * 2)
        {
            for (int i = 0; i < W; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / (W - 1));
        }
        // Magnitude spectrum of audio centred at `centre` (zero padded at the edges).
        const float* compute (const std::vector<float>& audio, long centre)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const long a = centre - W / 2;
            for (int i = 0; i < W; ++i)
            {
                const long k = a + i;
                buf[(size_t) i] = (k >= 0 && k < (long) audio.size()) ? audio[(size_t) k] * win[(size_t) i] : 0.0f;
            }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            return buf.data();
        }
        float magAt (double hz, double rate) const
        {
            const double bin = hz / rate * N;
            const int b = (int) bin;
            if (b < 1 || b + 1 >= N / 2) return 0.0f;
            return std::max (buf[(size_t) b], buf[(size_t) b + 1]);
        }
        // Magnitude only if the bin is a tonal peak (narrow, well above its neighbourhood); noise returns 0.
        float tonalAt (double hz, double rate) const
        {
            const int z = std::max (1, N / W);   // zero-padding factor
            const int b = (int) std::round (hz / rate * N);
            if (b < 9 * z || b + 9 * z >= N / 2) return 0.0f;
            int q = b; for (int k = b - z; k <= b + z; ++k) if (buf[(size_t) k] > buf[(size_t) q]) q = k;
            const float pk = buf[(size_t) q];
            if (buf[(size_t) q - 1] > pk || buf[(size_t) q + 1] > pk) return 0.0f;
            float around = 0; for (int k = 4; k <= 8; ++k) around += buf[(size_t) (q - k * z)] + buf[(size_t) (q + k * z)];
            around /= 10.0f;
            return pk > 3.0f * around ? pk : 0.0f;
        }
        bool isTonalPeak (int b) const
        {
            if (b < 9 || b + 9 >= N / 2) return false;
            const float pk = buf[(size_t) b];
            if (buf[(size_t) b - 1] > pk || buf[(size_t) b + 1] > pk) return false;
            float around = 0; for (int k = 4; k <= 8; ++k) around += buf[(size_t) (b - k)] + buf[(size_t) (b + k)];
            return pk > 3.0f * around / 10.0f;
        }
        juce::dsp::FFT fft;
        int N, W;
        std::vector<float> win;
        std::vector<float> buf;
    };

    // Pick the loop length (in bars, from candidates) whose repeats agree best. `sim(a, b)` compares bars.
    template <typename Sim>
    int chooseLoopBars (int nBars, Sim sim, float threshold = 0.6f)
    {
        for (int L : { 1, 2, 4, 8 })
        {
            if (L >= nBars) return std::max (1, std::min (L, nBars));
            float total = 0; int count = 0;
            for (int b = L; b < nBars; ++b) { total += sim (b, b - L); ++count; }
            if (count > 0 && total / (float) count >= threshold) return L;
        }
        return std::min (8, nBars);
    }
}

namespace MusicAnalyzer
{

float keyCorrelation (const std::array<float, 12>& chroma, int root, bool minor)
{
    const float* prof = minor ? minorProfile : majorProfile;
    float mx = 0, my = 0;
    for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) i]; my += prof[i]; }
    mx /= 12; my /= 12;
    float sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < 12; ++i)
    {
        const float x = chroma[(size_t) ((i + root) % 12)] - mx, y = prof[i] - my;
        sxy += x * y; sxx += x * x; syy += y * y;
    }
    return sxx > 1.0e-12f ? sxy / std::sqrt (sxx * syy) : 0.0f;
}

void diatonicTriads (int keyRoot, bool minor, int roots[7], bool isMinor[7])
{
    static const int majSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    static const bool majQual[7] = { false, true, true, false, false, true, true };   // I ii iii IV V vi vii°
    static const bool minQual[7] = { true, true, false, true, true, false, false };    // i ii° III iv v VI VII
    for (int d = 0; d < 7; ++d)
    {
        roots[d] = (keyRoot + (minor ? minSteps[d] : majSteps[d])) % 12;
        isMinor[d] = minor ? minQual[d] : majQual[d];
    }
}

MusicalDNA analyze (const MusicCapture::Snapshot& snap)
{
    MusicalDNA dna;
    if (! snap.valid()) return dna;
    const auto& x = snap.audio;
    const double rate = snap.rate;
    const long total = (long) x.size();

    // ------------------------------------------------------------------ onset envelope (hop ~11.6 ms)
    Spectrum s11 (11);   // pitch: 2048 points (~186 ms)
    Spectrum s9 (9);     // onsets: 512 points (~46 ms) for sharp timing
    const int hop = std::max (64, (int) std::round (rate * 0.0116));
    const int numFrames = (int) (total / hop);
    std::vector<float> fluxLow ((size_t) numFrames), fluxMid ((size_t) numFrames), fluxHigh ((size_t) numFrames), fluxBroad ((size_t) numFrames), frameEnergy ((size_t) numFrames);
    std::vector<std::array<float, 12>> frameChroma ((size_t) numFrames);
    constexpr int melLo = 57, melHi = 93, melN = melHi - melLo + 1;
    std::vector<std::array<float, melN>> frameMel ((size_t) numFrames);
    std::vector<float> prevLog ((size_t) s9.N / 2, 0.0f), prevMag ((size_t) s9.N / 2, 0.0f);
    std::vector<float> lowLin ((size_t) numFrames), lowCentroid ((size_t) numFrames);
    std::vector<bool> tonalScratch ((size_t) s9.N / 2 + 2, false);
    double tonalE = 0, chromaE = 0, hfNoiseE = 0, hfAllE = 0;
    const auto binOf = [&] (const Spectrum& sp, double hz) { return juce::jlimit (1, sp.N / 2 - 1, (int) std::round (hz / rate * sp.N)); };
    const int bLow0 = binOf (s9, 30), bLow1 = binOf (s9, 150), bMid0 = binOf (s9, 180), bMid1 = binOf (s9, 3000),
              bHi0 = binOf (s9, 3500), bHi1 = binOf (s9, std::min (5400.0, rate * 0.48)), bBr0 = binOf (s9, 1200), bBr1 = binOf (s9, std::min (4400.0, rate * 0.4)), bCentroid1 = binOf (s9, 260), bKick1 = binOf (s9, 100);
    const int bCh0 = binOf (s11, 175), bCh1 = binOf (s11, 2100);   // above kick/sub partials (bass is tracked separately)
    std::vector<int> binPc ((size_t) s11.N / 2, -1);
    for (int b = bCh0; b <= bCh1; ++b)
    {
        const double f = (double) b * rate / s11.N;
        binPc[(size_t) b] = ((int) std::round (12.0 * std::log2 (f / 440.0) + 69.0)) % 12;
    }
    double lowE = 0, highE = 0, allE = 0;
    for (int fr = 0; fr < numFrames; ++fr)
    {
        const long centre = (long) fr * hop;
        {
            const float* mag = s9.compute (x, centre);
            float fl = 0, fm = 0, fh = 0, e = 0, lin = 0, cNum = 0, cDen = 0, noiseFlux = 0; int noiseBins = 0;
            // bins near a tonal peak (notes) do not count as noise
            std::vector<bool>& tonal = tonalScratch;
            for (int b = bBr0 - 1; b <= bBr1 + 1; ++b) tonal[(size_t) b] = false;
            for (int b = bBr0; b <= bBr1; ++b) if (s9.isTonalPeak (b)) tonal[(size_t) b - 1] = tonal[(size_t) b] = tonal[(size_t) b + 1] = true;
            for (int b = 1; b < s9.N / 2; ++b)
            {
                const float lg = std::log1p (100.0f * mag[b]);
                const float d = std::max (0.0f, lg - prevLog[(size_t) b]);
                prevLog[(size_t) b] = lg;
                if (b >= bLow0 && b <= bLow1) { fl += d; lowE += mag[b] * mag[b]; }
                if (b >= bLow0 && b <= bKick1) lin += mag[b] * mag[b];
                else if (b >= bMid0 && b <= bMid1) fm += d;
                else if (b >= bHi0 && b <= bHi1) { fh += d; highE += mag[b] * mag[b]; }
                if (b >= bLow0 && b <= bCentroid1) { cNum += mag[b] * (float) b; cDen += mag[b]; }
                prevMag[(size_t) b] = mag[b];
                if (b >= bBr0 && b <= bBr1)
                {
                    hfAllE += (double) mag[b] * mag[b];
                    if (! tonal[(size_t) b]) { noiseFlux += d; ++noiseBins; hfNoiseE += (double) mag[b] * mag[b]; }
                }
                e += mag[b] * mag[b];
            }
            allE += e;
            fluxLow[(size_t) fr] = fl; fluxMid[(size_t) fr] = fm; fluxHigh[(size_t) fr] = fh; frameEnergy[(size_t) fr] = e;
            fluxBroad[(size_t) fr] = noiseBins > 0 ? noiseFlux / (float) noiseBins : 0.0f;
            lowLin[(size_t) fr] = std::sqrt (lin);   // low-band amplitude (onset taken against the recent minimum below)
            lowCentroid[(size_t) fr] = cDen > 1.0e-9f ? cNum / cDen * (float) (rate / s9.N) : 0.0f;
        }
        const float* mag = s11.compute (x, centre);
        auto& ch = frameChroma[(size_t) fr];
        ch.fill (0.0f);
        for (int b = bCh0; b <= bCh1; ++b) chromaE += (double) mag[b] * mag[b];
        for (int b = bCh0; b <= bCh1; ++b)
            if (s11.isTonalPeak (b)) { ch[(size_t) binPc[(size_t) b]] += mag[b]; tonalE += (double) mag[b] * mag[b]; }
        auto& ml = frameMel[(size_t) fr];
        for (int m = 0; m < melN; ++m)
        {
            const double f0 = midiToHz (melLo + m);
            float sal = 0, w = 1.0f;
            for (int h = 1; h <= 4; ++h, w *= 0.8f) sal += w * s11.tonalAt (f0 * h, rate);
            sal -= 0.5f * s11.tonalAt (f0 * 0.5, rate);        // an octave-below fundamental means f0 is a harmonic
            ml[(size_t) m] = std::max (0.0f, sal) * (1.0f + 0.012f * (float) m);   // the melody is usually the top voice
        }
    }

    // ------------------------------------------------------------------ tempo grid
    double bpm = snap.bpm;
    double beatAtStart = snap.beatAtStart;
    dna.tempoFromHost = snap.tempoKnown;
    const auto frameRate = rate / hop;
    if (! snap.tempoKnown || bpm < 40.0 || bpm > 250.0)
    {
        // Autocorrelation of the onset envelope, 70..180 BPM.
        std::vector<float> on ((size_t) numFrames);
        for (int i = 0; i < numFrames; ++i) on[(size_t) i] = fluxLow[(size_t) i] + fluxMid[(size_t) i] + 0.5f * fluxHigh[(size_t) i];
        double best = 0; double bestBpm = 120;
        for (double b = 70; b <= 180; b += 0.5)
        {
            const double lag = frameRate * 60.0 / b;
            double acc = 0;
            for (int i = (int) lag + 1; i < numFrames; ++i)
            {
                const double j = i - lag; const int j0 = (int) j; const double fr = j - j0;
                acc += on[(size_t) i] * ((1 - fr) * on[(size_t) j0] + fr * on[(size_t) std::min (j0 + 1, numFrames - 1)]);
            }
            acc *= 1.0 + 0.15 * std::exp (-std::pow ((b - 125.0) / 30.0, 2.0));   // gentle prior
            if (acc > best) { best = acc; bestBpm = b; }
        }
        bpm = bestBpm;
        // Phase: the offset with the strongest low-band comb.
        const double period = frameRate * 60.0 / bpm;
        double bestPh = 0, bestAcc = -1;
        for (double ph = 0; ph < period; ph += 1.0)
        {
            double acc = 0;
            for (double t = ph; t < numFrames; t += period) acc += fluxLow[(size_t) t];
            if (acc > bestAcc) { bestAcc = acc; bestPh = ph; }
        }
        beatAtStart = -(bestPh / frameRate) * bpm / 60.0;
    }
    dna.bpm = bpm;
    const double secPerBeat = 60.0 / bpm;
    const double totalBeats = (double) total / rate / secPerBeat;
    const auto timeOfBeat = [&] (double absBeat) { return (absBeat - beatAtStart) * secPerBeat; };   // seconds from audio start

    long kStart = (long) std::ceil (beatAtStart * 4.0 / 16.0 - 1.0e-6) * 16;      // first full bar
    long kEnd = (long) std::floor ((beatAtStart + totalBeats) * 4.0 + 1.0e-6);
    if (kEnd - kStart < 16) kStart = (long) std::ceil (beatAtStart * 4.0 - 1.0e-6);   // less than a bar after alignment
    const int nSteps = (int) std::max (0L, kEnd - kStart);
    const int nBars = nSteps / 16;
    if (nBars < 1) return dna;
    dna.barsAnalysed = nSteps / 16.0;

    // ------------------------------------------------------------------ per-step features
    const double stepSec = secPerBeat / 4.0;
    std::vector<float> onLow ((size_t) nSteps), onMid ((size_t) nSteps), onHigh ((size_t) nSteps), onBroad ((size_t) nSteps), onKick ((size_t) nSteps), stepEnergy ((size_t) nSteps);
    std::vector<std::array<float, 12>> stepChroma ((size_t) nSteps);
    std::vector<std::array<float, melN>> stepMel ((size_t) nSteps);
    std::vector<float> hatOffset ((size_t) nSteps, 0.0f);
    for (int j = 0; j < nSteps; ++j)
    {
        const double t0 = timeOfBeat ((kStart + j) / 4.0);
        const int f0 = (int) std::floor ((t0 - 0.03) * frameRate), f1 = (int) std::ceil ((t0 + 0.6 * stepSec) * frameRate);
        const int g0 = (int) std::floor (t0 * frameRate), g1 = (int) std::ceil ((t0 + stepSec) * frameRate);
        float ml = 0, mm = 0, mh = 0, mb = 0; int argH = f0;
        for (int f = std::max (0, f0); f <= std::min (numFrames - 1, f1); ++f)
        {
            ml = std::max (ml, fluxLow[(size_t) f]); mm = std::max (mm, fluxMid[(size_t) f]); mb = std::max (mb, fluxBroad[(size_t) f]);
            // kick = strong low-end attack whose pitch falls quickly (bass notes keep their pitch)
            const float drop = f + 3 < numFrames ? lowCentroid[(size_t) f] - lowCentroid[(size_t) f + 3] : 0.0f;
            float floorAmp = lowLin[(size_t) f];
            for (int k = 1; k <= 4 && f - k >= 0; ++k) floorAmp = std::min (floorAmp, lowLin[(size_t) (f - k)]);
            const float rise = std::max (0.0f, lowLin[(size_t) f] - floorAmp);
            onKick[(size_t) j] = std::max (onKick[(size_t) j], rise * (1.0f + 0.5f * juce::jlimit (0.0f, 1.0f, drop / 40.0f)));
            if (fluxHigh[(size_t) f] > mh) { mh = fluxHigh[(size_t) f]; argH = f; }
        }
        onLow[(size_t) j] = ml; onMid[(size_t) j] = mm; onHigh[(size_t) j] = mh; onBroad[(size_t) j] = mb;
        hatOffset[(size_t) j] = (float) (((double) argH / frameRate - t0) / stepSec);
        auto& ch = stepChroma[(size_t) j]; ch.fill (0.0f);
        auto& me = stepMel[(size_t) j]; me.fill (0.0f);
        float e = 0;
        for (int f = std::max (0, g0); f < std::min (numFrames, g1); ++f)
        {
            for (int c = 0; c < 12; ++c) ch[(size_t) c] += frameChroma[(size_t) f][(size_t) c];
            for (int m = 0; m < melN; ++m) me[(size_t) m] += frameMel[(size_t) f][(size_t) m];
            e += frameEnergy[(size_t) f];
        }
        stepEnergy[(size_t) j] = e;
    }
    auto normalise = [] (std::vector<float>& v)
    {
        const float p = std::max (1.0e-6f, percentile (v, 0.92f));
        for (auto& a : v) a = std::min (1.5f, a / p);
    };
    normalise (onLow); normalise (onMid); normalise (onHigh); normalise (onKick);

    // ------------------------------------------------------------------ groove
    std::array<float, 16> fl {}, fm {}, fh {}, fb {};
    for (int j = 0; j < nBars * 16; ++j)
    {
        fl[(size_t) (j % 16)] += onKick[(size_t) j]; fm[(size_t) (j % 16)] += onMid[(size_t) j];
        fh[(size_t) (j % 16)] += onHigh[(size_t) j]; fb[(size_t) (j % 16)] += onBroad[(size_t) j];
    }
    auto periodicity = [] (const std::array<float, 16>& a)
    {
        std::array<float, 16> s = a;
        std::sort (s.begin(), s.end(), std::greater<float>());
        const float top = (s[0] + s[1] + s[2] + s[3]) / 4.0f;
        float mean = 0; for (auto v : a) mean += v; mean /= 16.0f;
        return mean > 1.0e-6f ? top / mean : 0.0f;
    };
    const float perLow = periodicity (fl), perHigh = periodicity (fh);
    auto norm16 = [] (std::array<float, 16>& a) { const float m = *std::max_element (a.begin(), a.end()); if (m > 1.0e-6f) for (auto& v : a) v /= m; };
    // An onset spills into the next 16th (window overlap): keep the first step of each hit.
    auto despill = [] (std::array<float, 16>& a)
    {
        std::array<float, 16> o = a;
        for (int q = 0; q < 16; ++q) o[(size_t) q] = std::max (0.0f, a[(size_t) q] - 0.7f * a[(size_t) ((q + 15) % 16)]);
        a = o;
    };
    despill (fl); despill (fh); despill (fb);
    if (std::getenv ("FINISH_DEBUG_FRAMES") != nullptr)
    {
        const double t0 = timeOfBeat (kStart / 4.0);
        for (int f = (int) (t0 * frameRate) - 2; f < (int) ((t0 + 2 * secPerBeat) * frameRate) && f < numFrames; ++f)
            if (f >= 0) std::cout << "    f " << juce::String ((f / frameRate - t0) / secPerBeat * 4.0, 2) << " lin " << lowLin[(size_t) f]
                                  << " cen " << lowCentroid[(size_t) f] << " flux " << fluxLow[(size_t) f] << "\n";
    }
    if (std::getenv ("FINISH_DEBUG_DNA") != nullptr)
    {
        std::cout << "    [dbg] low:"; for (auto v : fl) std::cout << " " << juce::String (v / nBars, 2);
        std::cout << "\n    [dbg] mid:"; for (auto v : fm) std::cout << " " << juce::String (v / nBars, 2);
        std::cout << "\n    [dbg] hi :"; for (auto v : fh) std::cout << " " << juce::String (v / nBars, 2);
        std::cout << "\n    [dbg] brd:"; for (auto v : fb) std::cout << " " << juce::String (v / nBars, 3);
        std::cout << "\n";
    }
    const float perBroad = periodicity (fb);
    float broadMean = 0; for (auto v : fb) broadMean += v / (16.0f * (float) nBars);
    if (std::getenv ("FINISH_DEBUG_DNA") != nullptr) std::cout << "    [dbg] perLow " << perLow << " perBroad " << perBroad << " broadMean " << broadMean << "\n";
    const float noiseShare = hfAllE > 0 ? (float) (hfNoiseE / hfAllE) : 0.0f;
    if (std::getenv ("FINISH_DEBUG_DNA") != nullptr) std::cout << "    [dbg] noiseShare " << noiseShare << " lowShare " << (allE > 0 ? lowE / allE : 0.0) << "\n";
    if (perBroad > 1.8f && broadMean > 0.03f && noiseShare > 0.35f) dna.hasDrums = true;
    norm16 (fl); norm16 (fm); norm16 (fh); norm16 (fb);
    const float lowShare = allE > 0 ? (float) (lowE / allE) : 0.0f;
    dna.hasDrums = dna.hasDrums || (perLow > 1.7f && lowShare > 0.04f);
    juce::ignoreUnused (perHigh);
    for (int p = 0; p < 16; ++p)
    {
        dna.kick[(size_t) p] = fl[(size_t) p];
        // snare / clap = broadband noise bursts (tonal onsets only raise a few bins)
        dna.snare[(size_t) p] = (perBroad > 1.6f && broadMean > 0.03f && noiseShare > 0.3f) ? fb[(size_t) p] : 0.0f;
        dna.hats[(size_t) p] = fh[(size_t) p];
    }
    norm16 (dna.snare);
    dna.halfTime = dna.hasDrums && dna.snare[8] > 0.7f && dna.snare[4] < 0.45f && dna.snare[12] < 0.45f;
    {
        float odd = 0, even = 0; int no = 0, ne = 0;
        int oddHatSteps = 0; for (int q = 1; q < 16; q += 2) oddHatSteps += fh[(size_t) q] > 0.45f;
        if (oddHatSteps >= 2)
        for (int j = 0; j < nBars * 16; ++j)
            if (onHigh[(size_t) j] > 0.5f) { if (j % 2) { odd += hatOffset[(size_t) j]; ++no; } else { even += hatOffset[(size_t) j]; ++ne; } }
        if (no > 3 && ne > 3) dna.swing = juce::jlimit (0.0f, 0.33f, odd / no - even / ne);
    }

    bool kickInferred = false;
    // ------------------------------------------------------------------ bass (eighth notes, 4096-point FFT)
    Spectrum s12 (13, (int) std::round (rate * 0.17));   // 170 ms window, 8192-point zero-padded
    const int nEighths = nSteps / 2;
    std::vector<int> bassPitch ((size_t) nEighths, -1);
    std::vector<float> bassSal ((size_t) nEighths, 0.0f);
    std::array<float, 12> bassChroma {};
    constexpr int bassLo = 28, bassN = 28;
    std::vector<std::array<float, bassN>> bassSalAll ((size_t) nEighths);
    for (int e = 0; e < nEighths; ++e)
    {
        // late in the eighth: a kick hit on the same 8th has mostly decayed by then
        const double t = timeOfBeat ((kStart + 2 * e) / 4.0) + stepSec * 1.3;
        s12.compute (x, (long) (t * rate));
        for (int m = 0; m < bassN; ++m)
        {
            const double f = midiToHz (bassLo + m);
            bassSalAll[(size_t) e][(size_t) m] = std::max (0.0f, s12.tonalAt (f, rate) + 0.6f * s12.tonalAt (2 * f, rate) + 0.4f * s12.tonalAt (3 * f, rate)
                                                                 - 0.4f * s12.tonalAt (f * 0.5, rate));
        }
    }
    // A kick's tail is a stable low sine: its pitch is loud where the kick hits and weak elsewhere.
    int kickTone = -1;
    if (dna.hasDrums && nEighths > 0)
    {
        std::array<float, bassN> atKick {}, elsewhere {};
        int nk = 0, no = 0;
        for (int e = 0; e < nEighths; ++e)
        {
            const bool kickHere = dna.kick[(size_t) ((e * 2) % 16)] > 0.5f;
            for (int m = 0; m < bassN; ++m) (kickHere ? atKick : elsewhere)[(size_t) m] += bassSalAll[(size_t) e][(size_t) m];
            (kickHere ? nk : no)++;
        }
        if (nk > 0 && no > 0)
        {
            const int km = (int) (std::max_element (atKick.begin(), atKick.end()) - atKick.begin());
            const float k = atKick[(size_t) km] / nk, o = elsewhere[(size_t) km] / no;
            if (std::getenv ("FINISH_DEBUG_DNA") != nullptr) std::cout << "    [dbg] kick tone " << km + bassLo << " at kick " << k << " elsewhere " << o << "\n";
            if (o < 0.6f * k) kickTone = km;
        }
    }
    for (int e = 0; e < nEighths; ++e)
    {
        float best = 0; int bm = -1;
        for (int m = 0; m < bassN; ++m)
            if ((kickTone < 0 || std::abs (m - kickTone) > 1) && bassSalAll[(size_t) e][(size_t) m] > best) { best = bassSalAll[(size_t) e][(size_t) m]; bm = m; }
        bassPitch[(size_t) e] = bm >= 0 ? bassLo + bm : -1;
        bassSal[(size_t) e] = best;
    }
    float audioRms = 0.0f;
    { double sq = 0; for (auto v : x) sq += (double) v * v; audioRms = (float) std::sqrt (sq / std::max<long> (1, total)); }
    {
        const float ref = percentile (bassSal, 0.8f);
        const float bassLevel = audioRms > 1.0e-6f ? ref / (audioRms * (float) s12.W * 0.5f) : 0.0f;
        if (std::getenv ("FINISH_DEBUG_DNA") != nullptr) std::cout << "    [dbg] bass level " << bassLevel << "\n";
        int voiced = 0;
        for (int e = 0; e < nEighths; ++e)
        {
            if (bassPitch[(size_t) e] < 0 || bassSal[(size_t) e] < ref * 0.35f || ref <= 1.0e-6f) bassPitch[(size_t) e] = -1;
            else { ++voiced; bassChroma[(size_t) (bassPitch[(size_t) e] % 12)] += bassSal[(size_t) e]; }
        }
        dna.hasBass = nEighths > 0 && voiced > nEighths * 0.3f && lowShare > 0.06f && bassLevel > 0.08f;
    }

    // ------------------------------------------------------------------ kick vs bass
    // Low-end onsets that coincide with bass notes starting are mostly the bass: discount them.
    if (dna.hasBass && nBars > 0)
    {
        std::array<float, 16> bassStart {};
        for (int e = 0; e < nBars * 8 && e < nEighths; ++e)
        {
            const int p = bassPitch[(size_t) e], prev = e > 0 ? bassPitch[(size_t) e - 1] : -1;
            if (p >= 0 && (prev < 0 || prev != p || onLow[(size_t) (e * 2)] > 0.5f)) bassStart[(size_t) ((e * 2) % 16)] += 1.0f / (float) nBars;
        }
        for (int q = 0; q < 16; ++q) if (bassStart[(size_t) q] > 0.5f) dna.kick[(size_t) q] *= 0.35f;
        auto mx = *std::max_element (dna.kick.begin(), dna.kick.end());
        if (mx > 1.0e-6f) for (auto& v : dna.kick) v /= mx;
        // A dance groove whose bass sits on the off-beats almost always has the kick on the beats.
        int offbeatBass = 0; for (int q = 2; q < 16; q += 4) offbeatBass += bassStart[(size_t) q] > 0.5f;
        if (offbeatBass >= 3 && dna.hasDrums && bpm >= 110 && bpm <= 140 && (dna.kick[0] < 0.55f || dna.kick[4] < 0.55f))
        {
            for (int q = 0; q < 16; ++q) dna.kick[(size_t) q] = (q % 4 == 0) ? 1.0f : std::min (dna.kick[(size_t) q], 0.3f);
            kickInferred = true;
        }
    }
    dna.fourOnFloor = dna.hasDrums && dna.kick[0] > 0.55f && dna.kick[4] > 0.55f && dna.kick[8] > 0.55f && dna.kick[12] > 0.55f;

    // ------------------------------------------------------------------ key
    std::array<float, 12> chroma {};
    for (int j = 0; j < nSteps; ++j)
    {
        float s = 0; for (auto v : stepChroma[(size_t) j]) s += v;
        if (s <= 1.0e-9f) continue;
        const float w = std::sqrt (s);
        for (int c = 0; c < 12; ++c) chroma[(size_t) c] += stepChroma[(size_t) j][(size_t) c] / s * w;
    }
    {
        float cs = 0, bs = 0;
        for (int c = 0; c < 12; ++c) { cs += chroma[(size_t) c]; bs += bassChroma[(size_t) c]; }
        if (cs > 0) for (auto& v : chroma) v /= cs;
        if (bs > 0 && dna.hasBass) for (int c = 0; c < 12; ++c) chroma[(size_t) c] += 0.6f * bassChroma[(size_t) c] / bs;
    }
    dna.chroma = chroma;
    {
        float best = -2, second = -2; int bk = 0, sk = 0;
        for (int k = 0; k < 24; ++k)
        {
            const float c = keyCorrelation (chroma, k % 12, k >= 12);
            if (c > best) { second = best; sk = bk; best = c; bk = k; }
            else if (c > second) { second = c; sk = k; }
        }
        dna.keyRoot = bk % 12; dna.minor = bk >= 12;
        dna.altKeyRoot = sk % 12; dna.altMinor = sk >= 12;
        dna.keyConfidence = juce::jlimit (0.0f, 1.0f, (best - second) * 4.0f + (best - 0.45f));
    }

    bool pitchedMaterial = true;
    juce::ignoreUnused (kickInferred);
    // ------------------------------------------------------------------ chords (one per bar)
    {
        int roots[7]; bool qual[7];
        diatonicTriads (dna.keyRoot, dna.minor, roots, qual);
        std::vector<int> barDeg ((size_t) nBars, 0);
        std::vector<float> barMargin ((size_t) nBars, 0.0f);
        float harmonic = 0;
        for (int b = 0; b < nBars; ++b)
        {
            std::array<float, 12> c {}; std::array<float, 12> bc {};
            for (int j = b * 16; j < b * 16 + 16; ++j)
            {
                float s = 0; for (auto v : stepChroma[(size_t) j]) s += v;
                if (s > 0) for (int i = 0; i < 12; ++i) c[(size_t) i] += stepChroma[(size_t) j][(size_t) i] / s;
            }
            for (int e = b * 8; e < b * 8 + 8 && e < nEighths; ++e)
                if (bassPitch[(size_t) e] >= 0) bc[(size_t) (bassPitch[(size_t) e] % 12)] += 1.0f;
            float cs = 0; for (auto v : c) cs += v;
            if (cs > 0) for (auto& v : c) v /= cs;
            float mx = *std::max_element (c.begin(), c.end());
            harmonic += mx;
            float best = -1, second = -1; int bd = 0;
            for (int d = 0; d < 7; ++d)
            {
                const int r = roots[d];
                const float sc = c[(size_t) r] + c[(size_t) ((r + (qual[d] ? 3 : 4)) % 12)] * 0.8f + c[(size_t) ((r + 7) % 12)] * 0.6f
                               + (dna.hasBass ? 0.12f * bc[(size_t) r] / 8.0f * 8.0f / 8.0f : 0.0f) + (d == 0 ? 0.01f : 0.0f);
                if (sc > best) { second = best; best = sc; bd = d; }
                else if (sc > second) second = sc;
            }
            barDeg[(size_t) b] = bd;
            barMargin[(size_t) b] = best - second;
        }
        harmonic /= (float) nBars;   // peaky chroma (> ~0.16) = pitched content
        const int loop = chooseLoopBars (nBars, [&] (int a, int b) { return barDeg[(size_t) a] == barDeg[(size_t) b] ? 1.0f : 0.0f; }, 0.7f);
        const int L = std::max (1, std::min (4, loop));
        float margin = 0;
        for (int i = 0; i < L; ++i)
        {
            // vote across repeats
            std::array<int, 7> votes {};
            for (int b = i; b < nBars; b += L) votes[(size_t) barDeg[(size_t) b]]++;
            const int d = (int) (std::max_element (votes.begin(), votes.end()) - votes.begin());
            dna.chordRoots.push_back (roots[d]);
            dna.chordMinor.push_back (qual[d]);
            margin += barMargin[(size_t) i];
        }
        dna.harmonyConfidence = juce::jlimit (0.0f, 1.0f, margin / (float) L * 6.0f);
        const float tonalShare = chromaE > 0 ? (float) (tonalE / chromaE) : 0.0f;
        if (std::getenv ("FINISH_DEBUG_DNA") != nullptr) std::cout << "    [dbg] harmonic " << harmonic << " tonalShare " << tonalShare << "\n";
        dna.hasChords = harmonic > 0.16f && dna.harmonyConfidence > 0.15f && tonalShare > 0.08f;
        pitchedMaterial = tonalShare > 0.08f;
        // One static "chord" under a lone melody is just the melody's own notes, not a harmony.
        bool allSame = true; for (auto r : dna.chordRoots) allSame &= r == dna.chordRoots.front();
        if (allSame && ! dna.hasBass) dna.hasChords = false;
    }

    // ------------------------------------------------------------------ relative major / minor
    // Same notes, different home: decide with the loop's first chord, where the bass rests, and a
    // small prior for minor (most electronic music). Lower the confidence when it was close.
    {
        const int majRoot = dna.minor ? (dna.keyRoot + 3) % 12 : dna.keyRoot;
        const int minRoot = (majRoot + 9) % 12;
        const float cMaj = keyCorrelation (chroma, majRoot, false), cMin = keyCorrelation (chroma, minRoot, true);
        if (std::getenv ("FINISH_DEBUG_DNA") != nullptr) std::cout << "    [dbg] key maj " << cMaj << " min " << cMin << " first chord " << (dna.chordRoots.empty() ? -1 : dna.chordRoots.front()) << " start beat " << beatAtStart << "\n";
        if (std::abs (cMaj - cMin) < 0.22f)
        {
            float scoreMin = 0.3f, scoreMaj = 0.0f;
            if (! dna.chordRoots.empty())
            {
                if (dna.chordRoots.front() == minRoot) scoreMin += 1.0f;
                if (dna.chordRoots.front() == majRoot) scoreMaj += 1.0f;
            }
            float bs = 0; for (auto v : bassChroma) bs += v;
            if (bs > 0) { scoreMin += bassChroma[(size_t) minRoot] / bs * 2.0f; scoreMaj += bassChroma[(size_t) majRoot] / bs * 2.0f; }
            const bool chooseMinor = scoreMin >= scoreMaj;
            if (chooseMinor != dna.minor)
            {
                dna.altKeyRoot = dna.keyRoot; dna.altMinor = dna.minor;
                dna.keyRoot = chooseMinor ? minRoot : majRoot; dna.minor = chooseMinor;
            }
            else { dna.altKeyRoot = chooseMinor ? majRoot : minRoot; dna.altMinor = ! chooseMinor; }
            dna.keyConfidence = std::min (dna.keyConfidence, 0.4f + 0.3f * std::abs (scoreMin - scoreMaj));
        }
    }

    // ------------------------------------------------------------------ bass line loop
    if (dna.hasBass)
    {
        const int L = chooseLoopBars (nBars, [&] (int a, int b)
        {
            int same = 0;
            for (int e = 0; e < 8; ++e)
            {
                const int pa = bassPitch[(size_t) (a * 8 + e)], pb = bassPitch[(size_t) (b * 8 + e)];
                same += (pa < 0 && pb < 0) || (pa >= 0 && pb >= 0 && pa % 12 == pb % 12);
            }
            return same / 8.0f;
        }, 0.6f);
        dna.bassBars = std::min (L, 4);
        const int n8 = dna.bassBars * 8;
        std::vector<int> line ((size_t) n8, -1);
        for (int e = 0; e < n8; ++e)
        {
            std::array<float, 12> votes {}; float rest = 0; int octaveSum = 0, octaveN = 0;
            for (int r = e; r < nBars * 8; r += n8)
            {
                const int p = bassPitch[(size_t) r];
                if (p < 0) rest += 1.0f; else { votes[(size_t) (p % 12)] += 1.0f; octaveSum += p; ++octaveN; }
            }
            const int pc = (int) (std::max_element (votes.begin(), votes.end()) - votes.begin());
            if (votes[(size_t) pc] >= rest && octaveN > 0)
            {
                int pitch = (int) std::round ((double) octaveSum / octaveN);
                pitch = pitch - ((pitch % 12) - pc);
                while (pitch < 28) pitch += 12;
                while (pitch > 52) pitch -= 12;
                line[(size_t) e] = pitch;
            }
        }
        for (int e = 0; e < n8; ++e)
        {
            if (line[(size_t) e] < 0) continue;
            int len = 1;
            // sustain while the same pitch continues without a new low onset
            while (e + len < n8 && line[(size_t) (e + len)] == line[(size_t) e])
            {
                float on = 0; int cnt = 0;
                for (int r = (e + len) * 2; r < nBars * 16; r += n8 * 2) { on += onLow[(size_t) r]; ++cnt; }
                if (cnt > 0 && on / cnt > 0.35f) break;
                ++len;
            }
            dna.bassLine.push_back ({ e * 0.5, len * 0.5 - 0.05, line[(size_t) e], 0.85f });
            e += len - 1;
        }
    }

    // ------------------------------------------------------------------ melody motif (16th steps)
    {
        std::vector<int> mel ((size_t) nSteps, -1);
        std::vector<float> clarity ((size_t) nSteps, 0.0f), strength ((size_t) nSteps, 0.0f);
        std::vector<std::array<float, melN>> clean ((size_t) nSteps);
        // Remove what sustains through the bar (pads, held chords): the melody is what moves.
        for (int b = 0; b * 16 < nSteps; ++b)
        {
            const int j0 = b * 16, j1 = std::min (nSteps, j0 + 16);
            for (int m = 0; m < melN; ++m)
            {
                std::vector<float> v;
                for (int j = j0; j < j1; ++j) v.push_back (stepMel[(size_t) j][(size_t) m]);
                const float base = 0.9f * percentile (v, 0.25f);
                for (int j = j0; j < j1; ++j) clean[(size_t) j][(size_t) m] = std::max (0.0f, stepMel[(size_t) j][(size_t) m] - base);
            }
        }
        std::vector<int> bestM ((size_t) nSteps, 0);
        for (int j = 0; j < nSteps; ++j)
        {
            const auto& sal = stepMel[(size_t) j];
            const auto& cl = clean[(size_t) j];
            float mean = 0, best = 0; int bm = 0;
            for (int m = 0; m < melN; ++m) { mean += sal[(size_t) m]; if (cl[(size_t) m] > best) { best = cl[(size_t) m]; bm = m; } }
            mean /= (float) melN;
            clarity[(size_t) j] = mean > 1.0e-9f ? sal[(size_t) bm] / mean : 0.0f;
            strength[(size_t) j] = best;
            bestM[(size_t) j] = bm;
        }
        const float strongRef = percentile (strength, 0.9f);
        float clarSum = 0; int voiced = 0;
        for (int j = 0; j < nSteps; ++j)
            if (strongRef > 1.0e-9f && strength[(size_t) j] > 0.3f * strongRef && clarity[(size_t) j] > 2.0f)
            {
                mel[(size_t) j] = melLo + bestM[(size_t) j];
                clarSum += clarity[(size_t) j]; ++voiced;
            }
        // pitch re-attack (same note struck again)
        auto reattack = [&] (int j) { if (j <= 0 || mel[(size_t) j] < 0) return false; const int m = mel[(size_t) j] - melLo;
                                      return clean[(size_t) j][(size_t) m] > 1.6f * clean[(size_t) j - 1][(size_t) m] + 1.0e-9f; };
        dna.hasMelody = voiced > nSteps * 0.25f && pitchedMaterial;
        dna.melodyConfidence = voiced > 0 ? juce::jlimit (0.0f, 1.0f, (clarSum / voiced - 2.0f) / 3.0f) : 0.0f;
        if (dna.hasMelody)
        {
            const int L = chooseLoopBars (nBars, [&] (int a, int b)
            {
                int same = 0, count = 0;
                for (int s = 0; s < 16; ++s)
                {
                    const int pa = mel[(size_t) (a * 16 + s)], pb = mel[(size_t) (b * 16 + s)];
                    if (pa < 0 && pb < 0) continue;
                    ++count; same += (pa >= 0 && pb >= 0 && pa % 12 == pb % 12);
                }
                return count > 0 ? (float) same / count : 1.0f;
            }, 0.5f);
            dna.melodyBars = std::min (L, 4);
            const int n16 = dna.melodyBars * 16;
            std::vector<int> line ((size_t) n16, -1);
            for (int s = 0; s < n16; ++s)
            {
                std::map<int, float> votes; float rest = 0;
                for (int r = s; r < nBars * 16; r += n16)
                {
                    if (mel[(size_t) r] < 0) rest += 1.0f; else votes[mel[(size_t) r]] += clarity[(size_t) r];
                }
                int bp = -1; float bv = 0;
                for (auto& [p, v] : votes) if (v > bv) { bv = v; bp = p; }
                if (bp >= 0 && bv >= rest * 2.0f * 0.8f) line[(size_t) s] = bp;
            }
            for (int s = 0; s < n16; ++s)
            {
                if (line[(size_t) s] < 0) continue;
                int len = 1;
                while (s + len < n16 && line[(size_t) (s + len)] == line[(size_t) s] && len < 8)
                {
                    int attacks = 0, cnt = 0;
                    for (int r = s + len; r < nBars * 16; r += n16) { attacks += reattack (r); ++cnt; }
                    if (cnt > 0 && attacks * 2 > cnt) break;
                    ++len;
                }
                dna.melody.push_back ({ s * 0.25, len * 0.25 - 0.02, line[(size_t) s], 0.8f });
                s += len - 1;
            }
            if (dna.melody.size() < 3) { dna.hasMelody = false; dna.melody.clear(); dna.melodyBars = 0; }
        }
    }

    // ------------------------------------------------------------------ character
    {
        double sq = 0; for (auto v : x) sq += (double) v * v;
        const float rmsDb = (float) (10.0 * std::log10 (std::max (1.0e-12, sq / (double) total)));
        dna.loudnessLufs = rmsDb - 0.7f;
        dna.energy = juce::jlimit (0.0f, 1.0f, juce::jmap (rmsDb, -36.0f, -8.0f, 0.0f, 1.0f));
        int onsets = 0;
        for (int j = 0; j < nSteps; ++j) onsets += (onLow[(size_t) j] > 0.5f || onMid[(size_t) j] > 0.5f || onHigh[(size_t) j] > 0.5f);
        dna.density = onsets / (float) std::max (1, nSteps) * 4.0f;
        dna.brightness = juce::jlimit (0.0f, 1.0f, allE > 0 ? (float) std::sqrt (highE / allE) * 6.0f : 0.0f);
    }

    // ------------------------------------------------------------------ style (a guess, always correctable)
    {
        const double b = bpm;
        const bool dark = dna.minor && dna.brightness < 0.55f;
        Style st; float conf;
        if (b >= 158)            { st = Style::DnB; conf = 0.7f; }
        else if (b >= 136)       { st = dna.halfTime ? Style::Trap : (dna.fourOnFloor || ! dna.hasDrums ? (dark ? Style::Techno : Style::Trance) : Style::Techno); conf = 0.45f; }
        else if (b >= 125)       { st = (dna.hasMelody || dna.hasChords) && dark ? Style::Melodic : Style::Techno; conf = 0.5f; }
        else if (b >= 114)       { st = dark && (dna.hasMelody || dna.hasChords) ? Style::Melodic : Style::House; conf = 0.5f; }
        else if (b >= 85)        { st = dna.fourOnFloor ? Style::House : Style::HipHop; conf = 0.45f; }
        else if (b >= 60)        { st = Style::Trap; conf = 0.4f; }
        else                     { st = Style::Ambient; conf = 0.4f; }
        if (dna.fourOnFloor && (st == Style::House || st == Style::Techno || st == Style::Melodic || st == Style::Trance)) conf += 0.2f;
        if (dna.halfTime && st == Style::Trap) conf += 0.2f;
        if (! dna.hasDrums) conf -= 0.1f;
        dna.style = st;
        dna.styleConfidence = juce::jlimit (0.0f, 1.0f, conf);
    }

    dna.valid = true;
    return dna;
}

} // namespace MusicAnalyzer
