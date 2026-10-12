#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Version.h"
#include "Analysis/AudioAnalyzer.h"
#include "Analysis/AnalysisSession.h"
#include "DSP/WhaleDSP.h"
#include "Auto/AutoMaster.h"
#include "Auto/SpectralCapture.h"
#include "Presets/Presets.h"

// Threads:
//   audio thread   : input analyzer (for AUTO) + WhaleDSP. Reads parameters through atomics, publishes meters.
//   message thread : 30 Hz timer: AUTO state machine, loudness feed-forward, verdict for the whale scene.
class WhaleAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    WhaleAudioProcessor();
    ~WhaleAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "WHALE"; }
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

    juce::AudioProcessorValueTreeState state;
    PresetManager presets;
    WhaleDSP& getDSP() { return dsp; }

    // ---- AUTO
    enum class AutoState { idle, listening, verifying, done };
    void toggleAuto();                          // start, or cancel while running
    AutoState getAutoState() const { return autoState; }
    float getAutoProgress() const;              // 0..1 over listening + verifying
    bool isWaitingForAudio() const { return autoState == AutoState::listening && waitingForAudio; }
    const AutoResult& getAutoResult() const { return autoResult; }
    int getActiveStyle() const;                 // style used for targets (detected one when "Auto")

    // ---- Verdict for the whale scene
    enum class Verdict { idle, ok, working, validated, tooLoud, tooSoft, clipping, phase };
    Verdict getVerdict() const { return verdict; }
    juce::String getVerdictText() const;
    float getEffectiveTarget() const { return effectiveTarget.load(); }     // loud-passage LUFS target
    float getLoudnessError() const;                                       // out short-term - target (dB)
    int getSouffleCount() const { return souffleCount; }                  // increments once per validated AUTO
    float getOutputIntegrated() const { return outIntegrated; }
    bool isTargetLoudnessMode() const;

    void applyPreset (int index);
    void resetToNeutral();

    friend struct TestAccess;
private:
    void timerCallback() override;
    WhaleDSP::Settings buildSettings() noexcept;
    void writeBase (const ModuleBase& b);
    ModuleBase readBase() const;

    WhaleDSP dsp;
    AudioAnalyzer inAnalyzer, outAnalyzer;
    AnalysisSession autoSession;
    SpectralCapture inCap, outCap;                       // AUTO: spectra measured like the reference profiles
    std::vector<AnalysisFrame> verifyOutFrames;          // output loudness frames during a verification capture
    std::atomic<bool> capInput { false };                // audio thread: block is not silence
    std::vector<AnalysisFrame> recentIn, recentOut;     // rolling windows (message thread)
    std::vector<float> envLow, envHigh;                  // rhythm envelopes heard by AUTO (tempo when the host gives none)

    // parameter atomics
    std::atomic<float> *pStyle = nullptr, *pTarget = nullptr, *pCeil = nullptr, *pDosage = nullptr, *pBypass = nullptr, *pMatch = nullptr, *pManual = nullptr;
    std::atomic<float> *pM[8] {};
    std::atomic<float> *pOn[6] {};
    std::atomic<float> *pSpecG[4] {}, *pSpecD[4] {};
    std::atomic<float> *pGlue = nullptr, *pPunch = nullptr, *pDetail = nullptr, *pSat = nullptr, *pSide = nullptr, *pMono = nullptr, *pLow = nullptr, *pAir = nullptr, *pClip = nullptr;

    std::atomic<double> hostBpm { 0.0 };
    std::atomic<float> effectiveTarget { -9.0f }, driveFF { 0.0f };
    std::atomic<float> inputLoudPassage { -70.0f };      // read by the audio thread (loudness loop gate)
    float outIntegrated = -70.0f;

    AutoState autoState = AutoState::idle;
    bool waitingForAudio = false;
    double verifySeconds = 0.0, grAcc = 0.0, verifyPhaseStart = 0.0;
    int verifyPhase = 0;                                 // 0 settle, 1 measure + refine, 2 final measurement
    int grCount = 0;
    AutoResult autoResult;
    ModuleBase beforeAuto;
    float autoLoudTarget = -7.0f;
    bool haveAutoTarget = false;
    Verdict verdict = Verdict::idle;
    int souffleCount = 0;
    bool souffleArmed = false;
    double verdictHold = 0.0;

    static AudioFeatures makeFeatures (const std::vector<AnalysisFrame>& frames, const SpectralCapture::Result& spec);
    void finishListening();
    void startVerifyCapture();
    // What the processed output of the LISTENED passages looks like: listened spectrum + (output - input) measured
    // simultaneously on the same loud passages during verification (robust to the song moving to a breakdown).
    AudioFeatures estimateProcessed() const;

    AudioFeatures listenedFeatures;
    float autoInputLoud = -70.0f;                        // loud-passage loudness of what AUTO listened to
    std::atomic<float> autoInputLoudRef { -100.0f };
    bool verifyGateOpen = false;
    double verifyPhaseWall = 0.0;                        // time spent in the current phase (also while gated)

    // AUTO listens 30 s of playing audio (silence is not counted); the spectrum keeps the loudest 60 % of it.
    // Then: 3 s settle, 15 s measured on the processed output -> second pass, 10 s -> final measurement.
    static constexpr double listenSeconds = 30.0, settleSeconds = 3.0, refineSeconds = 15.0, finalSeconds = 10.0;
    static constexpr double verifyDuration = settleSeconds + refineSeconds + finalSeconds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhaleAudioProcessor)
};
