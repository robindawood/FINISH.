#pragma once
#include <cmath>

// Allocation-free stereo biquad (transposed direct form II, RBJ cookbook designs).
// Coefficients can be recomputed on the audio thread without any heap activity.
struct Biquad
{
    static constexpr int maxChannels = 2;

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1[maxChannels] {}, z2[maxChannels] {};

    inline float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept
    {
        for (int c = 0; c < maxChannels; ++c) z1[c] = z2[c] = 0.0f;
    }

    void setLowPass (double sr, double f, double q)   { design (Type::lowPass,   sr, f, q, 0.0); }
    void setHighPass (double sr, double f, double q)  { design (Type::highPass,  sr, f, q, 0.0); }
    void setLowShelf (double sr, double f, double db) { design (Type::lowShelf,  sr, f, 0.7071, db); }
    void setHighShelf (double sr, double f, double db){ design (Type::highShelf, sr, f, 0.7071, db); }
    void setPeak (double sr, double f, double q, double db) { design (Type::peak, sr, f, q, db); }

private:
    enum class Type { lowPass, highPass, lowShelf, highShelf, peak };

    void design (Type type, double sr, double f, double q, double gainDb) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        f = std::fmin (f, sr * 0.45);
        const double w0 = 2.0 * pi * f / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double sA = std::sqrt (A);

        double nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;

        switch (type)
        {
            case Type::lowPass:
                nb0 = (1 - cw) / 2; nb1 = 1 - cw; nb2 = (1 - cw) / 2;
                na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::highPass:
                nb0 = (1 + cw) / 2; nb1 = -(1 + cw); nb2 = (1 + cw) / 2;
                na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::lowShelf:
                nb0 = A * ((A + 1) - (A - 1) * cw + 2 * sA * alpha);
                nb1 = 2 * A * ((A - 1) - (A + 1) * cw);
                nb2 = A * ((A + 1) - (A - 1) * cw - 2 * sA * alpha);
                na0 = (A + 1) + (A - 1) * cw + 2 * sA * alpha;
                na1 = -2 * ((A - 1) + (A + 1) * cw);
                na2 = (A + 1) + (A - 1) * cw - 2 * sA * alpha;
                break;
            case Type::highShelf:
                nb0 = A * ((A + 1) + (A - 1) * cw + 2 * sA * alpha);
                nb1 = -2 * A * ((A - 1) + (A + 1) * cw);
                nb2 = A * ((A + 1) + (A - 1) * cw - 2 * sA * alpha);
                na0 = (A + 1) - (A - 1) * cw + 2 * sA * alpha;
                na1 = 2 * ((A - 1) - (A + 1) * cw);
                na2 = (A + 1) - (A - 1) * cw - 2 * sA * alpha;
                break;
            case Type::peak:
                nb0 = 1 + alpha * A; nb1 = -2 * cw; nb2 = 1 - alpha * A;
                na0 = 1 + alpha / A; na1 = -2 * cw; na2 = 1 - alpha / A;
                break;
        }

        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
};

// One-pole envelope coefficient for a time constant in seconds.
inline float envCoeff (double sampleRate, double seconds) noexcept
{
    return (float) std::exp (-1.0 / (seconds * sampleRate));
}

// ITU-R BS.1770 style K-weighting (pre-filter shelf + RLB high-pass), valid at any sample rate.
struct KWeighting
{
    Biquad shelf, hp;
    void prepare (double sr) { shelf.setHighShelf (sr, 1681.97, 4.0); hp.setHighPass (sr, 38.13, 0.5); reset(); }
    void reset() { shelf.reset(); hp.reset(); }
    inline float process (float x, int ch) noexcept { return hp.process (shelf.process (x, ch), ch); }
};
