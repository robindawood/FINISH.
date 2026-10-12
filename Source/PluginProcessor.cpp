#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String fr (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
    inline float powerToLufs (double p) { return -0.691f + 10.0f * (float) std::log10 (std::max (p, 1.0e-12)); }
}

WhaleAudioProcessor::WhaleAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "WHALE", createParameterLayout())
{
    auto raw = [this] (const char* id) { auto* v = state.getRawParameterValue (id); jassert (v != nullptr); return v; };
    pStyle = raw (PID::style); pTarget = raw (PID::target); pCeil = raw (PID::ceiling); pDosage = raw (PID::dosage);
    pBypass = raw (PID::bypass); pMatch = raw (PID::match); pManual = raw (PID::manualDrive);
    const char* macros[8] = { PID::mGlue, PID::mPunch, PID::mWarmth, PID::mAir, PID::mWidth, PID::mBass, PID::mDetail, PID::mLoud };
    for (int i = 0; i < 8; ++i) pM[i] = raw (macros[i]);
    const char* ons[6] = { PID::specOn, PID::compOn, PID::satOn, PID::widthOn, PID::toneOn, PID::clipOn };
    for (int i = 0; i < 6; ++i) pOn[i] = raw (ons[i]);
    for (int b = 0; b < 4; ++b) { pSpecG[b] = raw (PID::specGain[b]); pSpecD[b] = raw (PID::specDyn[b]); }
    pGlue = raw (PID::compGlue); pPunch = raw (PID::compPunch); pDetail = raw (PID::compDetail); pSat = raw (PID::satDrive);
    pSide = raw (PID::widthSide); pMono = raw (PID::widthMono); pLow = raw (PID::toneLow); pAir = raw (PID::toneAir); pClip = raw (PID::clipAmt);
    inAnalyzer.setPublishSamples (false);
    outAnalyzer.setPublishSamples (false);
    startTimerHz (30);
}

WhaleAudioProcessor::~WhaleAudioProcessor() { stopTimer(); }

bool WhaleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void WhaleAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    inAnalyzer.prepare (sampleRate);
    outAnalyzer.prepare (sampleRate);
    setLatencySamples (dsp.getLatencySamples());
}

int WhaleAudioProcessor::getActiveStyle() const
{
    const int s = (int) pStyle->load();
    if (s != styleAuto) return s;
    return autoResult.valid ? autoResult.style : styleTechHouse;
}

bool WhaleAudioProcessor::isTargetLoudnessMode() const
{
    const int t = (int) pTarget->load();
    return t != sameLevelTarget && t != manualTarget;
}

WhaleDSP::Settings WhaleAudioProcessor::buildSettings() noexcept
{
    WhaleDSP::Settings s;
    const float dos = pDosage->load();
    auto m = [this] (int i) { return pM[i]->load() - 0.5f; };   // macro offsets, 0 = neutral
    const bool on[6] = { pOn[0]->load() > 0.5f, pOn[1]->load() > 0.5f, pOn[2]->load() > 0.5f, pOn[3]->load() > 0.5f, pOn[4]->load() > 0.5f, pOn[5]->load() > 0.5f };
    enum { mGlue, mPunch, mWarmth, mAir, mWidth, mBass, mDetail, mLoud };

    s.bypass = pBypass->load() > 0.5f;
    s.matchAB = pMatch->load() > 0.5f;
    if (on[0])
        for (int b = 0; b < 4; ++b)
        {
            s.specGainDb[b] = dos * pSpecG[b]->load() + (b == 2 ? m (mDetail) * 3.0f : 0.0f);
            s.specDyn[b] = dos * pSpecD[b]->load();
        }
    if (on[1])
    {
        s.glue = juce::jlimit (0.0f, 1.0f, dos * pGlue->load() + m (mGlue));
        s.punch = juce::jlimit (0.0f, 1.0f, dos * pPunch->load() + m (mPunch));
        s.detail = juce::jlimit (0.0f, 1.0f, pDetail->load() + 0.8f * m (mDetail));
    }
    if (on[2]) s.satDrive = juce::jlimit (0.0f, 1.0f, dos * pSat->load() + 0.8f * m (mWarmth));
    if (on[3]) { s.sideDb = dos * pSide->load() + 6.0f * m (mWidth); s.monoHz = pMono->load(); }
    if (on[4]) { s.lowDb = dos * pLow->load() + 6.0f * m (mBass); s.airDb = dos * pAir->load() + 6.0f * m (mAir); }
    if (on[5]) s.clip = juce::jlimit (0.0f, 1.0f, pClip->load() * (0.5f + 0.5f * dos));
    s.ceilingDb = pCeil->load();

    const int t = (int) pTarget->load();
    if (t == manualTarget)
        s.driveDb = pManual->load() + 6.0f * m (mLoud);
    else if (t == sameLevelTarget)
        s.driveDb = 6.0f * m (mLoud);
    else
    {
        s.autoLoudness = true;
        s.targetLufs = effectiveTarget.load() + 6.0f * m (mLoud);
        s.driveDb = driveFF.load() + 6.0f * m (mLoud);
    }
    return s;
}

void WhaleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    const int nc = buffer.getNumChannels();
    if (nc == 0) return;
    inAnalyzer.process (buffer.getReadPointer (0), nc > 1 ? buffer.getReadPointer (1) : nullptr, buffer.getNumSamples());
    dsp.process (buffer, buildSettings());
    outAnalyzer.process (buffer.getReadPointer (0), nc > 1 ? buffer.getReadPointer (1) : nullptr, buffer.getNumSamples());
}

// ------------------------------------------------------------------------------------------------- AUTO

ModuleBase WhaleAudioProcessor::readBase() const
{
    ModuleBase b;
    for (int k = 0; k < 4; ++k) { b.specGain[k] = pSpecG[k]->load(); b.specDyn[k] = pSpecD[k]->load(); }
    b.compGlue = pGlue->load(); b.compPunch = pPunch->load(); b.compDetail = pDetail->load(); b.satDrive = pSat->load();
    b.widthSide = pSide->load(); b.widthMono = pMono->load(); b.toneLow = pLow->load(); b.toneAir = pAir->load(); b.clipAmt = pClip->load();
    return b;
}

void WhaleAudioProcessor::writeBase (const ModuleBase& b)
{
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = state.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    for (int k = 0; k < 4; ++k) { set (PID::specGain[k], b.specGain[k]); set (PID::specDyn[k], b.specDyn[k]); }
    set (PID::compGlue, b.compGlue); set (PID::compPunch, b.compPunch); set (PID::compDetail, b.compDetail);
    set (PID::satDrive, b.satDrive); set (PID::widthSide, b.widthSide); set (PID::widthMono, b.widthMono);
    set (PID::toneLow, b.toneLow); set (PID::toneAir, b.toneAir); set (PID::clipAmt, b.clipAmt);
}

void WhaleAudioProcessor::toggleAuto()
{
    if (autoState == AutoState::listening)
    {
        autoState = AutoState::idle;
        return;
    }
    if (autoState == AutoState::verifying)
    {
        writeBase (beforeAuto);   // cancel: back to the settings before AUTO
        autoState = AutoState::idle;
        return;
    }
    autoSession.clear();
    envLow.clear(); envHigh.clear();
    autoState = AutoState::listening;
    waitingForAudio = true;
    souffleArmed = false;
}

float WhaleAudioProcessor::getAutoProgress() const
{
    switch (autoState)
    {
        case AutoState::listening: return (float) juce::jlimit (0.0, 1.0, autoSession.getDuration() / listenSeconds) * 0.75f;
        case AutoState::verifying: return 0.75f + 0.25f * (float) juce::jlimit (0.0, 1.0, verifySeconds / verifyDuration);
        case AutoState::done:      return 1.0f;
        case AutoState::idle:      break;
    }
    return 0.0f;
}

void WhaleAudioProcessor::applyPreset (int index)
{
    const auto& all = presets.getAll();
    if (! juce::isPositiveAndBelow (index, all.size())) return;
    presets.apply (all[index], state);
    presets.setCurrent (index);
    autoState = AutoState::idle;
    haveAutoTarget = false;
}

void WhaleAudioProcessor::resetToNeutral()
{
    writeBase (ModuleBase());
    autoState = AutoState::idle;
    haveAutoTarget = false;
    presets.setCurrent (-1);
}

float WhaleAudioProcessor::getLoudnessError() const
{
    return dsp.getOutputLoudness() - (effectiveTarget.load() + 6.0f * (pM[7]->load() - 0.5f));
}

juce::String WhaleAudioProcessor::getVerdictText() const
{
    switch (verdict)
    {
        case Verdict::idle:      return fr ("EN ATTENTE DU SON");
        case Verdict::ok:        return fr ("PRÊT");
        case Verdict::working:   return autoState == AutoState::listening ? (waitingForAudio ? fr ("LANCE LA LECTURE (LE DROP)") : fr ("ÉCOUTE EN COURS")) : fr ("MASTERING EN COURS");
        case Verdict::validated: return fr ("MASTER VALIDÉ");
        case Verdict::tooLoud:   return fr ("TROP FORT");
        case Verdict::tooSoft:   return fr ("SOUS LA CIBLE");
        case Verdict::clipping:  return fr ("SATURE");
        case Verdict::phase:     return fr ("PROBLÈME DE PHASE");
    }
    return {};
}

void WhaleAudioProcessor::timerCallback()
{
    const double dt = 1.0 / 30.0;

    // Pull analysis frames (100 ms each).
    {
        AnalysisFrame buf[64];
        int got;
        while ((got = inAnalyzer.readFrames (buf, 64)) > 0)
            for (int i = 0; i < got; ++i)
            {
                if (buf[i].lufs() > AnalysisSession::silenceLufs) recentIn.push_back (buf[i]);
                if (autoState == AutoState::listening) autoSession.add (buf[i]);
            }
        while ((got = outAnalyzer.readFrames (buf, 64)) > 0)
            for (int i = 0; i < got; ++i)
                if (buf[i].lufs() > AnalysisSession::silenceLufs) recentOut.push_back (buf[i]);
        float lo[512], hi[512];
        while ((got = inAnalyzer.readEnvelopes (lo, hi, 512)) > 0)
            if (autoState == AutoState::listening && dsp.getInputLoudness() > -50.0f && envLow.size() < 20000)
            {
                envLow.insert (envLow.end(), lo, lo + got);
                envHigh.insert (envHigh.end(), hi, hi + got);
            }
        if (recentIn.size() > 300) recentIn.erase (recentIn.begin(), recentIn.begin() + (long) (recentIn.size() - 300));
        if (recentOut.size() > 1200) recentOut.erase (recentOut.begin(), recentOut.begin() + (long) (recentOut.size() - 1200));
    }

    // Loud-passage loudness of the input (90th percentile of 3 s short-term over the last 30 s).
    if (recentIn.size() > 30)
    {
        auto st = AnalysisSession::shortTermLoudness (recentIn);
        std::sort (st.begin(), st.end());
        inputLoudPassage = st[(size_t) (0.9 * (double) (st.size() - 1))];
    }
    if (! recentOut.empty())
    {
        double sp = 0, sw = 0;
        for (auto& f : recentOut) { sp += f.kPower * f.seconds; sw += f.seconds; }
        const float ungated = powerToLufs (sp / std::max (1.0e-9, sw));
        sp = sw = 0;
        for (auto& f : recentOut) if (f.lufs() > ungated - 10.0f) { sp += f.kPower * f.seconds; sw += f.seconds; }
        outIntegrated = sw > 0 ? powerToLufs (sp / sw) : ungated;
    }

    // Effective loud-passage target
    {
        const int t = (int) pTarget->load();
        float target = -9.0f;
        if (t == styleTarget || t == manualTarget || t == sameLevelTarget)
            target = haveAutoTarget ? autoLoudTarget : AutoMaster::profile (getActiveStyle()).st_drop.median;
        else if (t > 0 && t < sameLevelTarget)
            target = loudnessTargets[t].lufs + 1.0f;
        effectiveTarget.store (target);
        // Feed-forward drive: what the input needs to reach the target; the DSP's closed loop trims the rest.
        if (inputLoudPassage > -50.0f)
        {
            const float want = juce::jlimit (-6.0f, 24.0f, target - inputLoudPassage);
            driveFF.store (driveFF.load() + 0.05f * (want - driveFF.load()));
        }
    }

    // AUTO state machine
    if (autoState == AutoState::listening)
    {
        waitingForAudio = dsp.getInputLoudness() < -50.0f;
        if (autoSession.getDuration() >= listenSeconds)
        {
            const auto features = autoSession.computeFeatures();
            double bpm = hostBpm.load();
            if (bpm <= 0) bpm = AutoMaster::estimateTempo (envLow, envHigh, AudioAnalyzer::envelopeRate);
            autoResult = AutoMaster::compute (features, (int) pStyle->load(), bpm, (int) pTarget->load(), pCeil->load());
            if (autoResult.valid)
            {
                beforeAuto = readBase();
                writeBase (autoResult.base);
                autoLoudTarget = autoResult.loudPassageTarget;
                haveAutoTarget = true;
                for (auto* id : { PID::mGlue, PID::mPunch, PID::mWarmth, PID::mAir, PID::mWidth, PID::mBass, PID::mDetail, PID::mLoud })
                    if (auto* prm = state.getParameter (id)) prm->setValueNotifyingHost (0.5f);
                autoState = AutoState::verifying;
                verifySeconds = 0.0; grAcc = 0.0; grCount = 0;
                presets.setCurrent (-1);
            }
            else autoState = AutoState::idle;
        }
    }
    else if (autoState == AutoState::verifying)
    {
        if (dsp.getInputLoudness() > -50.0f)
        {
            verifySeconds += dt;
            grAcc += dsp.getGainReductionDb(); ++grCount;
        }
        if (verifySeconds >= verifyDuration)
        {
            // Verification: the limiter should not be doing all the work (punch).
            const float gr = grCount > 0 ? (float) (grAcc / grCount) : 0.0f;
            if (gr > 5.0f && (int) pTarget->load() == styleTarget)
            {
                auto b = readBase();
                if (b.clipAmt < 0.8f)
                {
                    b.clipAmt = std::min (0.8f, b.clipAmt + 0.2f);
                    writeBase (b);
                    autoResult.report.add (fr ("Vérification : le limiteur travaillait trop (") + juce::String (gr, 1) + fr (" dB) → clipper ")
                                           + juce::String (juce::roundToInt (b.clipAmt * 100)) + " %");
                }
                if (gr > 8.0f)
                {
                    autoLoudTarget -= 1.0f;
                    autoResult.report.add (fr ("Vérification : cible baissée de 1 dB pour garder le punch (") + juce::String (autoLoudTarget, 1) + " LUFS)");
                }
            }
            else
                autoResult.report.add (fr ("Vérification après traitement : OK (limiteur ") + juce::String (gr, 1) + " dB en moyenne)");
            autoState = AutoState::done;
            souffleArmed = true;
        }
    }

    // Verdict for the whale
    {
        Verdict v = Verdict::ok;
        const bool sound = dsp.getInputLoudness() > -50.0f;
        const float err = getLoudnessError();
        if (! sound) v = Verdict::idle;
        else if (autoState == AutoState::listening || autoState == AutoState::verifying) v = Verdict::working;
        else if (dsp.getCorrelation() < -0.05f) v = Verdict::phase;
        else if (dsp.getClipDb() > 4.0f || dsp.getGainReductionDb() > 10.0f) v = Verdict::clipping;
        else if ((int) pTarget->load() != sameLevelTarget && err > 2.0f) v = Verdict::tooLoud;
        else if (isTargetLoudnessMode() && err < -4.0f) v = Verdict::tooSoft;
        else if (autoState == AutoState::done && std::abs (err) < 1.5f) v = Verdict::validated;
        // hold problem verdicts a little so the scene does not flicker
        if (v != verdict)
        {
            verdictHold += dt;
            if (verdictHold > 0.6 || v == Verdict::working) { verdict = v; verdictHold = 0.0; }
        }
        else verdictHold = 0.0;
        if (verdict == Verdict::validated && souffleArmed) { ++souffleCount; souffleArmed = false; }
    }
}

// ------------------------------------------------------------------------------------------------- state

void WhaleAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = state.copyState();
    st.setProperty ("whaleVersion", WHALE_VERSION_STRING, nullptr);
    st.setProperty ("autoDone", autoState == AutoState::done, nullptr);
    st.setProperty ("autoTarget", haveAutoTarget ? autoLoudTarget : 0.0f, nullptr);
    st.setProperty ("autoStyle", autoResult.valid ? autoResult.style : 0, nullptr);
    st.setProperty ("autoReport", autoResult.report.joinIntoString ("\n"), nullptr);
    st.setProperty ("autoNotes", autoResult.mixNotes.joinIntoString ("\n"), nullptr);
    st.setProperty ("preset", presets.getCurrentName(), nullptr);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, dest);
}

void WhaleAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType())) return;
    auto st = juce::ValueTree::fromXml (*xml);
    state.replaceState (st);
    const float t = (float) st.getProperty ("autoTarget", 0.0f);
    haveAutoTarget = t < -0.5f;
    if (haveAutoTarget) autoLoudTarget = t;
    const int s = (int) st.getProperty ("autoStyle", 0);
    if (s > 0)
    {
        autoResult.valid = true;
        autoResult.style = s;
        autoResult.report = juce::StringArray::fromLines (st.getProperty ("autoReport").toString());
        autoResult.mixNotes = juce::StringArray::fromLines (st.getProperty ("autoNotes").toString());
        autoResult.mixNotes.removeEmptyStrings();
    }
    autoState = (bool) st.getProperty ("autoDone", false) ? AutoState::done : AutoState::idle;
    const auto name = st.getProperty ("preset").toString();
    presets.setCurrent (-1);
    for (int i = 0; i < presets.getAll().size(); ++i)
        if (presets.getAll()[i].name == name) presets.setCurrent (i);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WhaleAudioProcessor(); }
