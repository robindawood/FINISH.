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
#include "License/LicenseManager.h"

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
    // After AUTO: back to exactly what was there before it (settings, macros, preset, target), in one click.
    bool canUndoAuto() const { return undo.valid; }
    void undoAuto();
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
    // ---- Indicators (what is heard)
    float getToneDistance() const { return toneDistance.load(); }    // dB outside the style's references, -1 = not enough heard yet
    float getExpectedCrest() const;                                  // peak-to-loudness the target leaves (ceiling - target)
    bool isMatchMeasured() const { return dsp.isMatchReady(); }
    bool isTargetLoudnessMode() const;

    // Factory presets are a CHARACTER layered on AUTO's CORRECTION: choosing one keeps what AUTO fixed (tonal balance,
    // low-cut, mono bass, dynamics need) and adds the preset's colour; running AUTO keeps the chosen character.
    // User presets are full snapshots and replace both layers.
    void applyPreset (int index);
    void resetToNeutral();
    bool hasAutoCorrection() const { return haveCorrection; }
    int getCharacterPreset() const { return characterIndex; }
    static ModuleBase combineLayers (const ModuleBase* correction, const float* correctionCurve, const WhalePreset* character, int style);
    static void characterCurve (const WhalePreset& p, float out[29]);

    friend struct TestAccess;
    LicenseManager& licence = LicenseManager::get();

private:
    juce::int64 lockPos = 0;              // trial over: position in the 40 s silence cycle (samples)
    void applyTrialSilence (juce::AudioBuffer<float>&) noexcept;
    void timerCallback() override;
    WhaleDSP::Settings buildSettings() noexcept;
    void writeBase (const ModuleBase& b);
    ModuleBase readBase() const;

    WhaleDSP dsp;
    AudioAnalyzer inAnalyzer, outAnalyzer;
    AnalysisSession autoSession;
    SpectralCapture inCap, outCap;                       // AUTO: spectra measured like the reference profiles
    SpectralCapture liveCap;                             // live TONALITÉ meter: rolling 20 s of what is heard
    std::atomic<float> toneDistance { -1.0f };
    int toneTick = 0;
    void updateToneMeter();
    std::vector<AnalysisFrame> verifyOutFrames;          // output loudness frames during a verification capture
    std::atomic<bool> capInput { false };                // audio thread: block is not silence
    std::vector<AnalysisFrame> recentIn, recentOut;     // rolling windows (message thread)
    std::vector<float> envLow, envHigh;                  // rhythm envelopes heard by AUTO (tempo when the host gives none)

    // parameter atomics
    std::atomic<float> *pStyle = nullptr, *pTarget = nullptr, *pCeil = nullptr, *pDosage = nullptr, *pBypass = nullptr, *pMatch = nullptr, *pManual = nullptr;
    std::atomic<float> *pM[8] {};
    std::atomic<float> *pOn[6] {};
    std::atomic<float> *pSpecG[4] {}, *pSpecD[4] {}, *pSpecF[4] {};
    std::atomic<float> *pGlue = nullptr, *pPunch = nullptr, *pDetail = nullptr, *pSat = nullptr, *pSide = nullptr, *pMono = nullptr, *pLow = nullptr, *pAir = nullptr, *pClip = nullptr, *pLowCut = nullptr;

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

    ModuleBase correction;                    // AUTO's decision (the corrective layer)
    float correctionCurve[29] {};             // response of its EQ tools
    bool haveCorrection = false;
    struct UndoSnapshot
    {
        bool valid = false;
        ModuleBase base, correction;
        float macros[8] {}, curve[29] {}, charCurve[29] {};
        bool haveCorrection = false, haveAutoTarget = false;
        int characterIndex = -1, presetCurrent = -1;
        float autoLoudTarget = 0, inLoudRef = -100.0f;
        AutoResult result;
        AutoState state = AutoState::idle;
    } undo;
    void takeUndoSnapshot();
    int characterIndex = -1;                  // factory preset used as character (-1 = none)
    float charCurve[29] {};
    void rebuildLayers();                     // writes correction (+) character to the parameters
    const float* activeCharacterCurve() const { return characterIndex >= 0 ? charCurve : nullptr; }

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
