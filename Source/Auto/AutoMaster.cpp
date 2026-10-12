#include "AutoMaster.h"
#include "../DSP/Biquad.h"
#include <complex>

namespace
{
    juce::String fr (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }
    juce::String dbs (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

}

const StyleProfileData& AutoMaster::profile (int style) noexcept
{
    return styleProfileData[juce::jlimit (0, 5, style - 1)];
}

double AutoMaster::estimateTempo (const std::vector<float>& low, const std::vector<float>& high, double rate)
{
    auto acf = [rate] (const std::vector<float>& env, double& strength)
    {
        strength = 0;
        const size_t n = env.size();
        if (n < (size_t) (rate * 6)) return 0.0;
        std::vector<float> on (n, 0.0f);
        double mean = 0;
        for (size_t i = 1; i < n; ++i) { on[i] = std::max (0.0f, env[i] - env[i - 1]); mean += on[i]; }
        mean /= (double) n;
        for (auto& v : on) v -= (float) mean;
        double ac0 = 0;
        for (auto v : on) ac0 += (double) v * v;
        if (ac0 <= 0) return 0.0;
        const int lo = (int) (rate * 60.0 / 200.0), hi = (int) (rate * 60.0 / 60.0);
        std::vector<double> ac ((size_t) hi + 2, 0.0);
        for (int lag = lo - 1; lag <= hi + 1; ++lag)
        {
            double a = 0;
            for (size_t i = (size_t) lag; i < n; ++i) a += (double) on[i] * on[i - (size_t) lag];
            ac[(size_t) lag] = a;
        }
        int best = lo;
        for (int lag = lo; lag <= hi; ++lag) if (ac[(size_t) lag] > ac[(size_t) best]) best = lag;
        const double a = ac[(size_t) best - 1], b = ac[(size_t) best], c = ac[(size_t) best + 1], den = a - 2 * b + c;
        const double lag = best + (den != 0 ? 0.5 * (a - c) / den : 0.0);
        strength = b / ac0;
        return 60.0 * rate / lag;
    };
    auto norm = [] (double t) { if (t <= 0) return 0.0; while (t < 100) t *= 2; while (t > 200) t /= 2; return t; };
    double sh = 0, sk = 0;
    const double th = norm (acf (high, sh)), tk = norm (acf (low, sk));
    if (sh < 0.05 && sk < 0.05) return 0.0;
    if (th > 0 && std::abs (th - tk) / th < 0.03) return th;
    return sh >= sk ? th : tk;
}

int AutoMaster::detectStyle (const AudioFeatures& f, double bpm)
{
    // Slow, soft groove (or a dark mix with no host tempo): lo-fi.
    if (bpm > 0 && bpm < 105) return styleLofi;
    if (bpm <= 0 && f.highRatio < 0.012f && f.plr > 13.0f) return styleLofi;

    // Tempo decides the family, the spectral balance picks inside it.
    // A lo-fi groove read in double time lands at 160-190: its dark top end gives it away.
    if (bpm >= 150 && bpm <= 190 && f.highRatio < 0.012f && f.plr > 11.0f) return styleLofi;
    if (bpm >= 165 && bpm <= 190) return styleDnb;
    if (bpm >= 145 && bpm < 165) return styleHardTechno;
    if (bpm >= 136 && bpm < 145) return (f.subRatio < 0.45f || f.lowMidRatio > 0.4f) ? styleUkg : styleTechHouse;

    // House family (or unknown tempo): nearest of Tech House / Afro House on the balance.
    auto dist = [&] (int st)
    {
        const auto& p = profile (st);
        auto d = [] (float v, const ProfileStat& s) { const float x = 10.0f * std::log10 (std::max (v, 1.0e-5f) / std::max (s.median, 1.0e-5f)); return x * x; };
        return d (f.subRatio, p.subRatio) + d (f.lowMidRatio, p.lowMidRatio) + d (f.presenceRatio, p.presenceRatio) + d (f.highRatio, p.highRatio);
    };
    return dist (styleAfro) < dist (styleTechHouse) ? styleAfro : styleTechHouse;
}


// ============================================================================================ tone

namespace
{
    constexpr int nb = 29;
    constexpr int anchorLo = 9, anchorHi = 23;    // 200 Hz .. 5 kHz: reference level for both curves
    // "trop de ..." / "manque de ..." (with elision for the vowels)
    const char* regionNames[] = { "de sub (30-60 Hz)", "de basses (80-160 Hz)", "de bas-m\xc3\xa9""dium (200-500 Hz)", "de m\xc3\xa9""dium (630 Hz-1,6 kHz)",
                                  "de pr\xc3\xa9sence (2-5 kHz)", "d'aigus (6-10 kHz)", "d'air (12-16 kHz)" };
    constexpr int regionLo[] = { 1, 5, 9, 14, 19, 24, 27 }, regionHi[] = { 4, 8, 13, 18, 23, 26, 28 };

    // clear excess under 40 Hz, in bands where there is real energy
    bool hasSubExcess (const AutoMaster::ToneAnalysis& t)
    {
        for (int b = 0; b <= 2; ++b)
            if (t.wanted[b] < -1.0f && t.weight[b] > 0.1f) return true;
        return false;
    }

    struct Limits { float lo, hi; };
    Limits toolLimits (int tool, bool lofi)
    {
        switch (tool)
        {
            case 0: return { -4.5f, 4.0f };                    // bass shelf
            case 1: return { -5.0f, 3.0f };                    // 60 Hz bell
            case 2: return { -5.0f, 2.5f };                    // 300 Hz bell
            case 3: return { -5.0f, 3.0f };                    // 3.5 kHz bell
            case 4: return { -5.0f, lofi ? 1.0f : 3.0f };      // 8 kHz bell
            default: return { -4.0f, lofi ? 1.5f : 3.5f };     // air shelf
        }
    }
    float getTool (const ModuleBase& b, int k)
    {
        switch (k) { case 0: return b.toneLow; case 1: return b.specGain[0]; case 2: return b.specGain[1]; case 3: return b.specGain[2]; case 4: return b.specGain[3]; default: return b.toneAir; }
    }
    void setTool (ModuleBase& b, int k, float v)
    {
        switch (k) { case 0: b.toneLow = v; break; case 1: b.specGain[0] = v; break; case 2: b.specGain[1] = v; break; case 3: b.specGain[2] = v; break; case 4: b.specGain[3] = v; break; default: b.toneAir = v; }
    }
    const char* toolName (int k)
    {
        switch (k) { case 0: return "basses 110 Hz"; case 1: return "SPECTRAL 60 Hz"; case 2: return "SPECTRAL 300 Hz"; case 3: return "SPECTRAL 3,5 kHz"; case 4: return "SPECTRAL 8 kHz"; default: return "air 10 kHz"; }
    }
}

float AutoMaster::toolResponseDb (int tool, float hz, float gainDb)
{
    constexpr double sr = 48000.0;
    Biquad bq;
    switch (tool)
    {
        case 0: bq.setLowShelf (sr, 110.0, gainDb); break;
        case 1: bq.setPeak (sr, specBands[0].hz, specBands[0].q, gainDb); break;
        case 2: bq.setPeak (sr, specBands[1].hz, specBands[1].q, gainDb); break;
        case 3: bq.setPeak (sr, specBands[2].hz, specBands[2].q, gainDb); break;
        case 4: bq.setPeak (sr, specBands[3].hz, specBands[3].q, gainDb); break;
        default: bq.setHighShelf (sr, 10000.0, gainDb); break;
    }
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const auto num = (double) bq.b0 + (double) bq.b1 * z1 + (double) bq.b2 * z2;
    const auto den = 1.0 + (double) bq.a1 * z1 + (double) bq.a2 * z2;
    return (float) (20.0 * std::log10 (std::abs (num / den)));
}

AutoMaster::ToneAnalysis AutoMaster::analyseTone (const AudioFeatures& f, const StyleProfileData& p, float strength)
{
    ToneAnalysis t;
    // light smoothing of the mix curve (neighbouring bands), anchor both curves on the 200 Hz - 5 kHz average
    float mix[nb];
    for (int b = 0; b < nb; ++b)
    {
        const int l = std::max (0, b - 1), r = std::min (nb - 1, b + 1);
        mix[b] = 0.25f * f.thirdOctDb[l] + 0.5f * f.thirdOctDb[b] + 0.25f * f.thirdOctDb[r];
    }
    float am = 0, ap = 0;
    for (int b = anchorLo; b <= anchorHi; ++b) { am += mix[b]; ap += p.thirdOctMedian[b]; }
    am /= (float) (anchorHi - anchorLo + 1); ap /= (float) (anchorHi - anchorLo + 1);
    const bool provisional = p.numTracks < 5;
    double se = 0, sw = 0;
    for (int b = 0; b < nb; ++b)
    {
        t.deviation[b] = (mix[b] - am) - (p.thirdOctMedian[b] - ap);
        float tol = 0.5f * (p.thirdOctP75[b] - p.thirdOctP25[b]);
        tol = juce::jlimit (b < 5 ? 1.5f : 1.0f, 3.0f, tol);          // lowest bands: measurement is coarser
        if (provisional) tol = std::min (4.0f, tol * 1.4f + 0.5f);
        t.tolerance[b] = tol;
        float w = 1.0f;
        if (b == 0) w = 0.3f; else if (b == 1) w = 0.5f; else if (b <= 3) w = 0.75f; else if (b == 27) w = 0.6f; else if (b == 28) w = 0.25f;
        if (f.thirdOctDb[b] < -60.0f) w *= 0.1f;                       // nothing there (e.g. no air in a lo-fi track)
        t.weight[b] = w;
        const float ad = std::abs (t.deviation[b]);
        if (ad > tol)
        {
            const float ramp = juce::jlimit (0.0f, 1.0f, (ad - tol) / (0.5f * tol));
            t.wanted[b] = -(t.deviation[b] > 0 ? 1.0f : -1.0f) * (ad - 0.4f * tol) * strength * ramp;
            se += (double) w * (ad - tol) * (ad - tol);
        }
        sw += w;
    }
    t.error = (float) std::sqrt (se / std::max (1.0e-9, sw));
    return t;
}

std::array<float, 6> AutoMaster::fitTone (const ToneAnalysis& t)
{
    return fitToneBounded (t, false, {});
}

// Bounded least squares (active set): a tool that hits its limit is frozen there and the others are re-solved, so the
// remaining tools never compensate in the wrong direction. offset = gains already in place (second pass).
std::array<float, 6> AutoMaster::fitToneBounded (const ToneAnalysis& t, bool lofi, std::array<float, 6> offset)
{
    constexpr int K = 6;
    double A[nb][K];
    for (int b = 0; b < nb; ++b)
        for (int k = 0; k < K; ++k)
            A[b][k] = toolResponseDb (k, profileThirdOctHz[b], 3.0f) / 3.0;
    // the direction each tool is needed in its own zone: a tool may not move against it
    double need[K] {};
    for (int k = 0; k < K; ++k)
        for (int b = 0; b < nb; ++b) need[k] += t.weight[b] * std::abs (A[b][k]) * t.wanted[b];
    // Excess under 40 Hz (rumble, sub too deep): the bass tools must not lift it further, whatever 50-100 Hz wants.
    const bool subExcess = hasSubExcess (t);
    auto limitsOf = [&] (int k)
    {
        auto lim = toolLimits (k, lofi);
        if (subExcess && k <= 1) lim.hi = std::min (lim.hi, std::max (0.0f, offset[(size_t) k]));
        return lim;
    };
    std::array<float, 6> g {};
    bool fixed[K] {};
    for (int iter = 0; iter < 2 * K + 1; ++iter)
    {
        // residual target once the frozen tools are accounted for
        double target[nb];
        for (int b = 0; b < nb; ++b)
        {
            target[b] = t.wanted[b];
            for (int k = 0; k < K; ++k) if (fixed[k]) target[b] -= A[b][k] * g[(size_t) k];
        }
        int idx[K], m = 0;
        for (int k = 0; k < K; ++k) if (! fixed[k]) idx[m++] = k;
        if (m == 0) break;
        double M[K][K + 1] {};
        for (int i = 0; i < m; ++i)
        {
            for (int j = 0; j < m; ++j)
            {
                double s = 0;
                for (int b = 0; b < nb; ++b) s += t.weight[b] * A[b][idx[i]] * A[b][idx[j]];
                M[i][j] = s + (i == j ? 0.08 : 0.0);   // ridge: no wild opposing moves
            }
            double s = 0;
            for (int b = 0; b < nb; ++b) s += t.weight[b] * A[b][idx[i]] * target[b];
            M[i][m] = s;
        }
        for (int c = 0; c < m; ++c)   // Gauss-Jordan
        {
            int piv = c;
            for (int r = c + 1; r < m; ++r) if (std::abs (M[r][c]) > std::abs (M[piv][c])) piv = r;
            for (int j = 0; j <= m; ++j) std::swap (M[c][j], M[piv][j]);
            const double d = M[c][c];
            if (std::abs (d) < 1.0e-12) continue;
            for (int j = 0; j <= m; ++j) M[c][j] /= d;
            for (int r = 0; r < m; ++r)
                if (r != c)
                {
                    const double f = M[r][c];
                    for (int j = 0; j <= m; ++j) M[r][j] -= f * M[c][j];
                }
        }
        // freeze only the worst offender, then re-solve the others (freezing all at once would lock in values that
        // were only extreme because of another tool's excess)
        int worst = -1;
        float worstExcess = 0.0f;
        for (int i = 0; i < m; ++i)
        {
            const int k = idx[i];
            g[(size_t) k] = (float) M[i][m];
            const auto lim = limitsOf (k);
            const float total = g[(size_t) k] + offset[(size_t) k];
            const float excess = std::max (lim.lo - total, total - lim.hi);
            if (excess > worstExcess) { worstExcess = excess; worst = k; }
            const bool against = (double) g[(size_t) k] * need[k] < 0.0 && std::abs (g[(size_t) k]) > 0.25f;
            if (against && std::abs (g[(size_t) k]) > worstExcess) { worstExcess = std::abs (g[(size_t) k]); worst = k + 100; }
        }
        const bool violated = worst >= 0;
        if (worst >= 100)       // moving against its own zone's need: freeze at no change
        {
            g[(size_t) (worst - 100)] = 0.0f;
            fixed[worst - 100] = true;
        }
        else if (violated)
        {
            const auto lim = limitsOf (worst);
            g[(size_t) worst] = juce::jlimit (lim.lo, lim.hi, g[(size_t) worst] + offset[(size_t) worst]) - offset[(size_t) worst];
            fixed[worst] = true;
        }
        if (! violated) break;
    }
    return g;
}

// ============================================================================================ first pass

AutoResult AutoMaster::compute (const AudioFeatures& f, int styleParam, double bpm, int targetIndex, float ceilingDb)
{
    AutoResult r;
    if (! f.valid) return r;
    r.valid = true;
    r.styleWasDetected = styleParam == styleAuto;
    r.style = r.styleWasDetected ? detectStyle (f, bpm) : juce::jlimit (1, numStyles - 1, styleParam);
    const auto& p = profile (r.style);
    const int n = p.numTracks;
    const bool lofi = r.style == styleLofi;
    const bool hard = r.style == styleDnb || r.style == styleHardTechno;
    auto& b = r.base;

    r.report.add (fr ("Style : ") + p.name + (r.styleWasDetected ? fr (" (détecté") + (bpm > 0 ? ", " + juce::String (juce::roundToInt (bpm)) + " BPM)" : juce::String (")")) : juce::String())
                  + fr (" — comparé à ") + juce::String (n) + fr (" masters de référence") + (n < 5 ? fr (" (profil provisoire : corrections plus prudentes)") : juce::String()));
    if (f.hasSpectrum)
        r.report.add (fr ("Écoute : ") + juce::String (juce::roundToInt (f.durationSec)) + fr (" s, analyse sur les ") + juce::String (juce::roundToInt (f.loudSeconds))
                      + fr (" s les plus forts (même méthode que les références)"));

    // ---- Tonal balance: curve vs references, fitted on the EQ tools
    if (f.hasSpectrum)
    {
        const auto t = analyseTone (f, p, n < 5 ? 0.7f : 0.9f);
        r.toneErrorBefore = t.error;
        for (int g = 0; g < 7; ++g)
        {
            float dev = 0, excess = 0, w = 0;
            for (int i = regionLo[g]; i <= regionHi[g]; ++i)
            {
                dev += t.deviation[i] * t.weight[i]; w += t.weight[i];
                excess = std::max (excess, std::abs (t.deviation[i]) - t.tolerance[i]);
            }
            dev /= std::max (1.0e-6f, w);
            if (excess > 0.3f && std::abs (dev) > 0.75f)
                r.report.add ((dev > 0 ? fr ("Trop ") : fr ("Manque ")) + fr (regionNames[g]) + " : " + (dev > 0 ? "+" : "") + juce::String (dev, 1)
                              + fr (" dB par rapport aux références"));
        }
        auto g = fitToneBounded (t, lofi, {});
        juce::String moves;
        for (int k = 0; k < 6; ++k)
        {
            const auto lim = toolLimits (k, lofi);
            const float v = juce::jlimit (lim.lo, lim.hi, g[(size_t) k]);
            setTool (b, k, std::abs (v) < 0.2f ? 0.0f : v);
            if (std::abs (v) >= 0.2f) moves << (moves.isEmpty() ? "" : ", ") << toolName (k) << " " << dbs (v);
        }
        if (moves.isNotEmpty()) r.report.add (fr ("Égalisation : ") + moves);
        else r.report.add (fr ("Équilibre tonal déjà dans les références : pas d'égalisation"));
        if (hasSubExcess (t))
            r.mixNotes.add (fr ("Énergie excessive sous 40 Hz (") + dbs (std::max ({ t.deviation[0], t.deviation[1], t.deviation[2] })) + fr (" vs références) : un filtre coupe-bas vers 25-30 Hz sur le bus ou la basse/kick dans le mix rendra le master plus propre et plus fort."));
        // what mastering EQ cannot reasonably fix: tell where the mix should be reworked
        for (int gi = 0; gi < 7; ++gi)
        {
            float dev = 0, res = 0, w = 0;
            for (int i = regionLo[gi]; i <= regionHi[gi]; ++i)
            {
                float eq = 0;
                for (int k = 0; k < 6; ++k) eq += toolResponseDb (k, profileThirdOctHz[i], getTool (b, k));
                dev += t.deviation[i] * t.weight[i]; res += (t.deviation[i] + eq) * t.weight[i]; w += t.weight[i];
            }
            dev /= std::max (1.0e-6f, w); res /= std::max (1.0e-6f, w);
            if (std::abs (res) > 3.0f && gi > 0)
                r.mixNotes.add ((dev > 0 ? fr ("Trop ") : fr ("Manque ")) + fr (regionNames[gi]) + fr (" dans le mix (") + dbs (dev) + fr (") : le master en corrige ")
                                + juce::String (std::abs (res - dev), 1) + fr (" dB sans abîmer le son, le reste est à reprendre dans le mix."));
        }
        // dynamic bells: the corrected zones also get a level-dependent action (only the peaks are pushed further)
        b.specDyn[0] = lofi ? 0.0f : (b.specGain[0] < -1.0f ? 0.5f : 0.25f);
        b.specDyn[1] = b.specGain[1] < -1.0f ? 0.4f : 0.15f;
        b.specDyn[2] = b.specGain[2] < -1.0f ? 0.6f : 0.3f;
        b.specDyn[3] = b.specGain[3] < -1.0f ? 0.5f : 0.2f;
    }

    // ---- Width and mono bass (side energy, measured like the references)
    {
        const bool phase = f.monoLossDb > 4.5f || f.correlation < 0.2f;
        const float med = std::max (1.0e-4f, p.sideRatio.median);
        const float d = 10.0f * std::log10 (std::max (f.sideRatio, 1.0e-5f) / med);
        const float tol = juce::jlimit (1.5f, 4.0f, 2.5f * std::log10 (std::max (p.sideRatio.p75, 1.0e-4f) / std::max (p.sideRatio.p25, 1.0e-5f)) * (n < 5 ? 1.5f : 1.0f));
        if (phase)
        {
            b.widthSide = -2.0f;
            r.mixNotes.add (fr ("Problème de phase : le mix perd ") + juce::String (f.monoLossDb, 1) + fr (" dB en mono. À vérifier dans le mix (sons inversés, chorus/élargisseurs trop forts)."));
            r.report.add (fr ("Phase fragile → côtés -2 dB pour protéger le mono"));
        }
        else if (std::abs (d) > tol)
        {
            b.widthSide = juce::jlimit (-4.0f, 2.5f, -0.7f * (d - (d > 0 ? 0.5f * tol : -0.5f * tol)));
            r.report.add ((d > 0 ? fr ("Image plus large") : fr ("Image plus étroite")) + fr (" que les références (côtés ") + (d > 0 ? "+" : "") + juce::String (d, 1)
                          + fr (" dB) → côtés ") + dbs (b.widthSide));
        }
        if (! lofi)
        {
            b.widthMono = hard ? 160.0f : 140.0f;
            if (f.lowSideRatio > 0.01f)
                r.report.add (fr ("Basses en stéréo (") + pct (f.lowSideRatio) + fr (") alors que les références sont mono → graves au centre sous ") + juce::String (juce::roundToInt (b.widthMono)) + " Hz");
            else
                r.report.add (fr ("Graves gardés au centre sous ") + juce::String (juce::roundToInt (b.widthMono)) + fr (" Hz (comme les références)"));
        }
    }

    // ---- Loudness target (loud passages, like the closed loop measures them)
    r.integratedTarget = p.lufs_i.median;
    if (targetIndex == styleTarget)
        r.loudPassageTarget = p.st_drop.median;
    else if (targetIndex > 0 && targetIndex < sameLevelTarget)
    {
        r.loudPassageTarget = loudnessTargets[targetIndex].lufs + 1.0f;   // integrated target -> loud passages ~1 dB above
        r.integratedTarget = loudnessTargets[targetIndex].lufs;
    }

    // ---- Dynamics: the peaks of the loud passages must come down by (crest now) - (ceiling - target loudness).
    // That reduction is shared: compression ~1-2 dB, clipper up to ~4-5 dB, limiter the rest (kept around 2-3 dB).
    {
        const float loud = f.loudLufs > -60.0f ? f.loudLufs : f.integratedLufs;
        const float crestIn = (f.hasSpectrum ? f.truePeakDb : f.peakDb) - loud;
        const float crestOut = ceilingDb - r.loudPassageTarget;
        const float reduce = crestIn - crestOut;
        b.compDetail = lofi ? 0.7f : 0.5f;
        if (crestIn < p.plr_drop.p25 - 2.0f && crestIn < 7.0f)
        {
            b.compGlue = 0.0f; b.compPunch = 0.0f; b.clipAmt = 0.1f;
            r.mixNotes.add (fr ("Mix déjà très compressé (crête/volume ") + juce::String (crestIn, 1) + fr (" dB sur les passages forts) : plus de limiteur ne le rendra pas plus percutant. Allège la compression du bus dans le mix."));
            r.report.add (fr ("Mix déjà compressé → pas de compression en plus, clipper léger"));
        }
        else
        {
            if (lofi)
            {
                b.clipAmt = juce::jlimit (0.0f, 0.25f, 0.03f * reduce);
                b.compGlue = 0.15f;
                b.compPunch = 0.0f;
            }
            else
            {
                b.clipAmt = juce::jlimit (0.05f, hard ? 0.85f : 0.75f, 0.05f + 0.065f * reduce);
                b.compGlue = reduce > 6.0f ? juce::jlimit (0.2f, 0.5f, 0.15f + 0.03f * reduce) : 0.2f;
                b.compPunch = 0.2f;
            }
            r.report.add (fr ("Dynamique : crêtes à ") + juce::String (crestIn, 1) + fr (" dB au-dessus du volume des passages forts ; pour ") + juce::String (r.loudPassageTarget, 1)
                          + fr (" LUFS sous ") + juce::String (ceilingDb, 1) + fr (" dBTP il faut ") + juce::String (crestOut, 1) + fr (" dB → ") + juce::String (std::max (0.0f, reduce), 1)
                          + fr (" dB à absorber : compression ") + pct (b.compGlue) + ", clipper " + pct (b.clipAmt) + fr (", le limiteur fait le reste"));
        }
        b.satDrive = lofi ? 0.3f : (hard ? 0.25f : 0.15f);
    }

    if (targetIndex == styleTarget)
        r.report.add (fr ("Volume : passages forts à ") + juce::String (r.loudPassageTarget, 1) + fr (" LUFS (médiane des références, ") + juce::String (p.lufs_i.median, 1)
                      + fr (" LUFS intégré), plafond ") + juce::String (ceilingDb, 1) + " dBTP");
    else if (targetIndex > 0 && targetIndex < sameLevelTarget)
        r.report.add (fr ("Volume : cible ") + loudnessTargets[targetIndex].name + fr (" (choisie à la main)"));
    return r;
}
// ============================================================================================ second pass (processed output)

void AutoMaster::refine (const AudioFeatures& out, float avgGr, AutoResult& r)
{
    if (! r.valid || ! out.valid || ! out.hasSpectrum) return;
    const auto& p = profile (r.style);
    const bool lofi = r.style == styleLofi;
    auto& b = r.base;

    // Tone: what is still off once compression / clipper / limiter have done their part
    const auto t = analyseTone (out, p, 0.75f);
    std::array<float, 6> now {};
    for (int k = 0; k < 6; ++k) now[(size_t) k] = getTool (b, k);
    const auto g = fitToneBounded (t, lofi, now);
    juce::String moves;
    for (int k = 0; k < 6; ++k)
    {
        if (std::abs (g[(size_t) k]) < 0.3f) continue;
        const auto lim = toolLimits (k, lofi);
        const float before = getTool (b, k), after = juce::jlimit (lim.lo, lim.hi, before + g[(size_t) k]);
        if (std::abs (after - before) < 0.2f) continue;
        setTool (b, k, after);
        moves << (moves.isEmpty() ? "" : ", ") << toolName (k) << " " << dbs (before) << fr (" → ") << dbs (after);
    }
    r.report.add (fr ("Vérification sur le son traité : écart tonal ") + juce::String (t.error, 1) + " dB"
                  + (moves.isNotEmpty() ? fr (", ajusté : ") + moves
                                        : (t.error > 0.8f ? fr (", correction déjà au maximum autorisé (le reste est à traiter dans le mix)") : fr (", rien à reprendre"))));

    // Dynamics: the limiter should not be doing all the work (it would pump and flatten the kick)
    const float crestOut = out.truePeakDb - out.loudLufs;
    if (avgGr > 4.0f && ! lofi)
    {
        b.clipAmt = std::min (0.85f, b.clipAmt + 0.15f);
        b.compGlue = std::min (0.55f, b.compGlue + 0.05f);
        r.report.add (fr ("Le limiteur travaillait trop (") + juce::String (avgGr, 1) + fr (" dB) → clipper ") + pct (b.clipAmt) + ", compression " + pct (b.compGlue));
        if (avgGr > 7.0f)
        {
            r.loudPassageTarget -= 1.0f;
            r.report.add (fr ("Cible baissée de 1 dB pour garder le punch (") + juce::String (r.loudPassageTarget, 1) + " LUFS)");
        }
    }
    else
        r.report.add (fr ("Dynamique après traitement : limiteur ") + juce::String (avgGr, 1) + fr (" dB en moyenne, crêtes à ") + juce::String (crestOut, 1)
                      + fr (" dB au-dessus du volume (références : ") + juce::String (p.plr_drop.median, 1) + fr (" dB, mais elles dépassent souvent 0 dBTP)"));
}

void AutoMaster::finalCheck (const AudioFeatures& out, AutoResult& r)
{
    if (! r.valid || ! out.valid || ! out.hasSpectrum) return;
    const auto& p = profile (r.style);
    const auto t = analyseTone (out, p, 1.0f);
    r.toneErrorAfter = t.error;
    r.report.add (fr ("Résultat : écart tonal aux références ") + juce::String (r.toneErrorBefore, 1) + fr (" dB avant → ") + juce::String (t.error, 1)
                  + fr (" dB après ; passages forts à ") + juce::String (out.loudLufs, 1) + " LUFS, true peak " + juce::String (out.truePeakDb, 1) + " dBTP");
}
