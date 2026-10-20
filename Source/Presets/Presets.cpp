#include "Presets.h"
#include "../Auto/AutoMaster.h"

namespace
{
    juce::String fr (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    struct Def
    {
        const char* name; const char* desc; int style; int target; float ceiling;
        float colour[7]; float dyn[4];
        float punch, sat, glue, clip, side, detail, mono;
    };

    // name, description, style, loudness target, ceiling,
    // colour (SUB 25-50, BASS 63-125, LOW-MID 160-400, MID 500-1.6k, PRESENCE 2-5k, HIGHS 6-10k, AIR 12-16k) in units
    //   of the style's reference spread (+1 = towards the upper quartile of your references, -1 = lower quartile),
    // spectral dynamics (sub, low-mid, presence, highs), punch, saturation, glue, clipper, sides dB, detail, mono Hz
    const Def defs[] = {
        { "Club Punch", "Kick forward, bright highs, firm limiting", styleTechHouse, styleTarget, -1.0f,
          { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f }, { 0.3f, 0.3f, 0.4f, 0.3f }, 0.6f, 0.1f, 0.15f, 0.6f, 0.0f, 0.45f, 140.0f },
        { "Clean Groove", "Transparent, keeps the groove and the dynamics", styleTechHouse, styleTarget, -1.0f,
          { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f }, { 0.2f, 0.2f, 0.3f, 0.2f }, 0.2f, 0.0f, 0.1f, 0.2f, 0.0f, 0.6f, 130.0f },
        { "Rolling Bass", "Round bass and steady sub, smooth top end", styleTechHouse, styleTarget, -1.0f,
          { 1.0f, 1.5f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f }, { 0.4f, 0.3f, 0.4f, 0.3f }, 0.15f, 0.3f, 0.45f, 0.4f, 0.0f, 0.5f, 150.0f },
        { "Peak Time", "Loudest for the club without breaking the kick", styleTechHouse, 4, -0.5f,
          { 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f }, { 0.4f, 0.4f, 0.5f, 0.4f }, 0.2f, 0.25f, 0.55f, 0.75f, 0.0f, 0.4f, 150.0f },
        { "Deep & Warm", "Analog warmth, rounded highs, wide", styleTechHouse, styleTarget, -1.0f,
          { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f }, { 0.2f, 0.2f, 0.5f, 0.4f }, 0.05f, 0.5f, 0.35f, 0.3f, 1.0f, 0.55f, 130.0f },
        { "Afro Organic", "Natural percussion, open dynamics", styleAfro, styleTarget, -1.0f,
          { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f }, { 0.2f, 0.2f, 0.4f, 0.3f }, 0.15f, 0.1f, 0.1f, 0.15f, 0.5f, 0.65f, 130.0f },
        { "Latin Groove", "Congas and vocals forward, warm bass", styleAfro, styleTarget, -1.0f,
          { 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f }, { 0.2f, 0.2f, 0.4f, 0.3f }, 0.5f, 0.15f, 0.2f, 0.5f, 0.3f, 0.55f, 130.0f },
        { "Sunset Edit", "Smooth and wide for edits and vocals", styleAfro, styleTarget, -1.0f,
          { 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, -1.0f }, { 0.3f, 0.2f, 0.5f, 0.4f }, 0.05f, 0.4f, 0.25f, 0.2f, 1.0f, 0.6f, 120.0f },
        { "Afro Tech", "Tighter and louder, for peak time", styleAfroTech, 3, -1.0f,
          { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f }, { 0.4f, 0.3f, 0.4f, 0.3f }, 0.35f, 0.15f, 0.5f, 0.6f, 0.0f, 0.45f, 140.0f },
        { "Rave Hammer", "Hard, compact kick, very loud", styleHardTechno, styleTarget, -0.5f,
          { 0.0f, 1.0f, -1.0f, 0.0f, 0.5f, 0.0f, 0.0f }, { 0.4f, 0.4f, 0.5f, 0.4f }, 0.25f, 0.4f, 0.55f, 0.8f, 0.0f, 0.35f, 160.0f },
        { "Hard Groove", "Fast and percussive, keeps the bounce", styleHardTechno, styleTarget, -1.0f,
          { 0.0f, 1.0f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f }, { 0.3f, 0.3f, 0.4f, 0.3f }, 0.6f, 0.2f, 0.15f, 0.6f, 0.0f, 0.45f, 160.0f },
        { "Industrial", "Saturated and dark, highs held back", styleHardTechno, styleTarget, -0.5f,
          { 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, -1.0f, -1.0f }, { 0.4f, 0.3f, 0.6f, 0.5f }, 0.15f, 0.75f, 0.5f, 0.7f, -0.5f, 0.4f, 170.0f },
        { "Schranz Edge", "Biting mids, kick forward", styleHardTechno, styleTarget, -0.5f,
          { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f }, { 0.4f, 0.4f, 0.6f, 0.4f }, 0.4f, 0.45f, 0.35f, 0.75f, 0.0f, 0.35f, 160.0f },
        { "DnB Impact", "Very loud, controlled sub, snare forward", styleDnb, styleTarget, -0.5f,
          { -0.5f, 0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 0.5f }, { 0.6f, 0.3f, 0.5f, 0.4f }, 0.6f, 0.2f, 0.2f, 0.7f, 0.0f, 0.4f, 160.0f },
        { "Liquid", "Smooth and wide, dynamics preserved", styleDnb, 3, -1.0f,
          { 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f }, { 0.4f, 0.2f, 0.4f, 0.3f }, 0.15f, 0.1f, 0.1f, 0.2f, 0.8f, 0.6f, 140.0f },
        { "Neuro Pressure", "Aggressive bass held in, sharp transients", styleDnb, styleTarget, -0.5f,
          { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -0.5f }, { 0.6f, 0.4f, 0.6f, 0.4f }, 0.2f, 0.5f, 0.6f, 0.8f, 0.0f, 0.35f, 170.0f },
        { "Jump Up", "Big sub, wobble forward, very loud", styleDnb, styleTarget, -0.5f,
          { 1.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f }, { 0.6f, 0.3f, 0.5f, 0.3f }, 0.45f, 0.3f, 0.4f, 0.75f, 0.0f, 0.4f, 170.0f },
        { "2-Step Shuffle", "Swing preserved, warm bass", styleUkg, styleTarget, -1.0f,
          { 0.0f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f }, { 0.3f, 0.3f, 0.4f, 0.3f }, 0.55f, 0.15f, 0.15f, 0.5f, 0.3f, 0.55f, 140.0f },
        { "Speed Garage", "Driving bass, tight kick", styleUkg, styleTarget, -1.0f,
          { 0.0f, 1.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f }, { 0.4f, 0.3f, 0.4f, 0.3f }, 0.4f, 0.3f, 0.4f, 0.55f, 0.0f, 0.45f, 150.0f },
        { "Bass House", "Biting bass mids, club-ready", styleUkg, 3, -0.5f,
          { 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f }, { 0.5f, 0.3f, 0.5f, 0.3f }, 0.2f, 0.45f, 0.55f, 0.7f, 0.0f, 0.4f, 150.0f },
        { "Soft Lo-fi", "Muted, no squashing, keeps the hiss", styleLofi, styleTarget, -1.0f,
          { 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f }, { 0.0f, 0.2f, 0.4f, 0.4f }, 0.0f, 0.2f, 0.1f, 0.0f, 0.0f, 0.75f, 0.0f },
        { "Tape Warmth", "Tape saturation, rounded lows", styleLofi, styleTarget, -1.0f,
          { 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, -1.0f, -1.5f }, { 0.0f, 0.2f, 0.4f, 0.5f }, 0.0f, 0.65f, 0.25f, 0.0f, 0.5f, 0.7f, 0.0f },
        { "Chill Streaming", "Made for Spotify / Apple Music", styleLofi, 1, -1.0f,
          { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f }, { 0.0f, 0.2f, 0.4f, 0.3f }, 0.05f, 0.15f, 0.2f, 0.05f, 0.0f, 0.65f, 0.0f },
        { "Clean Streaming", "-14 LUFS, transparent, for every platform", styleTechHouse, 1, -1.0f,
          { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.3f, 0.2f }, 0.1f, 0.05f, 0.2f, 0.15f, 0.0f, 0.6f, 120.0f },
        { "Quick Demo", "Just loud and clean to send a demo", styleTechHouse, 3, -1.0f,
          { 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f }, { 0.3f, 0.3f, 0.3f, 0.3f }, 0.25f, 0.15f, 0.45f, 0.5f, 0.0f, 0.5f, 140.0f },
        { "Riddim Pressure", "Steady sub, tight kick, lighter low-mids, loud", styleAfroTech, styleTarget, -1.0f,
          { 1.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.5f, 0.0f }, { 0.4f, 0.3f, 0.4f, 0.3f }, 0.6f, 0.2f, 0.15f, 0.6f, 0.0f, 0.45f, 150.0f },
        { "Afro Tech Percu", "Lively, airy percussion, open groove", styleAfroTech, styleTarget, -1.0f,
          { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.5f }, { 0.3f, 0.3f, 0.5f, 0.4f }, 0.55f, 0.1f, 0.15f, 0.5f, 0.5f, 0.55f, 140.0f },
        { "Dancehall Edit", "Solid sub, vocals forward, dry kick", styleDancehall, styleTarget, -1.0f,
          { 1.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f }, { 0.4f, 0.3f, 0.4f, 0.3f }, 0.55f, 0.15f, 0.15f, 0.55f, 0.0f, 0.45f, 150.0f },
        { "Bashment Club", "Loudest for the club, dense low end", styleDancehall, 3, -0.5f,
          { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 0.5f, 0.3f, 0.5f, 0.3f }, 0.2f, 0.35f, 0.55f, 0.75f, 0.0f, 0.4f, 160.0f },
        { "Afrobeats Smooth", "Warm, silky vocals, breathing dynamics", styleDancehall, styleTarget, -1.0f,
          { 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, -0.5f, 0.5f }, { 0.3f, 0.2f, 0.5f, 0.3f }, 0.1f, 0.35f, 0.3f, 0.25f, 0.5f, 0.6f, 140.0f },
        // 1.2: EDM / Big Room
        { "Main Stage", "Huge and loud for festival drops", styleEdm, styleTarget, -0.5f,
          { 0.5f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.5f }, { 0.5f, 0.4f, 0.5f, 0.4f }, 0.3f, 0.3f, 0.55f, 0.8f, 0.5f, 0.35f, 160.0f },
        { "Big Room Kick", "Kick in front, tight low end, hard-hitting", styleEdm, styleTarget, -0.5f,
          { 0.0f, 1.5f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f }, { 0.4f, 0.4f, 0.4f, 0.3f }, 0.65f, 0.2f, 0.2f, 0.7f, 0.0f, 0.4f, 160.0f },
        { "Euphoric Lead", "Bright, wide supersaws, open top end", styleEdm, styleTarget, -1.0f,
          { 0.0f, 0.0f, -0.5f, 0.5f, 1.0f, 1.0f, 1.0f }, { 0.3f, 0.3f, 0.6f, 0.5f }, 0.2f, 0.15f, 0.35f, 0.5f, 1.0f, 0.5f, 150.0f },
        { "Radio Edit", "Balanced -11 LUFS, cleaner for streaming and radio", styleEdm, 2, -1.0f,
          { 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f }, { 0.3f, 0.3f, 0.4f, 0.3f }, 0.25f, 0.1f, 0.3f, 0.3f, 0.3f, 0.55f, 140.0f },
    };
}

namespace
{
    // regions of the 29 1/3-octave bands used by the colour recipes
    constexpr int regLo[7] = { 0, 4, 8, 13, 19, 24, 27 }, regHi[7] = { 3, 7, 12, 18, 23, 26, 28 };
    const char* regName[7] = { "sub", "bass", "low-mids", "mids", "presence", "highs", "air" };

    // Colour curve of a recipe, built from the style's reference spread: +1 in a region moves it the way the upper
    // quarter of your references sits above their median (at least 2 dB, at most 4 dB, so it is always audible
    // and never extreme); -1 towards the lower quarter.
    void colourFromRecipe (const float recipe[7], int style, float out[29])
    {
        const auto& p = AutoMaster::profile (style);
        float raw[29] {};
        for (int r = 0; r < 7; ++r)
        {
            if (recipe[r] == 0.0f) continue;
            for (int b = std::max (0, regLo[r] - 1); b <= std::min (28, regHi[r] + 1); ++b)
            {
                const float inside = (b >= regLo[r] && b <= regHi[r]) ? 1.0f : 0.4f;      // soft edges
                const float spread = recipe[r] > 0 ? p.thirdOctP75[b] - p.thirdOctMedian[b] : p.thirdOctMedian[b] - p.thirdOctP25[b];
                const float unit = juce::jlimit (2.0f, 4.0f, 2.0f * spread);
                raw[b] += recipe[r] * unit * inside;
            }
        }
        for (int b = 0; b < 29; ++b)
        {
            const int l = std::max (0, b - 1), h = std::min (28, b + 1);
            out[b] = juce::jlimit (-4.5f, 4.5f, 0.25f * raw[l] + 0.5f * raw[b] + 0.25f * raw[h]);
        }
    }

    juce::String tagsFor (const float recipe[7], float punch, float sat, float glue, float clip, float side)
    {
        juce::StringArray t;
        for (int r = 0; r < 7; ++r)
            if (std::abs (recipe[r]) >= 0.5f) t.add (fr (recipe[r] > 0 ? "+ " : "\xe2\x88\x92 ") + fr (regName[r]));
        if (punch >= 0.4f) t.add ("strong punch"); else if (punch >= 0.25f) t.add ("punch");
        if (sat >= 0.5f) t.add ("saturated"); else if (sat >= 0.25f) t.add ("warm");
        if (glue + clip >= 1.1f) t.add ("dense");
        else if (glue + clip <= 0.45f) t.add ("dynamic");
        if (side >= 0.8f) t.add ("wide");
        return t.joinIntoString (fr (" \xc2\xb7 "));
    }
}

PresetManager::PresetManager()
{
    for (auto& d : defs)
    {
        WhalePreset p;
        p.name = fr (d.name); p.description = fr (d.desc);
        p.style = d.style; p.target = d.target; p.ceiling = d.ceiling;
        p.hasColour = true;
        colourFromRecipe (d.colour, d.style, p.colour);
        AutoMaster::fitEq (p.colour, d.style == styleLofi, false, p.base);   // the same colour as EQ settings (no AUTO yet)
        for (int b = 0; b < 4; ++b) p.base.specDyn[b] = d.dyn[b];
        p.base.compGlue = d.glue; p.base.compPunch = d.punch; p.base.compDetail = d.detail; p.base.satDrive = d.sat;
        p.base.widthSide = d.side; p.base.widthMono = d.mono; p.base.clipAmt = d.clip;
        // loudness is part of the character: a dynamic preset keeps more headroom (the limiter would flatten it
        // otherwise), a punch preset a little, a dense one goes a little louder
        p.loudOffset = (d.glue + d.clip <= 0.45f) ? -1.0f : (d.punch >= 0.5f ? -0.5f : (d.glue + d.clip >= 1.2f ? 0.5f : 0.0f));
        p.tags = tagsFor (d.colour, d.punch, d.sat, d.glue, d.clip, d.side);
        if (p.loudOffset != 0.0f && p.target == styleTarget)
            p.tags << fr (" \xc2\xb7 ") << (p.loudOffset > 0 ? "+" : fr ("\xe2\x88\x92")) << juce::String (std::abs (p.loudOffset), 1) << " dB";
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
