#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Version.h"
#include "Analysis/AudioAnalyzer.h"
#include "Analysis/AnalysisSession.h"
#include "Diagnosis/DiagnosisEngine.h"
#include "Decision/DecisionEngine.h"
#include "Intent/IntentParser.h"
#include "Reference/ReferenceProfile.h"
#include "Music/TrackEngine.h"
#include "DSP/FinishDSP.h"

// Threading model
//   audio thread  : analyzer + section plan evaluation (by host song position) + DSP. Atomics, lock-free FIFOs,
//                   and a try-lock to pick up a new section plan (never blocks).
//   message thread: analysis session, diagnosis, FINISH MY TRACK engine, UI (30 Hz timer).
//   worker thread : offline analysis of the reference file.
class FINISHAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    enum class AnalysisState { Idle, Listening, Ready };
    static constexpr int spectrumBins = 96;

    FINISHAudioProcessor();
    ~FINISHAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FINISH"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState params;

    // ---- Message-thread API --------------------------------------------------------------
    void toggleAnalysis();                 // start listening / finish and build the track map
    void cancelAnalysis();
    AnalysisState getAnalysisState() const noexcept { return analysisState; }
    double getListeningSeconds() const     { return session.getDuration(); }
    const AnalysisSession& getSession() const noexcept { return session; }
    bool isHostPlaying() const noexcept    { return hostPlaying.load(); }
    double getHostTime() const noexcept    { return hostTime.load(); }
    bool hasHostTime() const noexcept      { return hostTimeValid.load(); }

    void setIntent (const juce::String& text);
    const juce::String& getIntentText() const noexcept { return intentText; }
    const juce::StringArray& getIntentUnderstood() const noexcept { return intentUnderstood; }

    // Whole-track strategies (PUNCH / WIDE / MASSIVE) drive the global controls.
    void applyProposal (int index);
    void resetMacros();
    const juce::String& getAppliedStrategy() const noexcept { return appliedStrategy; }

    // FINISH MY TRACK. Every change goes through here so the audio thread gets the new plan.
    const TrackEngine& getTrack() const noexcept { return track; }
    void editTrack (const std::function<void (TrackEngine&)>& change);
    float getCoherence() const noexcept { return pCoherence->load(); }

    void loadReference (const juce::File&);
    void clearReference();
    bool isReferenceLoading() const noexcept { return referenceLoading.load(); }
    const ReferenceProfile& getReference() const noexcept { return reference; }

    const AudioFeatures& getFeatures() const noexcept   { return features; }
    const MusicState& getMusic() const noexcept         { return track.getMusic(); }
    const std::vector<Diagnosis>& getDiagnoses() const noexcept { return diagnoses; }
    const DecisionResult& getDecision() const noexcept  { return decision; }
    int getScore() const noexcept                       { return score; }
    const juce::String& getStatusMessage() const noexcept { return statusMessage; }
    int getResultVersion() const noexcept               { return resultVersion + track.getVersion(); }

    const std::array<float, spectrumBins>& getSpectrum() const noexcept { return spectrum; }
    const FinishDSP& getDSP() const noexcept { return dsp; }
    // Section layer currently applied by the audio thread (for the UI).
    SectionMacros getLiveSectionLayer() const noexcept;

    MacroSettings getCurrentMacros() const;

private:
    void timerCallback() override;
    void finishAnalysis();
    void rebuildDecisions();
    void publishPlan();
    void updateSpectrum();
    void setMacro (const char* id, float value);

    // Audio
    FinishDSP dsp;
    AudioAnalyzer analyzer;
    std::atomic<bool> hostPlaying { false }, hostTimeValid { false };
    std::atomic<double> hostTime { 0.0 };
    std::atomic<float>* pAmount = nullptr; std::atomic<float>* pImpact = nullptr; std::atomic<float>* pPunch = nullptr;
    std::atomic<float>* pWidth = nullptr;  std::atomic<float>* pWarmth = nullptr; std::atomic<float>* pLoud = nullptr;
    std::atomic<float>* pBypass = nullptr; std::atomic<float>* pMatch = nullptr; std::atomic<float>* pCoherence = nullptr;
    std::atomic<double> currentSampleRate { 44100.0 };

    // Section plan hand-over (message thread writes under lock, audio thread try-locks).
    juce::SpinLock planLock;
    SectionPlan pendingPlan, audioPlan;
    std::atomic<int> planVersion { 0 };
    int audioPlanVersion = -1;
    SectionMacros layerSmoothed;
    float keepSmoothed = 0;
    std::atomic<float> liveImpact { 0 }, livePunch { 0 }, liveWidth { 0 }, liveWarmth { 0 }, liveLoud { 0 }, liveTrim { 0 };

    // Message thread state
    AnalysisSession session;
    DiagnosisEngine diagnosisEngine;
    DecisionEngine decisionEngine;
    TrackEngine track;
    AnalysisState analysisState = AnalysisState::Idle;
    bool sawPlayback = false;
    AudioFeatures features;
    std::vector<Diagnosis> diagnoses;
    DecisionResult decision;
    int score = 0, resultVersion = 0;
    juce::String statusMessage, intentText, appliedStrategy;
    juce::StringArray intentUnderstood;
    IntentVector intent;
    ReferenceProfile reference;

    // Spectrum (message thread)
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> sampleRing, fftData, scratch;
    int ringPos = 0;
    std::array<float, spectrumBins> spectrum {};
    std::vector<AnalysisFrame> frameScratch;

    // Reference worker
    juce::CriticalSection pendingLock;
    std::unique_ptr<ReferenceProfile> pendingReference;
    std::atomic<bool> referenceLoading { false };
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (FINISHAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FINISHAudioProcessor)
};
