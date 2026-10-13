#include "Presets.h"

namespace
{
    juce::String fr (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    struct Def
    {
        const char* name; const char* desc; int style; int target; float ceiling;
        float g0, g1, g2, g3, d0, d1, d2, d3;
        float glue, punch, detail, sat, side, mono, low, air, clip;
    };

    // name, description, style, target, ceiling,
    // spectral gains (sub, low-mid, presence, highs), spectral dynamics,
    // glue, punch, detail, saturation, sides dB, mono Hz, bass dB, air dB, clipper
    const Def defs[] = {
        // ---- Tech House / House
        { "Club Punch", "Kick devant, aigus brillants, limiteur ferme", styleTechHouse, styleTarget, -1.0f,
          0, -1.0f, 0.5f, 0, 0.3f, 0.3f, 0.4f, 0.3f,  0.40f, 0.35f, 0.45f, 0.15f, 0.0f, 140, 0.5f, 1.0f, 0.45f },
        { "Groove Propre", "Transparent, garde le groove et la dynamique", styleTechHouse, styleTarget, -1.0f,
          0, -0.5f, 0, 0, 0.2f, 0.2f, 0.3f, 0.2f,  0.25f, 0.20f, 0.60f, 0.08f, 0.0f, 130, 0, 0.5f, 0.25f },
        { "Rolling Bass", "Basse ronde et sub tenu, haut du spectre doux", styleTechHouse, styleTarget, -1.0f,
          0.5f, -1.0f, 0, -0.5f, 0.4f, 0.3f, 0.4f, 0.3f,  0.40f, 0.20f, 0.50f, 0.20f, 0.0f, 150, 1.0f, 0, 0.40f },
        { "Peak Time", "Le plus fort pour le club sans casser le kick", styleTechHouse, 4, -0.5f,
          0, -1.0f, 0.5f, 0, 0.4f, 0.4f, 0.5f, 0.4f,  0.50f, 0.30f, 0.40f, 0.25f, 0.0f, 150, 0.5f, 1.0f, 0.65f },
        { "Deep & Warm", "Chaleur analogique, aigus arrondis, large", styleTechHouse, styleTarget, -1.0f,
          0, 0.5f, -0.5f, -1.0f, 0.2f, 0.2f, 0.5f, 0.4f,  0.35f, 0.10f, 0.55f, 0.35f, 0.8f, 130, 0.5f, -0.5f, 0.30f },
        // ---- Afro / Latin House
        { "Afro Organic", "Percussions naturelles, dynamique ouverte", styleAfro, styleTarget, -1.0f,
          0, 0.5f, -0.5f, 0, 0.2f, 0.2f, 0.4f, 0.3f,  0.25f, 0.15f, 0.65f, 0.15f, 0.5f, 130, 0, 0.5f, 0.20f },
        { "Latin Groove", "Congas et voix devant, basse chaude", styleAfro, styleTarget, -1.0f,
          0, 0, 0.8f, 0.3f, 0.2f, 0.2f, 0.4f, 0.3f,  0.30f, 0.25f, 0.55f, 0.15f, 0.3f, 130, 0.5f, 0.8f, 0.30f },
        { "Sunset Edit", "Doux et large pour les edits et les voix", styleAfro, styleTarget, -1.0f,
          -0.5f, 0.5f, -0.8f, -0.5f, 0.3f, 0.2f, 0.5f, 0.4f,  0.30f, 0.05f, 0.60f, 0.25f, 1.0f, 120, 0.5f, 0, 0.20f },
        // ---- Afro Tech / Riddim (this one was in Afro before the style existed: it keeps its place in the list)
        { "Afro Tech", "Plus serré et plus fort, pour les peak times", styleAfroTech, 3, -1.0f,
          0, -0.8f, 0.5f, 0, 0.4f, 0.3f, 0.4f, 0.3f,  0.45f, 0.30f, 0.45f, 0.20f, 0.0f, 140, 0.5f, 0.8f, 0.45f },
        // ---- Hard Techno / Rave
        { "Rave Hammer", "Kick dur et compact, très fort", styleHardTechno, styleTarget, -0.5f,
          0, -1.5f, 0.5f, 0, 0.4f, 0.4f, 0.5f, 0.4f,  0.50f, 0.30f, 0.35f, 0.30f, 0.0f, 160, 0, 0.5f, 0.70f },
        { "Hard Groove", "Rapide et percussif, garde le rebond", styleHardTechno, styleTarget, -1.0f,
          0, -1.0f, 0.3f, 0, 0.3f, 0.3f, 0.4f, 0.3f,  0.40f, 0.35f, 0.45f, 0.25f, 0.0f, 160, 0.3f, 0.5f, 0.55f },
        { "Industrial", "Saturé et sombre, aigus contenus", styleHardTechno, styleTarget, -0.5f,
          0, -0.5f, -0.5f, -1.0f, 0.4f, 0.3f, 0.6f, 0.5f,  0.50f, 0.20f, 0.40f, 0.55f, -0.5f, 170, 0.5f, -0.5f, 0.65f },
        { "Schranz Edge", "Médiums mordants, kick devant", styleHardTechno, styleTarget, -0.5f,
          0, -1.0f, 1.0f, 0.5f, 0.4f, 0.4f, 0.6f, 0.4f,  0.45f, 0.40f, 0.35f, 0.35f, 0.0f, 160, 0, 0.5f, 0.70f },
        // ---- Drum & Bass
        { "DnB Impact", "Très fort, sub contrôlé, caisse claire devant", styleDnb, styleTarget, -0.5f,
          -0.5f, -1.0f, 0.8f, 0, 0.6f, 0.3f, 0.5f, 0.4f,  0.50f, 0.35f, 0.40f, 0.25f, 0.0f, 160, 0, 0.8f, 0.70f },
        { "Liquid", "Doux et large, dynamique préservée", styleDnb, 3, -1.0f,
          0, -0.5f, 0, 0, 0.4f, 0.2f, 0.4f, 0.3f,  0.30f, 0.20f, 0.60f, 0.15f, 0.8f, 140, 0, 1.0f, 0.35f },
        { "Neuro Pressure", "Basses agressives tenues, transitoires vives", styleDnb, styleTarget, -0.5f,
          -0.5f, -1.5f, 0.5f, 0.3f, 0.6f, 0.4f, 0.6f, 0.4f,  0.55f, 0.40f, 0.35f, 0.35f, 0.0f, 170, 0, 0.5f, 0.75f },
        { "Jump Up", "Gros sub, wobble devant, très fort", styleDnb, styleTarget, -0.5f,
          0.5f, -1.0f, 0.5f, 0, 0.6f, 0.3f, 0.5f, 0.3f,  0.50f, 0.30f, 0.40f, 0.30f, 0.0f, 170, 0.5f, 0.5f, 0.75f },
        // ---- UK Garage / Bass
        { "2-Step Shuffle", "Swing préservé, basse chaude", styleUkg, styleTarget, -1.0f,
          0, 0, 0.5f, 0.3f, 0.3f, 0.3f, 0.4f, 0.3f,  0.30f, 0.30f, 0.55f, 0.20f, 0.3f, 140, 0.3f, 0.8f, 0.40f },
        { "Speed Garage", "Basse qui pousse, kick serré", styleUkg, styleTarget, -1.0f,
          0, -0.8f, 0.5f, 0, 0.4f, 0.3f, 0.4f, 0.3f,  0.40f, 0.35f, 0.45f, 0.25f, 0.0f, 150, 0.5f, 0.5f, 0.50f },
        { "Bass House", "Médiums de basse mordants, très club", styleUkg, 3, -0.5f,
          0, -0.5f, 0.8f, 0, 0.5f, 0.3f, 0.5f, 0.3f,  0.50f, 0.30f, 0.40f, 0.35f, 0.0f, 150, 0, 0.5f, 0.60f },
        // ---- Lo-fi / Chill
        { "Lo-fi Doux", "Feutré, aucun écrasement, garde le souffle", styleLofi, styleTarget, -1.0f,
          0, 0, -0.5f, -1.0f, 0, 0.2f, 0.4f, 0.4f,  0.15f, 0.0f, 0.75f, 0.30f, 0.0f, 0, 0.5f, -0.5f, 0.0f },
        { "Tape Warmth", "Saturation cassette, bas arrondis", styleLofi, styleTarget, -1.0f,
          0, 0.8f, -0.8f, -1.5f, 0, 0.2f, 0.4f, 0.5f,  0.20f, 0.0f, 0.70f, 0.50f, 0.5f, 0, 1.0f, -1.0f, 0.0f },
        { "Chill Streaming", "Pensé pour Spotify / Apple Music", styleLofi, 1, -1.0f,
          0, 0, 0, -0.5f, 0, 0.2f, 0.4f, 0.3f,  0.20f, 0.05f, 0.65f, 0.25f, 0.0f, 0, 0.3f, 0, 0.05f },
        // ---- Universal
        { "Streaming Propre", "-14 LUFS, transparent, pour toutes les plateformes", styleTechHouse, 1, -1.0f,
          0, -0.5f, 0, 0, 0.2f, 0.2f, 0.3f, 0.2f,  0.25f, 0.15f, 0.60f, 0.10f, 0.0f, 120, 0, 0.3f, 0.15f },
        { "Demo Rapide", "Juste fort et propre pour envoyer une démo", styleTechHouse, 3, -1.0f,
          0, -0.5f, 0.3f, 0, 0.3f, 0.3f, 0.3f, 0.3f,  0.40f, 0.25f, 0.50f, 0.15f, 0.0f, 140, 0, 0.5f, 0.45f },
        // ---- Afro Tech / Riddim (appended: existing preset numbers stay the same)
        { "Riddim Pressure", "Sub tenu, kick serré, bas-médium allégé, fort", styleAfroTech, styleTarget, -1.0f,
          0.5f, -1.0f, 0.3f, 0.3f, 0.4f, 0.3f, 0.4f, 0.3f,  0.45f, 0.35f, 0.45f, 0.20f, 0.0f, 150, -0.5f, 1.0f, 0.55f },
        { "Afro Tech Percu", "Percussions vives et aérées, groove ouvert", styleAfroTech, styleTarget, -1.0f,
          0, -0.8f, 0.5f, 0.5f, 0.3f, 0.3f, 0.5f, 0.4f,  0.35f, 0.40f, 0.55f, 0.15f, 0.5f, 140, 0, 1.2f, 0.45f },
        // ---- Afro / Dancehall (club edits of dancehall, afrobeats, baile)
        { "Dancehall Edit", "Sub qui tient, voix devant, kick sec", styleDancehall, styleTarget, -1.0f,
          0.5f, -0.8f, 0.5f, 0, 0.4f, 0.3f, 0.4f, 0.3f,  0.40f, 0.35f, 0.45f, 0.20f, 0.0f, 150, 0, 0.8f, 0.50f },
        { "Bashment Club", "Le plus fort pour le club, basses denses", styleDancehall, 3, -0.5f,
          0.5f, -1.0f, 0.3f, 0, 0.5f, 0.3f, 0.5f, 0.3f,  0.50f, 0.30f, 0.40f, 0.25f, 0.0f, 160, 0.5f, 0.5f, 0.65f },
        { "Afrobeats Smooth", "Chaleureux, voix soyeuses, dynamique respirée", styleDancehall, styleTarget, -1.0f,
          0, 0.3f, -0.3f, 0, 0.3f, 0.2f, 0.5f, 0.3f,  0.30f, 0.20f, 0.60f, 0.20f, 0.5f, 140, 0.5f, 0.5f, 0.30f },
    };
}

PresetManager::PresetManager()
{
    for (auto& d : defs)
    {
        WhalePreset p;
        p.name = fr (d.name); p.description = fr (d.desc);
        p.style = d.style; p.target = d.target; p.ceiling = d.ceiling;
        const float g[4] = { d.g0, d.g1, d.g2, d.g3 }, dy[4] = { d.d0, d.d1, d.d2, d.d3 };
        for (int b = 0; b < 4; ++b) { p.base.specGain[b] = g[b]; p.base.specDyn[b] = dy[b]; }
        p.base.compGlue = d.glue; p.base.compPunch = d.punch; p.base.compDetail = d.detail; p.base.satDrive = d.sat;
        p.base.widthSide = d.side; p.base.widthMono = d.mono; p.base.toneLow = d.low; p.base.toneAir = d.air; p.base.clipAmt = d.clip;
        factory.add (p);
    }
    loadFavourites();
    refreshUserPresets();
}

juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WHALE").getChildFile ("Presets");
}

void PresetManager::refreshUserPresets()
{
    all = factory;
    auto dir = userFolder();
    if (! dir.isDirectory()) return;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.whalepreset"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("WHALEPRESET")) continue;
        WhalePreset p;
        p.factory = false; p.file = f;
        p.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
        p.description = xml->getStringAttribute ("description");
        p.style = juce::jlimit (1, numStyles - 1, xml->getIntAttribute ("style", styleTechHouse));
        p.target = juce::jlimit (0, numLoudnessTargets - 1, xml->getIntAttribute ("target", styleTarget));
        p.ceiling = (float) xml->getDoubleAttribute ("ceiling", -1.0);
        auto& b = p.base;
        for (int k = 0; k < 4; ++k)
        {
            b.specGain[k] = (float) xml->getDoubleAttribute (PID::specGain[k]);
            b.specDyn[k] = (float) xml->getDoubleAttribute (PID::specDyn[k]);
            b.specFreq[k] = (float) xml->getDoubleAttribute (PID::specFreq[k], specBands[k].hz);
        }
        b.compGlue = (float) xml->getDoubleAttribute (PID::compGlue);
        b.compPunch = (float) xml->getDoubleAttribute (PID::compPunch);
        b.compDetail = (float) xml->getDoubleAttribute (PID::compDetail, 0.5);
        b.satDrive = (float) xml->getDoubleAttribute (PID::satDrive);
        b.widthSide = (float) xml->getDoubleAttribute (PID::widthSide);
        b.widthMono = (float) xml->getDoubleAttribute (PID::widthMono);
        b.toneLow = (float) xml->getDoubleAttribute (PID::toneLow);
        b.toneAir = (float) xml->getDoubleAttribute (PID::toneAir);
        b.clipAmt = (float) xml->getDoubleAttribute (PID::clipAmt);
        p.lowCut = (float) xml->getDoubleAttribute (PID::lowCut, -1.0);
        all.add (p);
    }
}

void PresetManager::apply (const WhalePreset& p, juce::AudioProcessorValueTreeState& st) const
{
    auto set = [&] (const char* id, float v)
    {
        if (auto* prm = st.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    for (int b = 0; b < 4; ++b) { set (PID::specGain[b], p.base.specGain[b]); set (PID::specDyn[b], p.base.specDyn[b]); set (PID::specFreq[b], p.base.specFreq[b]); }
    set (PID::compGlue, p.base.compGlue); set (PID::compPunch, p.base.compPunch); set (PID::compDetail, p.base.compDetail);
    set (PID::satDrive, p.base.satDrive); set (PID::widthSide, p.base.widthSide); set (PID::widthMono, p.base.widthMono);
    set (PID::toneLow, p.base.toneLow); set (PID::toneAir, p.base.toneAir); set (PID::clipAmt, p.base.clipAmt);
    if (p.lowCut >= 0.0f) set (PID::lowCut, p.lowCut);
    set (PID::target, (float) p.target);
    set (PID::ceiling, p.ceiling);
    if (auto* sp = st.getParameter (PID::style))
        if (sp->convertFrom0to1 (sp->getValue()) != (float) styleAuto)   // keep "Auto" if the user chose it
            set (PID::style, (float) p.style);
    for (auto* id : { PID::mGlue, PID::mPunch, PID::mWarmth, PID::mAir, PID::mWidth, PID::mBass, PID::mDetail, PID::mLoud })
        set (id, 0.5f);
}

WhalePreset PresetManager::capture (const juce::AudioProcessorValueTreeState& st, const juce::String& name) const
{
    WhalePreset p;
    p.name = name; p.factory = false;
    auto get = [&] (const char* id) { return st.getRawParameterValue (id)->load(); };
    for (int b = 0; b < 4; ++b) { p.base.specGain[b] = get (PID::specGain[b]); p.base.specDyn[b] = get (PID::specDyn[b]); p.base.specFreq[b] = get (PID::specFreq[b]); }
    p.base.compGlue = get (PID::compGlue); p.base.compPunch = get (PID::compPunch); p.base.compDetail = get (PID::compDetail);
    p.base.satDrive = get (PID::satDrive); p.base.widthSide = get (PID::widthSide); p.base.widthMono = get (PID::widthMono);
    p.base.toneLow = get (PID::toneLow); p.base.toneAir = get (PID::toneAir); p.base.clipAmt = get (PID::clipAmt);
    p.lowCut = p.base.lowCut = get (PID::lowCut);
    p.style = juce::jmax (1, (int) get (PID::style));
    p.target = (int) get (PID::target);
    p.ceiling = get (PID::ceiling);
    return p;
}

bool PresetManager::saveUserPreset (const WhalePreset& p)
{
    auto dir = userFolder();
    if (! dir.createDirectory()) return false;
    juce::XmlElement x ("WHALEPRESET");
    x.setAttribute ("name", p.name);
    x.setAttribute ("description", p.description);
    x.setAttribute ("style", p.style);
    x.setAttribute ("target", p.target);
    x.setAttribute ("ceiling", p.ceiling);
    for (int k = 0; k < 4; ++k) { x.setAttribute (PID::specGain[k], p.base.specGain[k]); x.setAttribute (PID::specDyn[k], p.base.specDyn[k]); x.setAttribute (PID::specFreq[k], p.base.specFreq[k]); }
    x.setAttribute (PID::compGlue, p.base.compGlue); x.setAttribute (PID::compPunch, p.base.compPunch); x.setAttribute (PID::compDetail, p.base.compDetail);
    x.setAttribute (PID::satDrive, p.base.satDrive); x.setAttribute (PID::widthSide, p.base.widthSide); x.setAttribute (PID::widthMono, p.base.widthMono);
    x.setAttribute (PID::toneLow, p.base.toneLow); x.setAttribute (PID::toneAir, p.base.toneAir); x.setAttribute (PID::clipAmt, p.base.clipAmt);
    if (p.lowCut >= 0.0f) x.setAttribute (PID::lowCut, p.lowCut);
    const auto file = dir.getChildFile (juce::File::createLegalFileName (p.name) + ".whalepreset");
    const bool ok = x.writeTo (file);
    refreshUserPresets();
    return ok;
}

void PresetManager::loadFavourites()
{
    auto f = userFolder().getSiblingFile ("favourites.txt");
    if (f.existsAsFile()) favourites.addLines (f.loadFileAsString());
    favourites.removeEmptyStrings();
}

void PresetManager::saveFavourites() const
{
    auto f = userFolder().getSiblingFile ("favourites.txt");
    f.getParentDirectory().createDirectory();
    f.replaceWithText (favourites.joinIntoString ("\n"));
}

bool PresetManager::isFavourite (const juce::String& name) const { return favourites.contains (name); }

void PresetManager::setFavourite (const juce::String& name, bool fav)
{
    if (fav) favourites.addIfNotAlreadyThere (name); else favourites.removeString (name);
    saveFavourites();
}
