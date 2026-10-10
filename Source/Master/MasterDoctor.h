#pragma once
#include <JuceHeader.h>
#include "../Analysis/AudioFeatures.h"
#include "../Params.h"

// ======================================================================================
// Mastering from the mix's actual problems: measures (tonal balance in 7 bands, dynamics, stereo / mono
// compatibility, peaks, clipping), compares with genre landmarks (or a reference track), and corrects only what
// is out of range, scaled by a profile and an intensity. Explains every move, and compares before / after.
struct MasterPlan
{
    bool valid = false;
    // Corrections (dB unless noted), already scaled by intensity / profile
    float eqLowDb = 0, eqHighDb = 0, eqMudDb = 0, eqPresDb = 0;
    float sideDb = 0;            // width (M/S side gain)
    float monoBass = 0;          // 0..1 sides below 160 Hz removed
    float glue = 0, clip = 0;    // multiband leveller / soft clipper amounts (0..1)
    float punchKeep = 0;         // 0..1 slower leveller attack, softer clipping (transients pass)
    int recommendedTarget = sameLevelTarget;
    juce::String targetWhy;
    juce::String genre;          // "club", "general"

    struct Finding
    {
        juce::String title;      // "Graves en excès"
        juce::String measured;   // "68 % de l'énergie sous 150 Hz"
        juce::String landmark;   // "repère club 45-65 %"
        juce::String action;     // "-1.5 dB à 110 Hz" / "à corriger dans ton mix : ..."
        float severity = 0;      // 0..1
        bool handled = false;    // FINISH corrects it (else: advice)
    };
    std::vector<Finding> findings;
};

namespace MasterDoctor
{
    enum class Profile { Auto = 0, Punch, Warm, Open };

    struct Request
    {
        AudioFeatures in;
        const AudioFeatures* reference = nullptr;
        Profile profile = Profile::Auto;
        float intensity = 0.7f;
        bool keepDark = false, keepBright = false;
        int targetIndex = 0;
        double bpm = 120.0;
    };

    MasterPlan design (const Request& r);

    // "What FINISH did and why", with before / after measures when the output was measured too.
    juce::String report (const MasterPlan& plan, const AudioFeatures& in, const AudioFeatures* out, float outTruePeakDb,
                         int targetIndex, float targetLufs, float intensity, Profile profile);
    // Answer to a question about the mix ("pourquoi il manque de punch").
    juce::String explain (const AudioFeatures& in, const MasterPlan& plan, const juce::String& topic);

    juce::String profileLabel (Profile p);
    juce::String profileHelp (Profile p);
}
