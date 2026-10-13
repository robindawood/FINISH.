#pragma once

// Whole-track features computed from an analysis session (message thread).
struct AudioFeatures
{
    bool valid = false;
    float durationSec = 0;
    float integratedLufs = -70;   // BS.1770-style gated loudness
    float loudnessRange = 0;      // LU, P95 - P10 of 3 s short-term loudness
    float peakDb = -100;          // sample peak, dBFS
    float plr = 0;                // peak-to-loudness ratio (dB): low = squashed
    float lowRatio = 0;           // mid energy < 150 Hz / total mid energy
    float highRatio = 0;          // mid energy > 4 kHz / total mid energy
    float sideRatio = 0;          // side / (mid + side) energy: stereo width
    float lowSideRatio = 0;       // stereo content in the bass
    float correlation = 1;        // L/R correlation (-1..1)
    float punchIndex = 0;         // average attack strength (dB)
    float subRatio = 0;           // mid < 60 Hz / mid
    float lowMidRatio = 0;        // mid 200-500 Hz / mid energy above 150 Hz
    float presenceRatio = 0;      // mid 2-5 kHz / mid energy above 150 Hz
    float clipsPerMinute = 0;     // samples at full scale in the input, per minute
    float monoLossDb = 0;         // level lost when summed to mono (3 dB = uncorrelated, > 4.5 = phase problems)

    // AUTO: spectral measurement identical to the reference profiles (SpectralCapture). When hasSpectrum is set, the
    // ratios above come from it, plr = true peak - integrated loudness (like the profiles).
    bool hasSpectrum = false;
    float thirdOctDb[29] {};      // 1/3-octave band power / total mid power (dB), loud frames only
    float truePeakDb = -100;
    float loudSeconds = 0;        // seconds of loud material the spectrum was built from
    float loudLufs = -70;         // loud passages: median of the 3 s short-term values above their median (like the profiles' st_drop)
    float quietShare = 0;         // share of the listened time > 6 dB below the loud passages (a break in the extract)
    double hostStart = -1, hostEnd = -1;   // song position (s) of the listened audio, -1 when the host gave none
};
