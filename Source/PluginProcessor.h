#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Version.h"
#include "Analysis/AudioAnalyzer.h"
#include "Analysis/AnalysisSession.h"
#include "DSP/WhaleDSP.h"
#include "Auto/AutoMaster.h"
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
    float inputLoudPassage = -70.0f, outIntegrated = -70.0f;

    AutoState autoState = AutoState::idle;
    bool waitingForAudio = false;
    double verifySeconds = 0.0, grAcc = 0.0;
    int grCount = 0;
    AutoResult autoResult;
    ModuleBase beforeAuto;
    float autoLoudTarget = -7.0f;
    bool haveAutoTarget = false;
    Verdict verdict = Verdict::idle;
    int souffleCount = 0;
    bool souffleArmed = false;
    double verdictHold = 0.0;

    static constexpr double listenSeconds = 12.0, verifyDuration = 6.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhaleAudioProcessor)
};
