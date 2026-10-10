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
};
