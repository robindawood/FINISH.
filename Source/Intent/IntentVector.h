#pragma once
#include <cmath>

// What the producer asked for, each axis in -1..1 (0 = not mentioned).
struct IntentVector
{
    float impact = 0, energy = 0, punch = 0, width = 0, warmth = 0, brightness = 0, loudness = 0, dynamics = 0, bass = 0;

    bool isEmpty() const noexcept
    {
        for (float v : { impact, energy, punch, width, warmth, brightness, loudness, dynamics, bass })
            if (std::abs (v) > 1.0e-6f) return false;
        return true;
    }
};
