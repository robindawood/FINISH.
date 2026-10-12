#include "AutoMaster.h"

namespace
{
    juce::String fr (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }
    juce::String dbs (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    struct Corridor { float lo, hi; };
    Corridor corridor (const ProfileStat& s, int numTracks)
    {
        const float iqr = s.p75 - s.p25;
        float lo = std::min (s.p25 - 0.5f * iqr, s.min), hi = std::max (s.p75 + 0.5f * iqr, s.max);
        if (numTracks < 5)   // provisional profile: be even more tolerant
        {
            lo = std::min (lo, s.median * 0.8f);
            hi = std::max (hi, s.median * 1.25f);
        }
        return { std::max (lo, 1.0e-5f), std::max (hi, 2.0e-5f) };
    }
}

const StyleProfileData& AutoMaster::profile (int style) noexcept
{
    return styleProfileData[juce::jlimit (0, 5, style - 1)];
}

float AutoMaster::corridorDistanceDb (float value, const ProfileStat& s) noexcept
{
    const auto c = corridor (s, 10);
    if (value <= 0.0f) return -20.0f;
    if (value < c.lo) return 10.0f * std::log10 (value / c.lo);
    if (value > c.hi) return 10.0f * std::log10 (value / c.hi);
    return 0.0f;
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
    auto& b = r.base;

    r.report.add (fr ("Style : ") + p.name + (r.styleWasDetected ? fr (" (détecté") + (bpm > 0 ? ", " + juce::String (juce::roundToInt (bpm)) + " BPM)" : juce::String (")")) : juce::String())
                  + fr (" — comparé à ") + juce::String (n) + fr (" masters de référence") + (n < 5 ? fr (" (profil provisoire)") : juce::String()));

    auto dist = [&] (float v, const ProfileStat& s) {
        const auto c = corridor (s, n);
        if (v <= 0.0f) return -20.0f;
        if (v < c.lo) return 10.0f * std::log10 (v / c.lo);
        if (v > c.hi) return 10.0f * std::log10 (v / c.hi);
        return 0.0f;
    };

    // ---- Low end (< 150 Hz) -> Bass shelf
    {
        const float d = dist (f.lowRatio, p.lowRatio);
        if (std::abs (d) > 0.3f)
        {
            b.toneLow = juce::jlimit (-3.0f, 2.5f, -0.9f * d);
            r.report.add ((d > 0 ? fr ("Graves en excès : ") : fr ("Graves en retrait : ")) + pct (f.lowRatio) + fr (" de l'énergie sous 150 Hz (références ")
                          + pct (p.lowRatio.p25) + "-" + pct (p.lowRatio.p75) + fr (") → basses ") + dbs (b.toneLow));
        }
    }
    // ---- Sub (< 60 Hz) -> Spectral SUB (static + dynamic for booming notes)
    {
        const float d = dist (f.subRatio, p.subRatio);
        if (d > 0.3f)
        {
            b.specGain[0] = juce::jlimit (-2.5f, 0.0f, -0.8f * d);
            b.specDyn[0] = 0.5f;
            r.report.add (fr ("Sub trop présent (") + pct (f.subRatio) + fr (" du mid sous 60 Hz) → SPECTRAL sub ") + dbs (b.specGain[0]) + fr (", dynamique sur les notes qui débordent"));
        }
        else if (d < -0.5f && ! lofi)
        {
            b.specGain[0] = juce::jlimit (0.0f, 1.5f, -0.5f * d);
            r.report.add (fr ("Sub léger pour le style → SPECTRAL sub ") + dbs (b.specGain[0]));
        }
        else b.specDyn[0] = lofi ? 0.0f : 0.25f;
    }
    // ---- Low-mid 200-500 Hz (mud)
    {
        const float d = dist (f.lowMidRatio, p.lowMidRatio);
        if (d > 0.3f)
        {
            b.specGain[1] = juce::jlimit (-3.0f, 0.0f, -0.9f * d);
            b.specDyn[1] = 0.4f;
            r.report.add (fr ("Bas-médiums chargés (son boueux) : ") + pct (f.lowMidRatio) + fr (" entre 200 et 500 Hz → SPECTRAL ") + dbs (b.specGain[1]) + fr (" à 300 Hz"));
        }
        else if (d < -0.5f)
        {
            b.specGain[1] = juce::jlimit (0.0f, 1.5f, -0.5f * d);
            r.report.add (fr ("Manque de corps (bas-médiums) → SPECTRAL ") + dbs (b.specGain[1]) + fr (" à 300 Hz"));
        }
    }
    // ---- Presence 2-5 kHz
    {
        const float d = dist (f.presenceRatio, p.presenceRatio);
        if (d > 0.3f)
        {
            b.specGain[2] = juce::jlimit (-3.0f, 0.0f, -0.8f * d);
            b.specDyn[2] = 0.6f;
            r.report.add (fr ("Haut-médiums agressifs : ") + pct (f.presenceRatio) + fr (" entre 2 et 5 kHz → SPECTRAL ") + dbs (b.specGain[2]) + fr (" à 3,5 kHz, surtout sur les pics"));
        }
        else if (d < -0.4f)
        {
            b.specGain[2] = juce::jlimit (0.0f, 2.0f, -0.6f * d);
            r.report.add (fr ("Manque de présence (2-5 kHz, l'attaque du kick et des voix) → SPECTRAL ") + dbs (b.specGain[2]));
        }
        else b.specDyn[2] = 0.3f;   // light de-harsh on peaks only
    }
    // ---- Highs (> 4 kHz) -> Air shelf, dynamic 8 kHz if too much
    {
        const float d = dist (f.highRatio, p.highRatio);
        if (d > 0.3f)
        {
            b.toneAir = juce::jlimit (-3.0f, 0.0f, -0.8f * d);
            b.specDyn[3] = 0.4f;
            r.report.add (fr ("Aigus trop brillants : ") + pct (f.highRatio) + fr (" au-dessus de 4 kHz → air ") + dbs (b.toneAir) + fr (", charlestons adoucis sur les pics"));
        }
        else if (d < -0.4f)
        {
            b.toneAir = juce::jlimit (0.0f, lofi ? 1.0f : 2.5f, -0.7f * d);
            r.report.add (fr ("Aigus ternes → air ") + dbs (b.toneAir) + fr (" à 10 kHz"));
        }
    }
    // ---- Width and mono bass
    {
        const bool phase = f.monoLossDb > 4.5f || f.correlation < 0.2f;
        const float d = dist (f.sideRatio, p.sideRatio);
        if (phase)
        {
            b.widthSide = -1.5f;
            r.mixNotes.add (fr ("Problème de phase : le mix perd ") + juce::String (f.monoLossDb, 1) + fr (" dB en mono. À vérifier dans le mix (sons inversés, chorus/élargisseurs trop forts)."));
            r.report.add (fr ("Phase fragile → côtés -1,5 dB pour protéger le mono"));
        }
        else if (d > 0.5f)
        {
            b.widthSide = juce::jlimit (-3.0f, 0.0f, -0.7f * d);
            r.report.add (fr ("Image plus large que les références → côtés ") + dbs (b.widthSide));
        }
        else if (d < -0.8f)
        {
            b.widthSide = juce::jlimit (0.0f, 1.5f, -0.4f * d);
            r.report.add (fr ("Image étroite pour le style → côtés ") + dbs (b.widthSide));
        }
        if (! lofi)
        {
            b.widthMono = r.style == styleDnb || r.style == styleHardTechno ? 160.0f : 140.0f;
            if (f.lowSideRatio > 0.01f)
                r.report.add (fr ("Basses en stéréo (") + pct (f.lowSideRatio) + fr (") alors que toutes les références sont mono → graves au centre sous ") + juce::String (juce::roundToInt (b.widthMono)) + " Hz");
            else
                r.report.add (fr ("Graves gardés au centre sous ") + juce::String (juce::roundToInt (b.widthMono)) + fr (" Hz (comme 100 % des références)"));
        }
    }
    // ---- Dynamics: glue / punch / clipper / saturation
    {
        const auto plrC = corridor (p.plr, n);
        const bool squashed = f.plr < 8.0f;
        const bool veryDynamic = f.plr > plrC.hi + 2.0f;
        b.compDetail = lofi ? 0.7f : 0.5f;
        if (squashed)
        {
            b.compGlue = 0.0f; b.compPunch = 0.0f; b.clipAmt = 0.15f;
            r.mixNotes.add (fr ("Mix déjà très compressé (PLR ") + juce::String (f.plr, 1) + fr (" dB) : plus de limiteur ne le rendra pas plus percutant. Allège la compression du bus dans le mix."));
            r.report.add (fr ("Mix déjà compressé → pas de compression en plus, clipper léger"));
        }
        else
        {
            b.compGlue = lofi ? 0.15f : (veryDynamic ? 0.55f : 0.35f);
            b.compPunch = lofi ? 0.0f : (veryDynamic ? 0.1f : 0.25f);
            b.clipAmt = lofi ? 0.0f : (r.style == styleDnb || r.style == styleHardTechno ? 0.6f : 0.4f);
            r.report.add (fr ("Dynamique : PLR ") + juce::String (f.plr, 1) + fr (" dB (références ") + juce::String (p.plr.p25, 1) + "-" + juce::String (p.plr.p75, 1)
                          + fr (") → compression ") + pct (b.compGlue) + ", punch " + pct (b.compPunch) + ", clipper " + pct (b.clipAmt));
        }
        b.satDrive = lofi ? 0.3f : (r.style == styleHardTechno || r.style == styleDnb ? 0.25f : 0.15f);
    }
    // ---- Loudness target (loud passages, like the closed loop measures them)
    r.integratedTarget = p.lufs_i.median;
    if (targetIndex == styleTarget)
    {
        r.loudPassageTarget = p.st_drop.median;
        r.report.add (fr ("Volume : passages forts à ") + juce::String (r.loudPassageTarget, 1) + fr (" LUFS (médiane des références, ") + juce::String (p.lufs_i.median, 1)
                      + fr (" LUFS intégré), plafond ") + juce::String (ceilingDb, 1) + " dBTP");
    }
    else if (targetIndex > 0 && targetIndex < sameLevelTarget)
    {
        r.loudPassageTarget = loudnessTargets[targetIndex].lufs + 1.0f;   // integrated target -> loud passages ~1 dB above
        r.integratedTarget = loudnessTargets[targetIndex].lufs;
        r.report.add (fr ("Volume : cible ") + loudnessTargets[targetIndex].name + fr (" (choisie à la main)"));
    }
    if (r.loudPassageTarget > -6.5f && ceilingDb > -0.5f)
        r.report.add (fr ("Style très fort : le clipper fait une partie du travail pour garder le punch du kick"));
    return r;
}
