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
};
