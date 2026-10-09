#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    dsp.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    layerSmoothed = {};
    keepSmoothed = 0;
    setLatencySamples (0);
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
    double songTime = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSeconds()) { songTime = *t; timeValid = true; }
        }
    hostPlaying.store (playing);
    hostTimeValid.store (timeValid);
    if (timeValid) hostTime.store (songTime);

    // Diagnose the incoming (unprocessed) track, stamped with the song position while playing.
    analyzer.process (buffer.getReadPointer (0),
                      buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr,
                      n, playing && timeValid ? songTime : -1.0);

    // FINISH MY TRACK: pick up a new section plan if one is waiting (never blocks).
    if (planVersion.load() != audioPlanVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (planLock);
        if (tl.isLocked()) { audioPlan = pendingPlan; audioPlanVersion = planVersion.load(); }
    }

    SectionPlan::Layer target;
    if (timeValid && audioPlan.count > 0)
        target = audioPlan.evaluate (songTime + 0.5 * n / currentSampleRate.load(), pCoherence->load());
    else if (audioPlan.count > 0)
        target.delta = audioPlan.mean.scaled (0.6f * pCoherence->load());

    // Block-rate smoothing on top of the DSP's per-sample smoothing: section changes glide, never click.
    const float a = std::exp (-(float) n / (float) (currentSampleRate.load() * 0.06));
    layerSmoothed = SectionMacros::lerp (target.delta, layerSmoothed, a);
    keepSmoothed = target.keepOriginal + (keepSmoothed - target.keepOriginal) * a;
    liveImpact = layerSmoothed.impact; livePunch = layerSmoothed.punch; liveWidth = layerSmoothed.width;
    liveWarmth = layerSmoothed.warmth; liveLoud = layerSmoothed.loudness; liveTrim = layerSmoothed.trimDb;

    FinishDSP::Settings s;
    s.macros = getCurrentMacros();
    s.macros.impact   = juce::jlimit (0.0f, 1.0f, s.macros.impact   + layerSmoothed.impact);
    s.macros.punch    = juce::jlimit (0.0f, 1.0f, s.macros.punch    + layerSmoothed.punch);
    s.macros.width    = juce::jlimit (0.0f, 1.0f, s.macros.width    + layerSmoothed.width);
    s.macros.warmth   = juce::jlimit (0.0f, 1.0f, s.macros.warmth   + layerSmoothed.warmth);
    s.macros.loudness = juce::jlimit (0.0f, 1.0f, s.macros.loudness + layerSmoothed.loudness);
    s.trimDb = layerSmoothed.trimDb;
    s.sectionDry = keepSmoothed;
    s.bypass = pBypass->load() > 0.5f;
    s.levelMatch = pMatch->load() > 0.5f;
    dsp.process (buffer, s);
}

SectionMacros FINISHAudioProcessor::getLiveSectionLayer() const noexcept
{
    return { liveImpact.load(), livePunch.load(), liveWidth.load(), liveWarmth.load(), liveLoud.load(), liveTrim.load() };
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
        if (analysisState == AnalysisState::Listening)
            for (int i = 0; i < got; ++i) session.add (frameScratch[(size_t) i]);
    }

    if (analysisState == AnalysisState::Listening)
    {
        const bool playing = hostPlaying.load();
        if (playing) sawPlayback = true;
        // Auto-finish when the user stops the transport after playing the track.
        if ((sawPlayback && ! playing && session.getDuration() >= 8.0) || session.getFrames().size() >= AnalysisSession::maxFrames)
            finishAnalysis();
    }

    updateSpectrum();

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
    if (session.getDuration() < 3.0)
    {
        analysisState = features.valid ? AnalysisState::Ready : AnalysisState::Idle;
        statusMessage = "Not enough audio. Play at least a few seconds (ideally the whole track) while FINISH is listening.";
        session.clear();
        ++resultVersion;
        return;
    }
    features = session.computeFeatures();
    track.build (session.getFrames());
    session.clear();
    analysisState = AnalysisState::Ready;
    const auto& m = track.getMusic();
    statusMessage = "Track map ready: " + juce::String ((int) m.sections.size()) + " sections, "
                  + juce::String ((int) track.getTransitions().size()) + " transitions, "
                  + juce::String (juce::roundToInt (features.durationSec)) + " s analysed.";
    appliedStrategy = {};
    rebuildDecisions();
}

void FINISHAudioProcessor::rebuildDecisions()
{
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
    auto parsed = IntentParser::parse (intentText);
    intent = parsed.intent;
    intentUnderstood = parsed.understood;
    rebuildDecisions();
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

    for (auto* name : { "ANALYSIS", "TRACKMAP" })
        state.removeChild (state.getChildWithName (name), nullptr);

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
    for (auto* name : { "ANALYSIS", "TRACKMAP" })
        state.removeChild (state.getChildWithName (name), nullptr);
    params.replaceState (state);

    const auto text = state.getProperty ("intent").toString();
    const auto refPath = state.getProperty ("referencePath").toString();
    const auto applied = state.getProperty ("applied").toString();

    auto restore = [this, text, refPath, applied, analysis, map]
    {
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
        setIntent (text);           // re-ranks everything and publishes the section plan
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
