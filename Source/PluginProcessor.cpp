#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Compose/MusicAnalyzer.h"

FINISHAudioProcessor::FINISHAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS", createParameterLayout())
{
    pAmount = params.getRawParameterValue (ParamIDs::amount);
    pImpact = params.getRawParameterValue (ParamIDs::impact);
    pPunch  = params.getRawParameterValue (ParamIDs::punch);
    pWidth  = params.getRawParameterValue (ParamIDs::width);
    pWarmth = params.getRawParameterValue (ParamIDs::warmth);
    pLoud   = params.getRawParameterValue (ParamIDs::loudness);
    pBypass = params.getRawParameterValue (ParamIDs::bypass);
    pMatch  = params.getRawParameterValue (ParamIDs::match);
    pCoherence = params.getRawParameterValue (ParamIDs::coherence);
    pTarget = params.getRawParameterValue (ParamIDs::target);
    pCeiling = params.getRawParameterValue (ParamIDs::ceiling);
    pProfile = params.getRawParameterValue (ParamIDs::masterProfile);
    pIntensity = params.getRawParameterValue (ParamIDs::masterIntensity);
    outAnalyzer.setPublishSamples (false);

    sampleRing.assign ((size_t) fftSize, 0.0f);
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    scratch.assign (8192, 0.0f);
    frameScratch.resize (512);
    spectrum.fill (-100.0f);

    rebuildDecisions();
    startTimerHz (30);
}

FINISHAudioProcessor::~FINISHAudioProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 4000);
}

bool FINISHAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void FINISHAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    analyzer.prepare (sampleRate);
    outAnalyzer.prepare (sampleRate);
    dsp.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    layerSmoothed = {};
    keepSmoothed = 0;
    capture.prepare (sampleRate);
    preview.prepare (sampleRate, samplesPerBlock);
    previewSeqVersion = -1;
    setLatencySamples (dsp.getLatencySamples());
}

MacroSettings FINISHAudioProcessor::getCurrentMacros() const
{
    MacroSettings m;
    m.amount = pAmount->load(); m.impact = pImpact->load(); m.punch = pPunch->load();
    m.width = pWidth->load();   m.warmth = pWarmth->load(); m.loudness = pLoud->load();
    return m;
}

void FINISHAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (buffer.getNumChannels() == 0 || n == 0) return;

    bool playing = false, timeValid = false;
    double songTime = 0.0, bpm = 120.0, ppq = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSeconds()) { songTime = *t; timeValid = true; }
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    hostBpm.store (bpm);
    hostPlaying.store (playing);
    hostTimeValid.store (timeValid);
    if (timeValid) hostTime.store (songTime);

    // Diagnose the incoming (unprocessed) track, stamped with the song position while playing.
    analyzer.process (buffer.getReadPointer (0),
                      buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr,
                      n, playing && timeValid ? songTime : -1.0);
    // What was played, on the song timeline, for the musical analysis (key, groove, melody...).
    capture.push (buffer.getReadPointer (0), buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr,
                  n, timeValid ? songTime : -1.0, ppq, bpm, playing);

    // Preview of generated parts: replaces (solo) or joins the input, then goes through the master chain.
    const bool previewing = preview.isPlaying();
    previewingNow.store (previewing);
    if (previewing && previewSolo.load()) buffer.clear();
    preview.render (buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr, n, previewBpm.load());

    // FINISH MY TRACK: pick up a new section plan if one is waiting (never blocks).
    if (planVersion.load() != audioPlanVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (planLock);
        if (tl.isLocked()) { audioPlan = pendingPlan; audioPlanVersion = planVersion.load(); }
    }

    SectionPlan::Layer target;
    if (previewing && previewSolo.load()) {}      // the preview is not the song: no section moves on it
    else if (timeValid && audioPlan.count > 0)
        target = audioPlan.evaluate (songTime + 0.5 * n / currentSampleRate.load(), pCoherence->load());
    else if (audioPlan.count > 0)
        target.delta = audioPlan.mean.scaled (0.6f * pCoherence->load());

    // Block-rate smoothing on top of the DSP's per-sample smoothing: section changes glide, never click.
    const float a = std::exp (-(float) n / (float) (currentSampleRate.load() * 0.06));
    layerSmoothed = SectionMacros::lerp (target.delta, layerSmoothed, a);
    keepSmoothed = target.keepOriginal + (keepSmoothed - target.keepOriginal) * a;
    liveImpact = layerSmoothed.impact; livePunch = layerSmoothed.punch; liveWidth = layerSmoothed.width;
    liveWarmth = layerSmoothed.warmth; liveLoud = layerSmoothed.loudness; liveTrim = layerSmoothed.trimDb;
    liveFx.store (layerSmoothed.hp + layerSmoothed.lp + layerSmoothed.gate + layerSmoothed.echo + layerSmoothed.space);

    FinishDSP::Settings s;
    s.macros = getCurrentMacros();
    s.macros.impact   = juce::jlimit (0.0f, 1.0f, s.macros.impact   + layerSmoothed.impact);
    s.macros.punch    = juce::jlimit (0.0f, 1.0f, s.macros.punch    + layerSmoothed.punch);
    s.macros.width    = juce::jlimit (0.0f, 1.0f, s.macros.width    + layerSmoothed.width);
    s.macros.warmth   = juce::jlimit (0.0f, 1.0f, s.macros.warmth   + layerSmoothed.warmth);
    s.macros.loudness = juce::jlimit (0.0f, 1.0f, s.macros.loudness + layerSmoothed.loudness);
    s.trimDb = layerSmoothed.trimDb;
    s.sectionDry = keepSmoothed;
    s.fx = { layerSmoothed.hp, layerSmoothed.lp, layerSmoothed.gate, layerSmoothed.echo, layerSmoothed.space };
    s.bpm = bpm;
    s.ppq = ppq;
    s.bypass = pBypass->load() > 0.5f;
    s.matchAB = pMatch->load() > 0.5f;
    s.ceilingDb = pCeiling->load();
    // Mastering drive: from the loudness target when one is chosen, else the manual LOUDNESS knob.
    const int targetIdx = getTargetIndex();
    s.driveDb = targetIdx > 0 ? autoDriveDb.load() : s.macros.loudness * s.macros.amount * 10.0f;
    s.targetLufs = getTargetLufs();
    s.autoLoudness = targetIdx > 0 && s.targetLufs > -60.0f;
    // Mastering stage: corrections from the mix analysis (MasterDoctor), only when a target is chosen.
    const bool mastering = targetIdx > 0;
    s.glue = mastering ? autoGlue.load() : 0.0f;
    s.clip = mastering ? autoClip.load() : 0.0f;
    s.eqLowDb = mastering ? autoEqLow.load() : 0.0f;
    s.eqHighDb = mastering ? autoEqHigh.load() : 0.0f;
    s.eqMudDb = mastering ? autoMud.load() : 0.0f;
    s.eqPresDb = mastering ? autoPres.load() : 0.0f;
    s.sideDb = mastering ? autoSide.load() : 0.0f;
    s.monoBass = mastering ? autoMono.load() : 0.0f;
    s.punchKeep = mastering ? autoPunch.load() : 0.0f;
    dsp.process (buffer, s);
    // What leaves FINISH, measured like what came in (not during a preview: that is not the song).
    if (! previewing)
        outAnalyzer.process (buffer.getReadPointer (0), buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr,
                             n, playing && timeValid ? songTime : -1.0);
}

SectionMacros FINISHAudioProcessor::getLiveSectionLayer() const noexcept
{
    return { liveImpact.load(), livePunch.load(), liveWidth.load(), liveWarmth.load(), liveLoud.load(), liveTrim.load() };
}

int FINISHAudioProcessor::getTargetIndex() const noexcept
{
    return juce::jlimit (0, numLoudnessTargets - 1, (int) pTarget->load());
}

float FINISHAudioProcessor::getTargetLufs() const noexcept
{
    const int t = getTargetIndex();
    if (t == sameLevelTarget) return sameLevelLufs.load();
    return loudnessTargets[t].lufs;
}

float FINISHAudioProcessor::getSongLoudness() const noexcept
{
    return features.valid ? features.integratedLufs : liveSongLufs;
}

// Open-loop drive into the limiter: reach the target integrated loudness from the song's measured loudness.
// Below the limiter threshold every dB of drive is a dB of loudness; above it the limiter absorbs part of it.
float FINISHAudioProcessor::computeDriveFor (float targetLufs) const noexcept
{
    const float song = getSongLoudness();
    if (song < -60.0f) return 0.0f;
    const float ceiling = pCeiling->load();
    const float peak = features.valid ? features.peakDb : song + 12.0f;
    const float wanted = targetLufs - song;
    const float headroom = ceiling - peak;
    float drive = wanted <= headroom ? wanted : headroom + (wanted - headroom) / limiterEfficiency;
    return juce::jlimit (-12.0f, 30.0f, drive);
}

void FINISHAudioProcessor::updateAutoDrive()
{
    // Long-term input loudness while playing (used before an analysis exists).
    const float in = dsp.getInputLoudness();
    if (hostPlaying.load() && in > -50.0f)
        liveSongLufs = liveSongLufs < -60.0f ? in : liveSongLufs + (in - liveSongLufs) / 600.0f;
    const int t = getTargetIndex();
    autoDriveDb.store (t > 0 && getTargetLufs() > -60.0f ? computeDriveFor (getTargetLufs()) : 0.0f);
}

juce::AudioProcessorParameter* FINISHAudioProcessor::getBypassParameter() const
{
    return params.getParameter (ParamIDs::bypass);
}

// ======================================================================================
// Message thread

void FINISHAudioProcessor::timerCallback()
{
    // Drain analysis frames even when not listening, so the FIFO never stalls.
    for (;;)
    {
        const int got = analyzer.readFrames (frameScratch.data(), (int) frameScratch.size());
        if (got == 0) break;
        for (int i = 0; i < got; ++i)
        {
            const auto& f = frameScratch[(size_t) i];
            if (analysisState == AnalysisState::Listening) session.add (f);
            if (f.hostTime >= 0.0 && hostPlaying.load() && ! previewingNow.load())
            {
                masterInFrames.push_back (f);
                while (masterInFrames.size() > 600) masterInFrames.pop_front();
            }
            // Always remember the last stretch played: FINISH MY TRACK can use it at once.
            if (f.hostTime >= 0.0)
            {
                if (recentLastTime >= 0.0 && (f.hostTime < recentLastTime - 0.05 || f.hostTime > recentLastTime + 0.6))
                    recent.clear();
                recent.add (f);
                recentLastTime = f.hostTime + f.seconds;
            }
        }
    }

    for (;;)
    {
        const int got = outAnalyzer.readFrames (frameScratch.data(), (int) frameScratch.size());
        if (got == 0) break;
        for (int i = 0; i < got; ++i)
        {
            const auto& f = frameScratch[(size_t) i];
            if (f.hostTime >= 0.0 && hostPlaying.load() && ! previewingNow.load())
            {
                masterOutFrames.push_back (f);
                while (masterOutFrames.size() > 600) masterOutFrames.pop_front();
            }
        }
    }
    {
        const float pk = dsp.takeMaxPeakDb();
        if (hostPlaying.load() && ! previewingNow.load()) tpHistory.push_back (pk);
        while (tpHistory.size() > 1800) tpHistory.pop_front();
    }
    updateMaster (false);

    // The user's Live project
    if (project.tick())
    {
        if (project.takeFreshPlan())
        {
            // Fresh read: show what FINISH understood and what it plans (or answer the request that triggered the read).
            if (pendingProjectCommand == "__sketch__")
            {
                pendingProjectCommand.clear();
                sendArrangementToLive();
            }
            else if (pendingProjectCommand.isNotEmpty())
            {
                const auto cmd = pendingProjectCommand;
                pendingProjectCommand.clear();
                handleProjectCommand (cmd);
            }
            else
                setAnswer (ProjectAnalyzer::report (project.getAnalysis(), project.getProject()) + "\n" + project.planText());
            if (project.getOptions().level == Level::Finalise || (int) pTarget->load() == 0) applyRecommendedTarget();
            pageRequest = 0;
        }
        if (project.takeLiveResult()) setAnswer (project.getLastReport());
        statusMessage = project.getStatus();
        ++resultVersion;
    }

    if (analysisState == AnalysisState::Listening)
    {
        const bool playing = hostPlaying.load();
        if (playing) sawPlayback = true;
        // Auto-finish when the user stops the transport after playing the track.
        if ((sawPlayback && ! playing && session.getDuration() >= 8.0) || session.getFrames().size() >= AnalysisSession::maxFrames)
            finishAnalysis();
    }

    // Musical analysis finished on the worker?
    {
        ComposeJobResult r;
        {
            const juce::ScopedLock sl (composeLock);
            if (composeResult.ready) { r = composeResult; composeResult = {}; }
        }
        if (r.ready)
        {
            composing = false;
            if (r.dna.valid)
            {
                composer.setDNA (r.dna, r.userBar, (juce::uint32) juce::Random::getSystemRandom().nextInt (1 << 30) + 1u);
                afterCompose();
            }
            else
            {
                statusMessage = utf8 ("Pas assez de musique pour composer : joue au moins 4 mesures (un drop, une boucle ou une mélodie).");
                ++resultVersion;
            }
        }
    }
    if (composer.pollLive())
    {
        statusMessage = composer.getLiveStatus();
        ++resultVersion;
    }
    refreshPreviewSequence();

    updateSpectrum();
    updateAutoDrive();

    // Reference analysis finished on the worker thread?
    std::unique_ptr<ReferenceProfile> ready;
    {
        const juce::ScopedLock sl (pendingLock);
        ready = std::move (pendingReference);
    }
    if (ready != nullptr)
    {
        referenceLoading = false;
        if (ready->valid) { reference = *ready; statusMessage = "Reference loaded: " + reference.name; }
        else              { statusMessage = "Could not read reference file " + ready->name; }
        rebuildDecisions();
    }
}

void FINISHAudioProcessor::updateSpectrum()
{
    for (;;)
    {
        const int got = analyzer.readSamples (scratch.data(), (int) scratch.size());
        if (got == 0) break;
        for (int i = 0; i < got; ++i)
        {
            sampleRing[(size_t) ringPos] = scratch[(size_t) i];
            ringPos = (ringPos + 1) % fftSize;
        }
    }
    if (getActiveEditor() == nullptr) return;

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = sampleRing[(size_t) ((ringPos + i) % fftSize)];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double sr = currentSampleRate.load();
    const float norm = 4.0f / (float) fftSize;
    for (int b = 0; b < spectrumBins; ++b)
    {
        const double f0 = 30.0 * std::pow (18000.0 / 30.0, (double) b / spectrumBins);
        const double f1 = 30.0 * std::pow (18000.0 / 30.0, (double) (b + 1) / spectrumBins);
        int k0 = (int) std::floor (f0 / sr * fftSize), k1 = (int) std::ceil (f1 / sr * fftSize);
        k0 = juce::jlimit (1, fftSize / 2 - 1, k0); k1 = juce::jlimit (k0 + 1, fftSize / 2, k1);
        float mag = 0;
        for (int k = k0; k < k1; ++k) mag = std::max (mag, fftData[(size_t) k]);
        // +3 dB/octave tilt so a balanced mix reads roughly flat.
        const float db = 20.0f * std::log10 (std::max (mag * norm, 1.0e-7f)) + 3.0f * (float) std::log2 (std::sqrt (f0 * f1) / 1000.0);
        spectrum[(size_t) b] = std::max (db, spectrum[(size_t) b] - 1.2f);
    }
}

void FINISHAudioProcessor::toggleAnalysis()
{
    if (analysisState == AnalysisState::Listening)
    {
        finishAnalysis();
        return;
    }
    session.clear();
    sawPlayback = hostPlaying.load();
    analysisState = AnalysisState::Listening;
    statusMessage = "Listening. Play your track, then stop playback (or click STOP & MAP) to get the track map.";
    ++resultVersion;
}

void FINISHAudioProcessor::cancelAnalysis()
{
    if (analysisState != AnalysisState::Listening) return;
    session.clear();
    analysisState = features.valid ? AnalysisState::Ready : AnalysisState::Idle;
    statusMessage = {};
    ++resultVersion;
}

void FINISHAudioProcessor::finishAnalysis()
{
    finishAnalysisFrom (session);
    session.clear();
}

void FINISHAudioProcessor::finishAnalysisFrom (const AnalysisSession& source)
{
    if (source.getDuration() < 3.0)
    {
        analysisState = features.valid ? AnalysisState::Ready : AnalysisState::Idle;
        statusMessage = utf8 ("Pas assez d'audio : joue au moins quelques mesures pendant que FINISH écoute.");
        composeAfterwards = false;
        ++resultVersion;
        return;
    }
    const auto frames = source.getFrames();
    features = AnalysisSession::computeFeatures (frames);
    track.build (frames);
    analysisState = AnalysisState::Ready;
    appliedStrategy = {};
    const auto& m = track.getMusic();
    statusMessage = utf8 ("Carte prête : ") + juce::String ((int) m.sections.size()) + " sections, "
                  + juce::String (juce::roundToInt (features.durationSec)) + utf8 (" s analysées.");
    rebuildDecisions();

    if (! composeAfterwards) return;
    composeAfterwards = false;

    // 1. Mastering on when the user had none: the target the mix analysis recommends (never louder by default).
    if ((int) pTarget->load() == 0) applyRecommendedTarget();

    // 2. Section moves FINISH is confident about (only on a real song with several parts).
    if (m.sections.size() >= 3)
        editTrack ([] (TrackEngine& e)
        {
            const auto& v = e.getVerdicts();
            for (int i = 0; i < (int) v.size(); ++i)
                if (v[(size_t) i].potential >= 0.5f && e.variantsFor (TrackEngine::Selection::section (i)) != nullptr && e.editFor (TrackEngine::Selection::section (i)) == nullptr)
                    e.apply (TrackEngine::Selection::section (i), 0, 0.8f);
            for (auto& t : e.getTransitions())
                if (! t.works && t.severity >= 0.5f && e.variantsFor (TrackEngine::Selection::transition (t.index)) != nullptr)
                    e.apply (TrackEngine::Selection::transition (t.index), 0, 0.8f);
        });

    // 3. The music: analyse the strongest part (the drop if there is one) and write the missing parts.
    double from = frames.front().hostTime, to = frames.back().hostTime + frames.back().seconds;
    int best = -1; float bestLoud = -100.0f;
    for (int i = 0; i < (int) m.sections.size(); ++i)
        if (m.sections[(size_t) i].length() >= 8.0 && m.sections[(size_t) i].loudness > bestLoud && isPeakType (m.sections[(size_t) i].type))
            { bestLoud = m.sections[(size_t) i].loudness; best = i; }
    if (best >= 0) { from = m.sections[(size_t) best].startTime; to = m.sections[(size_t) best].endTime; }
    if (from >= 0.0) to = std::min (to, from + 120.0);
    startComposeAnalysis (from, to);
}

void FINISHAudioProcessor::startComposeAnalysis (double from, double to)
{
    auto snap = capture.snapshot (from, to);
    if (! snap.valid())
    {
        statusMessage = utf8 ("FINISH n'a rien enregistré à composer : lance la lecture dans ton DAW, puis FINISH MY TRACK.");
        ++resultVersion;
        return;
    }
    composing = true;
    const int userBar = std::max (0, (int) std::ceil (snap.beatAtStart / 4.0 - 1.0e-6));
    statusMessage = utf8 ("Écoute de ta musique : tempo, tonalité, groove, basse, mélodie...");
    ++resultVersion;
    pool.addJob ([this, snap = std::move (snap), userBar]
    {
        auto dna = MusicAnalyzer::analyze (snap);
        const juce::ScopedLock sl (composeLock);
        composeResult.dna = std::move (dna);
        composeResult.userBar = userBar;
        composeResult.ready = true;
    });
}

void FINISHAudioProcessor::afterCompose()
{
    const auto& d = composer.getDNA();
    const auto& a = composer.getArrangement();
    statusMessage = juce::String (styleName (d.style)) + "  " + d.keyName() + "  " + juce::String (d.bpm, 1) + " BPM  |  "
                  + juce::String ((int) a.sections.size()) + utf8 (" parties, ") + juce::String (a.totalBars()) + utf8 (" mesures. ")
                  + utf8 ("Écoute avec PLAY, envoie avec SEND TO LIVE.");
    const auto unsure = d.uncertainties();
    if (! unsure.isEmpty()) statusMessage << "  (" << unsure[0] << ")";
    previewSeqVersion = -1;
    ++resultVersion;
}

// ======================================================================================
// FINISH 3: the user's Live project

void FINISHAudioProcessor::readLiveProject()
{
    juce::String err;
    if (project.readProject (err)) statusMessage = project.getStatus();
    else
    {
        const auto& d = project.getDiagnosis();
        statusMessage = err;
        juce::String a = utf8 ("CONNEXION À LIVE : ") + d.title + "\n\n";
        for (int i = 0; i < d.steps.size(); ++i) a << juce::String (i + 1) << ". " << d.steps[i] << "\n";
        a << "\n" << LiveBridge::linkVersusControlSurface();
        a << utf8 ("\n\nSans Live (FL Studio...) : onglet ESQUISSE, FINISH écoute ton morceau et propose des parties en MIDI.");
        setAnswer (a);
    }
    pageRequest = 0;
    ++resultVersion;
}

void FINISHAudioProcessor::showProject (LiveProject p)
{
    project.loadProject (std::move (p));
    project.takeFreshPlan();
    setAnswer (ProjectAnalyzer::report (project.getAnalysis(), project.getProject()) + "\n" + project.planText());
    statusMessage = project.getStatus();
    pageRequest = 0;
    ++resultVersion;
}

void FINISHAudioProcessor::editProject (const std::function<void (ProjectSession&)>& change)
{
    change (project);
    statusMessage = project.getStatus();
    ++resultVersion;
}

void FINISHAudioProcessor::sendProjectToLive()
{
    stopPreview();
    juce::String err;
    if (project.send (currentSampleRate.load(), err)) statusMessage = project.getStatus();
    else statusMessage = err;
    ++resultVersion;
}

void FINISHAudioProcessor::listenInLive()
{
    stopPreview();
    juce::String err;
    if (project.sendAndListen (currentSampleRate.load(), err)) statusMessage = utf8 ("Écriture puis lecture dans Live (tes vrais sons)…");
    else statusMessage = err;
    ++resultVersion;
}

void FINISHAudioProcessor::stopEverything()
{
    stopPreview();
    juce::String err;
    project.stopLive (err);
    ++resultVersion;
}

void FINISHAudioProcessor::showSoundInventory()
{
    if (! project.hasProject()) { readLiveProject(); return; }
    setAnswer (ProjectAnalyzer::soundInventory (project.getAnalysis(), project.getProject()));
    pageRequest = 0;
    ++resultVersion;
}

void FINISHAudioProcessor::undoProjectSend()
{
    juce::String err;
    if (project.undoSend (err)) statusMessage = project.getStatus();
    else statusMessage = err;
    ++resultVersion;
}

void FINISHAudioProcessor::previewProject (int section, bool loop)
{
    if (! project.hasPlan()) return;
    previewSource = 1;
    projectPreviewVersion = -1;
    refreshPreviewSequence();
    const auto arr = project.previewArrangement();
    if (arr.sections.empty()) return;
    // Plan section index -> preview section index (the preview holds the new sections only)
    int idx = -1, k = 0;
    const auto& plan = project.getPlan();
    for (int i = 0; i < (int) plan.sections.size(); ++i)
    {
        if (plan.sections[(size_t) i].existing) continue;
        if (i == section) idx = k;
        ++k;
    }
    if (idx < 0)
    {
        previewSectionIndex = -1;
        preview.play (0.0, 0.0, loop ? arr.totalBars() * 4.0 : 0.0);
    }
    else
    {
        const auto& s = arr.sections[(size_t) idx];
        previewSectionIndex = section;
        const double a = s.startBar * 4.0, b = a + s.lengthBeats();
        preview.play (a, loop ? a : 0.0, loop ? b : 0.0);
    }
    ++resultVersion;
}

// ======================================================================================
// Mastering from the mix analysis

double FINISHAudioProcessor::getMasterSeconds() const noexcept
{
    double d = 0;
    for (auto& f : masterInFrames) d += f.seconds;
    return d;
}

void FINISHAudioProcessor::updateMaster (bool force)
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const int profile = juce::jlimit (0, numMasterProfiles - 1, (int) pProfile->load());
    const float intensity = pIntensity->load();
    const int target = getTargetIndex();
    const bool settingsChanged = profile != lastProfile || std::abs (intensity - lastIntensity) > 0.005f || target != lastTargetForPlan;
    if (! force && ! settingsChanged && now - lastMasterUpdate < 2.0) return;
    if (! force && ! settingsChanged && ! hostPlaying.load()) return;
    lastMasterUpdate = now;
    lastProfile = profile; lastIntensity = intensity; lastTargetForPlan = target;

    if (getMasterSeconds() >= 8.0)
    {
        masterInFeatures = AnalysisSession::computeFeatures (std::vector<AnalysisFrame> (masterInFrames.begin(), masterInFrames.end()));
        masterOutFeatures = AnalysisSession::computeFeatures (std::vector<AnalysisFrame> (masterOutFrames.begin(), masterOutFrames.end()));
        if (masterInFeatures.valid) sameLevelLufs.store (masterInFeatures.integratedLufs);
        masterOutTpMax = -100.0f;
        for (auto v : tpHistory) masterOutTpMax = std::max (masterOutTpMax, v);
    }
    MasterDoctor::Request r;
    r.in = masterInFeatures.valid ? masterInFeatures : features;
    if (! r.in.valid) return;
    if (! masterInFeatures.valid) sameLevelLufs.store (features.integratedLufs);
    r.reference = reference.valid ? &reference.features : nullptr;
    r.profile = (MasterDoctor::Profile) profile;
    r.intensity = intensity;
    r.keepDark = keepDark;
    r.keepBright = keepBright;
    r.targetIndex = target;
    r.bpm = hostBpm.load();
    const auto plan = MasterDoctor::design (r);
    // Move only for real changes (the analysis window slides while playing).
    auto moved = [] (float a, float b, float tol) { return std::abs (a - b) > tol; };
    const bool changed = ! masterPlan.valid || settingsChanged || force
        || moved (plan.eqLowDb, masterPlan.eqLowDb, 0.3f) || moved (plan.eqHighDb, masterPlan.eqHighDb, 0.3f)
        || moved (plan.eqMudDb, masterPlan.eqMudDb, 0.3f) || moved (plan.eqPresDb, masterPlan.eqPresDb, 0.3f)
        || moved (plan.sideDb, masterPlan.sideDb, 0.3f) || moved (plan.monoBass, masterPlan.monoBass, 0.5f)
        || moved (plan.glue, masterPlan.glue, 0.08f) || moved (plan.clip, masterPlan.clip, 0.08f) || moved (plan.punchKeep, masterPlan.punchKeep, 0.5f)
        || plan.findings.size() != masterPlan.findings.size();
    if (! changed) return;
    masterPlan = plan;
    autoEqLow.store (plan.eqLowDb);
    autoEqHigh.store (plan.eqHighDb);
    autoMud.store (plan.eqMudDb);
    autoPres.store (plan.eqPresDb);
    autoSide.store (plan.sideDb);
    autoMono.store (plan.monoBass);
    autoGlue.store (plan.glue);
    autoClip.store (plan.clip);
    autoPunch.store (plan.punchKeep);
    ++resultVersion;
}

juce::String FINISHAudioProcessor::getMasterReport() const
{
    const auto& in = masterInFeatures.valid ? masterInFeatures : features;
    const bool outOk = masterInFeatures.valid && masterOutFeatures.valid && getTargetIndex() > 0;
    return MasterDoctor::report (masterPlan, in, outOk ? &masterOutFeatures : nullptr, outOk ? masterOutTpMax : -100.0f,
                                 getTargetIndex(), getTargetLufs(), pIntensity->load(), (MasterDoctor::Profile) juce::jlimit (0, 3, (int) pProfile->load()));
}

void FINISHAudioProcessor::setMasterCharacter (bool dark, bool bright)
{
    keepDark = dark;
    keepBright = bright && ! dark;
    if (dark)
        if (auto* p = params.getParameter (ParamIDs::masterProfile))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) MasterDoctor::Profile::Warm));
            p->endChangeGesture();
        }
    updateMaster (true);
}

void FINISHAudioProcessor::applyRecommendedTarget()
{
    updateMaster (true);
    const int t = masterPlan.valid ? masterPlan.recommendedTarget : sameLevelTarget;
    if (auto* prm = params.getParameter (ParamIDs::target))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) t));
        prm->endChangeGesture();
    }
    lastTargetForPlan = -1;
    updateMaster (true);
    project.log (ProjectSession::Entry::Kind::Master, utf8 ("Mastering : cible ") + juce::String (loudnessTargets[t].name)
                 + (masterPlan.valid ? juce::String (". ") + masterPlan.targetWhy : juce::String()));
}

bool FINISHAudioProcessor::handleProjectCommand (const juce::String& text)
{
    const auto cmd = ProjectCommand::parse (text);
    if (cmd.isEmpty()) return false;
    using K = ProjectCommand::Kind;
    intentUnderstood = cmd.understood;

    if (cmd.kind == K::AnalyseMix)
    {
        updateMaster (true);
        const auto& in = masterInFeatures.valid ? masterInFeatures : features;
        setAnswer (MasterDoctor::explain (in, masterPlan, cmd.mixTopic) + "\n\n" + getMasterReport());
        project.log (ProjectSession::Entry::Kind::Master, utf8 ("Analyse du mix présentée."));
        statusMessage = in.valid ? utf8 ("Analyse du mix prête (") + juce::String (juce::roundToInt (in.durationSec)) + utf8 (" s écoutées).")
                                 : utf8 ("Lance la lecture : FINISH mesure ton mix pendant que tu joues.");
        pageRequest = 3;
        ++resultVersion;
        return true;
    }
    if (cmd.kind == K::Master)
    {
        setMasterCharacter (cmd.keepDark, cmd.keepBright);
        if (getTargetIndex() == 0) applyRecommendedTarget();
        setAnswer (getMasterReport());
        statusMessage = cmd.keepDark ? utf8 ("Mastering : caractère sombre conservé (profil CHAUD, aucun aigu ajouté).") : utf8 ("Mastering réglé d'après l'analyse de ton mix.");
        project.log (ProjectSession::Entry::Kind::Master, statusMessage);
        pageRequest = 3;
        ++resultVersion;
        return true;
    }

    project.refreshDiagnosis();
    const bool connected = project.getDiagnosis().connected();
    if (! project.hasProject())
    {
        if (! connected)
        {
            if (composer.hasDNA()) return false;      // sketch mode: the older composer handles it
            readLiveProject();                         // explains how to connect
            return true;
        }
        pendingProjectCommand = text;
        readLiveProject();
        statusMessage = utf8 ("Je lis d'abord ton projet dans Live…");
        return true;
    }
    if (cmd.kind == K::Send) { sendConfirmRequested = true; pageRequest = 0; ++resultVersion; return true; }
    if (cmd.kind == K::Listen) { listenConfirmRequested = true; pageRequest = 0; ++resultVersion; return true; }
    if (cmd.kind == K::UndoSend) { undoProjectSend(); return true; }

    const auto out = project.apply (cmd);
    if (out.needsProject) { pendingProjectCommand = text; readLiveProject(); return true; }
    if (out.wantsSend) sendConfirmRequested = true;
    if (out.wantsListen) listenConfirmRequested = true;
    if (out.wantsUndoSend) undoProjectSend();
    if (out.wantsMaster && getTargetIndex() == 0) applyRecommendedTarget();
    if (out.answer.isNotEmpty()) setAnswer (out.answer);
    statusMessage = out.answer.upToFirstOccurrenceOf ("\n", false, false);
    if (isPreviewingProject()) previewProject (getPreviewSection(), true);
    pageRequest = 0;
    ++resultVersion;
    return true;
}

void FINISHAudioProcessor::finishMyTrack()
{
    if (composing.load()) return;
    if (analysisState == AnalysisState::Listening)
    {
        composeAfterwards = true;
        finishAnalysis();
        return;
    }
    // Already played something? Use it right away.
    if (recent.getDuration() >= 6.0 && capture.getLastRunSeconds() >= 6.0)
    {
        composeAfterwards = true;
        const auto copy = recent;
        finishAnalysisFrom (copy);
        return;
    }
    composeAfterwards = true;
    toggleAnalysis();
    statusMessage = utf8 ("J'écoute : lance la lecture (ton drop, ta boucle ou ta mélodie, 8 mesures ou plus), puis stop.");
    ++resultVersion;
}

void FINISHAudioProcessor::editComposer (const std::function<void (Composer&)>& change)
{
    change (composer);
    ++resultVersion;
}

void FINISHAudioProcessor::refreshPreviewSequence()
{
    if (previewSource == 1)
    {
        if (project.getVersion() == projectPreviewVersion || ! project.hasPlan()) return;
        projectPreviewVersion = project.getVersion();
        const auto arr = project.previewArrangement();
        previewBpm.store (arr.bpm);
        preview.setSequence (PreviewSynth::compile (arr));
        return;
    }
    if (composer.getVersion() == previewSeqVersion) return;
    previewSeqVersion = composer.getVersion();
    if (! composer.hasArrangement()) return;
    previewBpm.store (composer.getDNA().bpm);
    preview.setSequence (PreviewSynth::compile (composer.getArrangement()));
}

void FINISHAudioProcessor::previewSection (int sectionIndex, bool loop)
{
    if (! composer.hasArrangement()) return;
    if (previewSource != 0) { previewSource = 0; previewSeqVersion = -1; }
    refreshPreviewSequence();
    const auto& arr = composer.getArrangement();
    if (sectionIndex < 0 || sectionIndex >= (int) arr.sections.size())
    {
        previewSectionIndex = -1;
        preview.play (0.0, 0.0, loop ? arr.totalBars() * 4.0 : 0.0);
        return;
    }
    const auto& s = arr.sections[(size_t) sectionIndex];
    previewSectionIndex = sectionIndex;
    const double a = s.startBar * 4.0, b = a + s.lengthBeats();
    preview.play (a, loop ? a : 0.0, loop ? b : 0.0);
}

void FINISHAudioProcessor::stopPreview()
{
    preview.stop();
    previewSectionIndex = -1;
}

ArrangementExport::Result FINISHAudioProcessor::exportArrangement (bool revealFolder)
{
    auto r = composer.exportAll (currentSampleRate.load());
    statusMessage = r.ok ? utf8 ("Exporté : ") + r.folder.getFullPathName() : r.error;
    if (r.ok && revealFolder) r.folder.revealToUser();
    ++resultVersion;
    return r;
}

void FINISHAudioProcessor::sendArrangementToLive()
{
    // The sketch goes onto the user's OWN tracks (their instruments), never on new tracks with substitute instruments.
    if (! composer.hasArrangement()) return;
    if (! project.hasProject())
    {
        project.refreshDiagnosis();
        if (project.getDiagnosis().connected())
        {
            pendingProjectCommand = "__sketch__";
            readLiveProject();
            statusMessage = utf8 ("Je lis ton projet pour envoyer l'esquisse sur TES pistes…");
        }
        else
        {
            statusMessage = utf8 ("Live n'est pas connecté : FINISH n'envoie l'esquisse que sur tes propres pistes (pas d'instrument de remplacement). "
                                  "Connecte Live (page PROJET) ou utilise EXPORT / glisser-déposer.");
            readLiveProject();
        }
        ++resultVersion;
        return;
    }
    juce::String err;
    const auto& d = composer.getDNA();
    if (project.loadSketchPlan (composer.getArrangement(), d.keyRoot, d.minor, err))
    {
        setAnswer (project.planText());
        pageRequest = 0;
        sendConfirmRequested = true;
        statusMessage = utf8 ("Esquisse routée sur tes pistes : vérifie, puis confirme l'envoi.");
    }
    else statusMessage = err;
    ++resultVersion;
}

juce::File FINISHAudioProcessor::exportClipForDrag (int section, Lane lane)
{
    if (! composer.hasArrangement()) return {};
    const auto dir = ArrangementExport::defaultRoot().getChildFile ("Drag");
    const auto& arr = composer.getArrangement();
    if (lane == Lane::Fx) return ArrangementExport::writeSectionFx (arr, section, dir, currentSampleRate.load());
    return ArrangementExport::writeClipMidi (arr, section, lane, dir);
}

void FINISHAudioProcessor::rebuildDecisions()
{
    // Tonal balance for the mastering stage: towards the reference when there is one, else towards a sane range.
    {
        float lo = 0, hi = 0;
        if (features.valid)
        {
            auto ratioDb = [] (float a, float b) { return 10.0f * std::log10 (std::max (a, 1.0e-6f) / std::max (b, 1.0e-6f)); };
            if (reference.valid)
            {
                lo = juce::jlimit (-2.5f, 2.5f, 0.5f * ratioDb (reference.features.lowRatio, features.lowRatio));
                hi = juce::jlimit (-2.5f, 2.5f, 0.5f * ratioDb (reference.features.highRatio, features.highRatio));
            }
            else
            {
                if (features.lowRatio > 0.72f) lo = -std::min (2.5f, (features.lowRatio - 0.72f) * 12.0f);
                else if (features.lowRatio < 0.30f) lo = std::min (2.5f, (0.30f - features.lowRatio) * 10.0f);
                if (features.highRatio > 0.15f) hi = -std::min (2.5f, (features.highRatio - 0.15f) * 20.0f);
                else if (features.highRatio < 0.03f) hi = std::min (2.5f, (0.03f - features.highRatio) * 80.0f);
            }
        }
        autoEqLow.store (lo);
        autoEqHigh.store (hi);
    }
    diagnoses = diagnosisEngine.analyze (features, track.getMusic(), reference);
    decision = decisionEngine.build (diagnoses, intent, features.valid);
    score = features.valid ? DiagnosisEngine::finishScore (diagnoses) : 0;
    track.setContext (diagnoses, intent);
    publishPlan();
    ++resultVersion;
}

void FINISHAudioProcessor::publishPlan()
{
    const auto plan = track.compilePlan();
    {
        const juce::SpinLock::ScopedLockType sl (planLock);
        pendingPlan = plan;
    }
    ++planVersion;
}

void FINISHAudioProcessor::editTrack (const std::function<void (TrackEngine&)>& change)
{
    change (track);
    publishPlan();
    ++resultVersion;
}

void FINISHAudioProcessor::setIntent (const juce::String& text)
{
    intentText = text.trim();
    // FINISH 3: requests about the user's project / the mix go to the project session and the master doctor.
    if (uiPage != 1 && handleProjectCommand (intentText)) return;
    const auto cmd = ComposeCommand::parse (intentText);
    auto parsed = IntentParser::parse (intentText);
    intent = parsed.intent;
    intentUnderstood = cmd.understood;
    for (auto& u : parsed.understood) intentUnderstood.addIfNotAlreadyThere (u);
    rebuildDecisions();

    if (cmd.isEmpty()) return;
    if (cmd.masterTarget >= 0)
        if (auto* t = params.getParameter (ParamIDs::target))
        {
            t->beginChangeGesture();
            t->setValueNotifyingHost (t->convertTo0to1 ((float) cmd.masterTarget));
            t->endChangeGesture();
        }
    auto res = composer.apply (cmd);
    if (cmd.finishTrack || res.needsMaterial)
    {
        finishMyTrack();
        if (res.needsMaterial && ! composing.load())
            statusMessage = utf8 ("D'abord la matière : ") + statusMessage;
        return;
    }
    if (! res.done.isEmpty()) statusMessage = res.done.joinIntoString ("  |  ");
    else if (cmd.masterTarget >= 0) statusMessage = juce::String ("MASTER ") + loudnessTargets[cmd.masterTarget].name;
    ++resultVersion;
}

void FINISHAudioProcessor::setMacro (const char* id, float value)
{
    if (auto* p = params.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void FINISHAudioProcessor::applyProposal (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) decision.proposals.size())) return;
    const auto& p = decision.proposals[(size_t) index];
    if (p.refused) return;
    setMacro (ParamIDs::impact, p.targets.impact);
    setMacro (ParamIDs::punch, p.targets.punch);
    setMacro (ParamIDs::width, p.targets.width);
    setMacro (ParamIDs::warmth, p.targets.warmth);
    setMacro (ParamIDs::loudness, p.targets.loudness);
    setMacro (ParamIDs::amount, p.targets.amount);
    if (auto* b = params.getParameter (ParamIDs::bypass)) { b->beginChangeGesture(); b->setValueNotifyingHost (0.0f); b->endChangeGesture(); }
    appliedStrategy = p.title;
    ++resultVersion;
}

void FINISHAudioProcessor::resetMacros()
{
    const MacroSettings n;
    setMacro (ParamIDs::impact, n.impact);
    setMacro (ParamIDs::punch, n.punch);
    setMacro (ParamIDs::width, n.width);
    setMacro (ParamIDs::warmth, n.warmth);
    setMacro (ParamIDs::loudness, n.loudness);
    setMacro (ParamIDs::amount, n.amount);
    appliedStrategy = {};
    ++resultVersion;
}

void FINISHAudioProcessor::loadReference (const juce::File& file)
{
    if (! file.existsAsFile()) return;
    referenceLoading = true;
    statusMessage = "Analysing reference " + file.getFileName() + " ...";
    ++resultVersion;

    pool.addJob ([this, file]
    {
        auto result = std::make_unique<ReferenceProfile>();
        result->name = file.getFileNameWithoutExtension();
        result->path = file.getFullPathName();

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) })
        {
            AudioAnalyzer offline;
            offline.setPublishSamples (false);
            offline.prepare (reader->sampleRate);
            AnalysisSession refSession;
            std::vector<AnalysisFrame> tmp (512);

            const int chunk = 8192;
            const int chans = (int) juce::jlimit (1u, 2u, reader->numChannels);
            juce::AudioBuffer<float> buf (chans, chunk);
            const juce::int64 maxLen = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0 * 15.0));

            for (juce::int64 pos = 0; pos < maxLen; pos += chunk)
            {
                if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob())
                    if (job->shouldExit()) return;
                const int len = (int) std::min<juce::int64> (chunk, maxLen - pos);
                reader->read (&buf, 0, len, pos, true, chans > 1);
                offline.process (buf.getReadPointer (0), chans > 1 ? buf.getReadPointer (1) : nullptr, len);
                for (int got; (got = offline.readFrames (tmp.data(), (int) tmp.size())) > 0;)
                    for (int i = 0; i < got; ++i) refSession.add (tmp[(size_t) i]);
            }
            result->features = refSession.computeFeatures();
            result->valid = result->features.valid && result->features.durationSec > 5.0f;
        }

        const juce::ScopedLock sl (pendingLock);
        pendingReference = std::move (result);
    });
}

void FINISHAudioProcessor::clearReference()
{
    reference = {};
    statusMessage = "Reference removed.";
    rebuildDecisions();
}

// ======================================================================================
// State

void FINISHAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("version", FINISH_VERSION_STRING, nullptr);
    state.setProperty ("intent", intentText, nullptr);
    state.setProperty ("referencePath", reference.valid ? reference.path : juce::String(), nullptr);
    state.setProperty ("applied", appliedStrategy, nullptr);

    for (auto* name : { "ANALYSIS", "TRACKMAP", "COMPOSE", "PROJECT" })
        state.removeChild (state.getChildWithName (name), nullptr);
    state.appendChild (project.toState(), nullptr);
    state.setProperty ("keepDark", keepDark, nullptr);
    state.setProperty ("keepBright", keepBright, nullptr);
    if (composer.hasDNA())
        state.appendChild (composer.toState(), nullptr);

    if (features.valid)
    {
        juce::ValueTree a ("ANALYSIS");
        a.setProperty ("dur", features.durationSec, nullptr);
        a.setProperty ("lufs", features.integratedLufs, nullptr);
        a.setProperty ("lra", features.loudnessRange, nullptr);
        a.setProperty ("peak", features.peakDb, nullptr);
        a.setProperty ("plr", features.plr, nullptr);
        a.setProperty ("low", features.lowRatio, nullptr);
        a.setProperty ("high", features.highRatio, nullptr);
        a.setProperty ("side", features.sideRatio, nullptr);
        a.setProperty ("lowSide", features.lowSideRatio, nullptr);
        a.setProperty ("corr", features.correlation, nullptr);
        a.setProperty ("punch", features.punchIndex, nullptr);
        state.appendChild (a, nullptr);
    }
    if (track.hasMap())
        state.appendChild (track.toValueTree(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void FINISHAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid() || ! state.hasType (params.state.getType())) return;

    auto analysis = state.getChildWithName ("ANALYSIS").createCopy();
    auto map = state.getChildWithName ("TRACKMAP").createCopy();
    auto compose = state.getChildWithName ("COMPOSE").createCopy();
    auto projectState = state.getChildWithName ("PROJECT").createCopy();
    keepDark = (bool) state.getProperty ("keepDark", false);
    keepBright = (bool) state.getProperty ("keepBright", false);
    for (auto* name : { "ANALYSIS", "TRACKMAP", "COMPOSE", "PROJECT" })
        state.removeChild (state.getChildWithName (name), nullptr);
    params.replaceState (state);

    const auto text = state.getProperty ("intent").toString();
    const auto refPath = state.getProperty ("referencePath").toString();
    const auto applied = state.getProperty ("applied").toString();

    auto restore = [this, text, refPath, applied, analysis, map, compose, projectState]
    {
        project.fromState (projectState);
        if (compose.isValid()) composer.fromState (compose);
        if (analysis.isValid())
        {
            features = {};
            features.valid = true;
            features.durationSec = analysis["dur"];
            features.integratedLufs = analysis["lufs"];
            features.loudnessRange = analysis["lra"];
            features.peakDb = analysis["peak"];
            features.plr = analysis["plr"];
            features.lowRatio = analysis["low"];
            features.highRatio = analysis["high"];
            features.sideRatio = analysis["side"];
            features.lowSideRatio = analysis["lowSide"];
            features.correlation = analysis["corr"];
            features.punchIndex = analysis["punch"];
            analysisState = AnalysisState::Ready;
        }
        track.fromValueTree (map);
        if (refPath.isNotEmpty() && refPath != reference.path)
            loadReference (juce::File (refPath));
        // re-rank everything and publish the section plan (without re-running compose commands)
        intentText = text;
        auto parsed = IntentParser::parse (text);
        intent = parsed.intent;
        intentUnderstood = parsed.understood;
        rebuildDecisions();
        appliedStrategy = applied;
    };

    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
        restore();
    else
        juce::MessageManager::callAsync ([weak = juce::WeakReference<FINISHAudioProcessor> (this), restore]
                                         { if (weak != nullptr) restore(); });
}

juce::AudioProcessorEditor* FINISHAudioProcessor::createEditor()
{
    return new FINISHAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FINISHAudioProcessor();
}
