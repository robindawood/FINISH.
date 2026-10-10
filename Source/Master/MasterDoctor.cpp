#include "MasterDoctor.h"
#include "../Compose/ComposeTypes.h"

namespace MasterDoctor
{
namespace
{
    struct Range { float lo, hi; };
    struct Landmarks { Range low, sub, lowMid, pres, high, side; float lowSideMax; };

    // Broad landmarks (share of energy) for finished masters. They are deliberately wide: FINISH only acts when a
    // mix is clearly outside. A reference track replaces them.
    // Calibrated on spectra falling 4 to 5 dB per octave (typical finished mixes): low = share below 150 Hz,
    // sub = below 60 Hz, lowMid / pres = 200-500 Hz / 2-5 kHz within the energy above 150 Hz, high = above 4 kHz.
    const Landmarks club    { { 0.45f, 0.76f }, { 0.08f, 0.50f }, { 0.22f, 0.42f }, { 0.05f, 0.17f }, { 0.015f, 0.10f }, { 0.04f, 0.30f }, 0.05f };
    const Landmarks general { { 0.32f, 0.68f }, { 0.04f, 0.42f }, { 0.22f, 0.45f }, { 0.06f, 0.19f }, { 0.02f, 0.12f }, { 0.04f, 0.32f }, 0.06f };

    float distDb (float x, Range r)   // > 0 above the range, < 0 below, 0 inside
    {
        x = std::max (x, 1.0e-6f);
        if (x > r.hi) return 10.0f * std::log10 (x / r.hi);
        if (x < r.lo) return 10.0f * std::log10 (x / r.lo);
        return 0.0f;
    }
    juce::String num (float v) { return v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v)); }
    juce::String pct (float x) { return num (x * 100.0f) + " %"; }
    juce::String rng (Range r) { return num (r.lo * 100.0f) + "-" + num (r.hi * 100.0f) + " %"; }
    juce::String db (float v, int dec = 1) { return (v > 0 ? "+" : "") + juce::String (v, dec) + " dB"; }
}

juce::String profileLabel (Profile p)
{
    static const char* n[] = { "AUTO", "PUNCH", "CHAUD", "OUVERT" };
    return n[(int) p];
}

juce::String profileHelp (Profile p)
{
    switch (p)
    {
        case Profile::Auto:  return utf8 ("Corrige seulement ce qui sort des repères, sans couleur ajoutée.");
        case Profile::Punch: return utf8 ("Priorité aux attaques : le leveller laisse passer les transitoires, très peu d'écrêtage.");
        case Profile::Warm:  return utf8 ("Garde le caractère sombre : aucun aigu ajouté, les excès de haut sont adoucis.");
        case Profile::Open:  return utf8 ("Plus d'air et d'ouverture, stéréo légèrement élargie si le mono le permet.");
    }
    return {};
}

MasterPlan design (const Request& r)
{
    MasterPlan p;
    const auto& f = r.in;
    if (! f.valid) return p;
    p.valid = true;
    const bool isClub = (r.bpm >= 110.0 && r.bpm <= 150.0) || r.bpm >= 160.0;
    p.genre = isClub ? "club" : utf8 ("général");
    Landmarks lm = isClub ? club : general;
    const auto* ref = r.reference != nullptr && r.reference->valid ? r.reference : nullptr;
    if (ref != nullptr)
    {
        auto around = [] (float v) { return Range { v * 0.82f, v * 1.22f }; };
        lm.low = around (ref->lowRatio);
        lm.high = around (ref->highRatio);
        if (ref->lowMidRatio > 0) lm.lowMid = around (ref->lowMidRatio);
        if (ref->presenceRatio > 0) lm.pres = around (ref->presenceRatio);
        if (ref->subRatio > 0) lm.sub = around (ref->subRatio);
        p.genre = utf8 ("référence");
    }
    const juce::String landmarkName = ref != nullptr ? utf8 ("référence ") : (isClub ? utf8 ("repère club ") : utf8 ("repère "));
    const float k = juce::jlimit (0.0f, 1.0f, r.intensity);
    const bool warm = r.profile == Profile::Warm || r.keepDark;
    const bool open = r.profile == Profile::Open;
    auto add = [&] (const juce::String& title, const juce::String& measured, const juce::String& landmark, const juce::String& action, float sev, bool handled)
    {
        p.findings.push_back ({ title, measured, landmark, action, juce::jlimit (0.0f, 1.0f, sev), handled });
    };

    // ---- tonal balance
    {
        const float d = distDb (f.lowRatio, lm.low);
        if (d > 0.2f)
        {
            p.eqLowDb = -juce::jlimit (0.5f, 3.0f, d * 1.6f) * (warm ? 0.7f : 1.0f);
            add (utf8 ("Graves en excès"), pct (f.lowRatio) + utf8 (" de l'énergie sous 150 Hz"), landmarkName + rng (lm.low),
                 db (p.eqLowDb * k) + utf8 (" en plateau à 110 Hz"), d / 2.0f, true);
        }
        else if (d < -0.2f)
        {
            p.eqLowDb = juce::jlimit (0.5f, 2.5f, -d * 1.2f) * (warm ? 1.2f : 1.0f);
            add (utf8 ("Graves en retrait"), pct (f.lowRatio) + utf8 (" de l'énergie sous 150 Hz"), landmarkName + rng (lm.low),
                 db (p.eqLowDb * k) + utf8 (" en plateau à 110 Hz"), -d / 2.0f, true);
        }
    }
    {
        const float d = distDb (f.subRatio, lm.sub);
        if (d > 0.5f)
            add (utf8 ("Sub très présent"), pct (f.subRatio) + utf8 (" du centre sous 60 Hz"), landmarkName + rng (lm.sub),
                 utf8 ("le leveller tient la bande grave ; dans ton mix, vérifie que kick et basse ne jouent pas le même sub"), d / 3.0f, true);
    }
    {
        const float d = distDb (f.lowMidRatio, lm.lowMid);
        if (d > 0.3f)
        {
            p.eqMudDb = -juce::jlimit (0.5f, 3.0f, 0.5f + d * 1.4f) * (warm ? 0.7f : 1.0f);
            add (utf8 ("Bas-médiums chargés (son boueux)"), pct (f.lowMidRatio) + utf8 (" du médium entre 200 et 500 Hz"), landmarkName + rng (lm.lowMid),
                 db (p.eqMudDb * k) + utf8 (" en cloche à 300 Hz"), d / 2.0f, true);
        }
    }
    {
        const float d = distDb (f.presenceRatio, lm.pres);
        if (d > 0.3f)
        {
            p.eqPresDb = -juce::jlimit (0.5f, 2.5f, d * 1.3f);
            add (utf8 ("Haut-médiums agressifs"), pct (f.presenceRatio) + utf8 (" du médium entre 2 et 5 kHz"), landmarkName + rng (lm.pres),
                 db (p.eqPresDb * k) + utf8 (" en cloche à 3,2 kHz"), d / 2.0f, true);
        }
        else if (d < -0.5f && ! warm)
        {
            p.eqPresDb = juce::jlimit (0.3f, 1.5f, -d * 0.6f) * (open ? 1.3f : 1.0f);
            add (utf8 ("Manque de présence"), pct (f.presenceRatio) + " entre 2 et 5 kHz", landmarkName + rng (lm.pres),
                 db (p.eqPresDb * k) + utf8 (" en cloche à 3,2 kHz"), -d / 3.0f, true);
        }
    }
    {
        const float d = distDb (f.highRatio, lm.high);
        if (d > 0.3f)
        {
            p.eqHighDb = -juce::jlimit (0.5f, 3.0f, d * 1.4f) * (warm ? 1.2f : 1.0f);
            add (utf8 ("Aigus trop brillants"), pct (f.highRatio) + utf8 (" de l'énergie au-dessus de 4 kHz"), landmarkName + rng (lm.high),
                 db (p.eqHighDb * k) + utf8 (" en plateau à 9 kHz"), d / 2.0f, true);
        }
        else if (d < -0.3f)
        {
            if (warm)
                add (utf8 ("Aigus discrets"), pct (f.highRatio) + utf8 (" au-dessus de 4 kHz"), landmarkName + rng (lm.high),
                     utf8 ("laissés tels quels : caractère sombre conservé"), -d / 3.0f, false);
            else
            {
                p.eqHighDb = juce::jlimit (0.5f, 2.5f, -d * 1.1f) * (open ? 1.3f : 1.0f);
                add (utf8 ("Manque d'air"), pct (f.highRatio) + utf8 (" de l'énergie au-dessus de 4 kHz"), landmarkName + rng (lm.high),
                     db (p.eqHighDb * k) + utf8 (" en plateau à 9 kHz"), -d / 2.0f, true);
            }
        }
        else if (open && p.eqHighDb == 0.0f && f.highRatio < lm.high.hi * 0.8f) p.eqHighDb = 0.8f;
    }
    if (r.keepBright) { p.eqHighDb = std::max (0.0f, p.eqHighDb); p.eqPresDb = std::max (0.0f, p.eqPresDb); }
    if (warm) { p.eqHighDb = std::min (0.0f, p.eqHighDb); p.eqPresDb = std::min (0.0f, p.eqPresDb); }

    // ---- stereo and mono compatibility
    const bool monoRisk = f.monoLossDb > 4.5f || f.correlation < 0.2f;
    if (monoRisk)
    {
        p.sideDb = -juce::jlimit (1.0f, 3.0f, std::max (f.monoLossDb - 3.5f, (0.2f - f.correlation) * 6.0f));
        add (utf8 ("Compatibilité mono fragile"), utf8 ("perte de ") + juce::String (f.monoLossDb, 1) + utf8 (" dB en mono, corrélation ") + juce::String (f.correlation, 2),
             utf8 ("moins de 4,5 dB, corrélation > 0,2"), db (p.sideDb * k) + utf8 (" sur les côtés"), 0.7f, true);
    }
    else if (f.sideRatio > lm.side.hi)
    {
        p.sideDb = -1.0f;
        add (utf8 ("Image très large"), pct (f.sideRatio) + utf8 (" de l'énergie sur les côtés"), landmarkName + rng (lm.side), db (p.sideDb * k) + utf8 (" sur les côtés"), 0.4f, true);
    }
    else if (f.sideRatio < lm.side.lo && f.monoLossDb < 3.5f)
    {
        p.sideDb = juce::jlimit (0.5f, 2.0f, (lm.side.lo - f.sideRatio) * 40.0f) * (open ? 1.3f : 1.0f);
        add (utf8 ("Image étroite"), pct (f.sideRatio) + utf8 (" de l'énergie sur les côtés"), landmarkName + rng (lm.side),
             db (p.sideDb * k) + utf8 (" sur les côtés (les graves restent au centre)"), 0.35f, true);
    }
    else if (open && f.monoLossDb < 3.2f) p.sideDb = 0.6f;
    if (f.lowSideRatio > lm.lowSideMax)
    {
        p.monoBass = 1.0f;
        add (utf8 ("Graves en stéréo"), pct (f.lowSideRatio) + utf8 (" des graves sur les côtés"), utf8 ("moins de 5 %"),
             utf8 ("graves remis au centre sous 160 Hz (club et mono)"), 0.5f, true);
    }

    // ---- dynamics
    const bool squashed = f.plr < 8.0f;   // (a short loop has a tiny LRA by nature: the crest factor tells more)
    static constexpr float glueFor[] = { 0.0f, 0.45f, 0.6f, 0.7f, 0.8f, 0.35f };
    static constexpr float clipFor[] = { 0.0f, 0.15f, 0.4f, 0.65f, 0.85f, 0.1f };
    const int t = juce::jlimit (0, numLoudnessTargets - 1, r.targetIndex);
    p.glue = glueFor[t] * (0.5f + 0.7f * k);
    p.clip = clipFor[t] * (0.4f + 0.8f * k);
    if (squashed)
    {
        p.glue *= 0.4f;
        p.clip *= 0.3f;
        add (utf8 ("Mix déjà très compressé"), "PLR " + juce::String (f.plr, 1) + " dB",
             utf8 ("PLR 8-13 dB"), utf8 ("leveller et clipper presque coupés, pas de volume en plus : la dynamique restante est précieuse"), 0.6f, true);
    }
    else if (f.plr > 15.0f)
        add (utf8 ("Crêtes très au-dessus du corps du son"), "PLR " + juce::String (f.plr, 1) + " dB", utf8 ("PLR 8-13 dB"),
             utf8 ("le leveller et le limiteur tiennent les crêtes ; si ça manque d'énergie, regarde les transitoires du kick/snare"), 0.4f, true);
    const float punchThr = ref != nullptr ? ref->punchIndex - 1.5f : 5.0f;
    if (f.punchIndex < punchThr)
    {
        p.punchKeep = 1.0f;
        add (utf8 ("Attaques molles"), utf8 ("les attaques dépassent de ") + juce::String (f.punchIndex, 1) + utf8 (" dB le corps du son"),
             (ref != nullptr ? utf8 ("référence ") : juce::String()) + utf8 ("> ") + juce::String (punchThr, 1) + " dB",
             utf8 ("leveller plus lent et écrêtage réduit pour ne pas les écraser davantage (le punch se crée surtout au mix)"), 0.5f, true);
    }
    if (r.profile == Profile::Punch) { p.punchKeep = 1.0f; p.clip *= 0.4f; p.glue *= 0.7f; }
    if (p.punchKeep > 0.0f) p.clip *= 0.6f;

    // ---- things FINISH cannot repair
    if (f.clipsPerMinute > 3.0f)
        add (utf8 ("Saturation numérique dans ton mix"), juce::String (juce::roundToInt (f.clipsPerMinute)) + utf8 (" échantillons à 0 dBFS par minute"), utf8 ("aucun"),
             utf8 ("à corriger avant FINISH : baisse ta piste Master (ou la piste qui sature) de 3 à 6 dB dans Live. Un master ne peut pas réparer un écrêtage."), 0.8f, false);
    if (f.peakDb > -0.3f && f.clipsPerMinute <= 3.0f)
        add (utf8 ("Pics au ras du 0 dBFS"), utf8 ("crête ") + juce::String (f.peakDb, 1) + " dBFS", utf8 ("-3 à -6 dBFS avant master"),
             utf8 ("FINISH les contrôle ; idéalement garde 3 dB de marge sur ton master dans Live"), 0.3f, false);

    // ---- loudness: never louder by default
    const float L = f.integratedLufs;
    const float genreTarget = isClub ? -11.0f : -14.0f;
    if (squashed || L >= genreTarget - 0.5f)
    {
        p.recommendedTarget = sameLevelTarget;
        p.targetWhy = utf8 ("Ton mix est déjà à ") + juce::String (L, 1) + utf8 (" LUFS : FINISH garde ce niveau (pas de volume en plus, uniquement des corrections).");
    }
    else
    {
        p.recommendedTarget = isClub ? 2 : 1;
        p.targetWhy = utf8 ("Ton mix est à ") + juce::String (L, 1) + utf8 (" LUFS, en dessous du niveau habituel ") + (isClub ? "club" : "streaming")
                    + " (" + juce::String (genreTarget, 0) + utf8 (" LUFS) : FINISH le monte jusque-là, pas plus.");
    }

    // Scale the EQ / width by the intensity.
    p.eqLowDb *= k; p.eqHighDb *= k; p.eqMudDb *= k; p.eqPresDb *= k; p.sideDb *= k;
    std::sort (p.findings.begin(), p.findings.end(), [] (auto& a, auto& b) { return a.severity > b.severity; });
    return p;
}

juce::String report (const MasterPlan& plan, const AudioFeatures& in, const AudioFeatures* out, float outTp,
                     int targetIndex, float targetLufs, float intensity, Profile profile)
{
    juce::String s;
    if (! plan.valid || ! in.valid) return utf8 ("Joue ton morceau (au moins 20 secondes) : FINISH mesure ton mix pendant la lecture.");
    s << utf8 ("ANALYSE DU MIX (") << juce::roundToInt (in.durationSec) << utf8 (" s écoutées, repères ") << plan.genre << ")\n";
    int shown = 0;
    for (auto& f : plan.findings)
    {
        s << (f.handled ? "  + " : "  ! ") << f.title << " : " << f.measured << "  (" << f.landmark << ")\n      -> " << f.action << "\n";
        ++shown;
    }
    if (shown == 0) s << utf8 ("  Rien de hors repères : FINISH reste quasi transparent (limiteur de sécurité seulement).\n");

    s << utf8 ("\nRÉGLAGE : profil ") << profileLabel (profile) << utf8 (", intensité ") << juce::roundToInt (intensity * 100.0f) << " %, cible ";
    if (targetIndex == 0) s << utf8 ("MANUEL (corrections coupées)");
    else if (targetIndex == sameLevelTarget) s << utf8 ("MÊME NIVEAU (") << juce::String (targetLufs, 1) << " LUFS)";
    else s << loudnessTargets[targetIndex].name << " LUFS";
    s << "\n" << plan.targetWhy << "\n";

    if (out != nullptr && out->valid)
    {
        auto row = [&] (const juce::String& name, float a, float b, const juce::String& unit, int dec = 1)
        {
            auto fmt = [dec] (float v) { return dec == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, dec); };
            s << "  " << name.paddedRight (' ', 22) << fmt (a).paddedLeft (' ', 7) << "  ->" << fmt (b).paddedLeft (' ', 7) << "  " << unit << "\n";
        };
        s << utf8 ("\nAVANT -> APRÈS (même passage)\n");
        row (utf8 ("Loudness intégrée"), in.integratedLufs, out->integratedLufs, "LUFS");
        row (utf8 ("Crête"), in.peakDb, outTp > -99.0f ? outTp : out->peakDb, "dBFS");
        row (utf8 ("PLR (crête/loudness)"), in.plr, (outTp > -99.0f ? outTp : out->peakDb) - out->integratedLufs, "dB");
        row (utf8 ("Plage dynamique LRA"), in.loudnessRange, out->loudnessRange, "LU");
        row (utf8 ("Graves < 150 Hz"), in.lowRatio * 100.0f, out->lowRatio * 100.0f, "%", 0);
        row (utf8 ("Bas-médiums 200-500"), in.lowMidRatio * 100.0f, out->lowMidRatio * 100.0f, "%", 1);
        row (utf8 ("Présence 2-5 kHz"), in.presenceRatio * 100.0f, out->presenceRatio * 100.0f, "%", 1);
        row (utf8 ("Aigus > 4 kHz"), in.highRatio * 100.0f, out->highRatio * 100.0f, "%", 1);
        row (utf8 ("Côtés (largeur)"), in.sideRatio * 100.0f, out->sideRatio * 100.0f, "%", 1);
        row (utf8 ("Perte en mono"), in.monoLossDb, out->monoLossDb, "dB");
        row (utf8 ("Attaques (punch)"), in.punchIndex, out->punchIndex, "dB");
        const float gain = out->integratedLufs - in.integratedLufs;
        s << (std::abs (gain) < 0.6f ? utf8 ("  Niveau quasi identique : l'A/B (MATCH) compare le son, pas le volume.\n")
                                     : utf8 ("  Écart de niveau ") + db (gain) + utf8 (" : avec MATCH, l'A/B compare à volume égal.\n"));
    }
    s << utf8 ("\nLIMITES : FINISH corrige l'équilibre global ; il ne peut pas séparer un kick d'une basse ni réparer une saturation. "
               "Les repères sont larges et génériques : charge une référence (REFERENCE) pour une comparaison précise.");
    return s;
}

juce::String explain (const AudioFeatures& f, const MasterPlan& plan, const juce::String& topic)
{
    if (! f.valid) return utf8 ("Joue ton morceau (au moins 20 secondes, le passage le plus fort) : FINISH mesure ton mix pendant la lecture.");
    juce::StringArray why;
    if (topic == "punch" || topic.isEmpty())
    {
        why.add (utf8 ("POURQUOI ÇA MANQUE DE PUNCH — ce que je mesure :"));
        if (f.punchIndex < 5.0f) why.add (utf8 ("- Attaques faibles : elles ne dépassent le corps du son que de ") + juce::String (f.punchIndex, 1) + utf8 (" dB (une mix percutante : 6 dB et plus). Cause fréquente : compression/limiteur trop rapide sur les drums ou le bus, ou kick à attaque molle."));
        else why.add (utf8 ("- Les attaques sont correctes (") + juce::String (f.punchIndex, 1) + utf8 (" dB au-dessus du corps du son)."));
        if (f.plr < 8.0f) why.add (utf8 ("- Mix déjà écrasé : PLR ") + juce::String (f.plr, 1) + utf8 (" dB. Plus de limiteur ne rendra pas le son plus percutant : il faut rendre de la dynamique au mix (moins de compression sur le bus)."));
        if (f.lowMidRatio > 0.44f) why.add (utf8 ("- Bas-médiums chargés (") + pct (f.lowMidRatio) + utf8 (" entre 200 et 500 Hz) : ils masquent le claquement du kick et de la caisse claire."));
        if (f.subRatio > 0.50f) why.add (utf8 ("- Beaucoup de sub (") + pct (f.subRatio) + utf8 (") : si la basse joue en même temps que le kick sur la même zone, le kick ne ressort plus (side-chain ou EQ complémentaire)."));
        if (f.lowSideRatio > 0.05f) why.add (utf8 ("- Graves en stéréo (") + pct (f.lowSideRatio) + utf8 (") : un grave centré tape plus."));
        if (f.presenceRatio < 0.05f) why.add (utf8 ("- Peu de présence entre 2 et 5 kHz : le clic du kick et l'attaque de la caisse claire y vivent."));
        why.add (utf8 ("\nCe que FINISH fait : ") + (plan.punchKeep > 0 ? utf8 ("leveller plus lent et moins d'écrêtage pour garder chaque attaque") : utf8 ("rien de spécial sur les attaques, elles sont correctes"))
                 + (plan.eqMudDb < 0 ? utf8 (", ") + db (plan.eqMudDb) + utf8 (" à 300 Hz") : juce::String()) + ".");
        why.add (utf8 ("Ce qui marche le mieux dans ton projet : side-chain de la basse sur le kick, un transient shaper (ou moins de compression) sur la piste batterie, "
                       "un coup d'EQ -2 dB vers 250-400 Hz sur les nappes/accords."));
    }
    else
    {
        why.add (utf8 ("ANALYSE DU MIX — ") + topic.toUpperCase());
        for (auto& x : plan.findings) why.add ("- " + x.title + " : " + x.measured + " (" + x.landmark + ") -> " + x.action);
        if (plan.findings.empty()) why.add (utf8 ("- Rien de hors repères sur ce qui a été joué."));
    }
    return why.joinIntoString ("\n");
}
}
