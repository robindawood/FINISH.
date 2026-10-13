#pragma once
#include <JuceHeader.h>
#include "../Params.h"
#include "../Analysis/AudioFeatures.h"
#include "StyleProfilesData.h"

// AUTO: compares what was heard with the style's reference masters, measured the same way (SpectralCapture), and
// writes module settings:
//   - tonal balance: the 1/3-octave curve of the mix vs the style's median curve; a band is corrected only when it
//     leaves the band's interquartile tolerance, and then pulled most of the way back towards the median; the wanted
//     correction curve is fitted onto WHALE's EQ tools (bass shelf, 4 bells, air shelf) with limits;
//   - dynamics: the peak-to-loudness ratio to remove is split between compression, clipper and limiter;
//   - width / mono bass, loudness target.
// refine() is the second pass: it measures the PROCESSED output and corrects what is still off (e.g. low end eaten by
// the limiter), then checks the density.
struct AutoResult
{
    bool valid = false;
    int style = styleTechHouse;        // style used (1..6)
    bool styleWasDetected = false;
    float loudPassageTarget = -7.0f;   // LUFS short-term of the loud passages (what the closed loop holds)
    float integratedTarget = -8.5f;    // what the style's masters measure integrated (for display)
    float toneErrorBefore = 0, toneErrorAfter = -1;   // weighted RMS distance (dB) outside tolerance: mix / processed output
    ModuleBase base;
    juce::StringArray report;          // what AUTO did (French, for the user)
    juce::StringArray mixNotes;        // problems that belong to the mix, not to the master
};

class AutoMaster
{
public:
    static const StyleProfileData& profile (int style) noexcept;   // style 1..6
    static int detectStyle (const AudioFeatures& f, double bpm);
    // Tempo from 1 kHz rhythm envelopes (hats grid + kick), half-time folded into 100-200 BPM. 0 if unclear.
    static double estimateTempo (const std::vector<float>& low, const std::vector<float>& high, double rate);

    // First pass, from the unprocessed mix.
    static AutoResult compute (const AudioFeatures& f, int styleParam, double bpm, int targetIndex, float ceilingDb);
    // Second pass, from the processed output: adjusts r.base (and r.loudPassageTarget), appends to the report.
    static void refine (const AudioFeatures& out, float avgLimiterGrDb, AutoResult& r);
    // Final measurement of the processed output (report only).
    static void finalCheck (const AudioFeatures& out, AutoResult& r);

    // Tone analysis shared by both passes (exposed for tests).
    struct ToneAnalysis
    {
        float deviation[29] {};    // mix - style median, both anchored on 200 Hz - 5 kHz (dB)
        float tolerance[29] {};    // half interquartile range of the references (dB, clamped)
        float wanted[29] {};       // correction wanted per band (dB), 0 inside the tolerance
        float weight[29] {};
        float error = 0;           // weighted RMS of the part outside the tolerance (dB)
    };
    static ToneAnalysis analyseTone (const AudioFeatures& f, const StyleProfileData& p, float strength);
    // Least-squares fit of a wanted curve onto the EQ tools: gains (dB) for
    // [0] bass shelf 110 Hz, [1] bell 60 Hz, [2] bell 300 Hz, [3] bell 3.5 kHz, [4] bell 8 kHz, [5] air shelf 10 kHz.
    // The 4 bells' centres can move (specBands[].lo..hi); BellHz = their current centres.
    using BellHz = std::array<float, 4>;
    static BellHz defaultBells() noexcept { return { specBands[0].hz, specBands[1].hz, specBands[2].hz, specBands[3].hz }; }
    static std::array<float, 6> fitTone (const ToneAnalysis& t);
    static std::array<float, 6> fitToneBounded (const ToneAnalysis& t, bool lofi, std::array<float, 6> alreadyApplied, bool lowCutActive = false,
                                                BellHz bells = defaultBells());
    // Places the bells where the wanted curve needs them (coordinate search over each bell's range), then fits the gains.
    static std::array<float, 6> placeAndFit (const ToneAnalysis& t, bool lofi, bool lowCutActive, BellHz& bells);
    static float toolResponseDb (int tool, float hz, float gainDb, float centreHz = 0.0f);
    // Low-cut (6th-order Butterworth high-pass, 0 = off): response and the corner AUTO picks from a tone analysis.
    // currentHz = low-cut already in the measured signal (second pass).
    static float lowCutResponseDb (float cornerHz, float hz);
    static float chooseLowCut (const ToneAnalysis& t, float currentHz, int style);
};
