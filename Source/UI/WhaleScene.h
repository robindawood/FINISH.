#pragma once
#include <JuceHeader.h>
#include "Theme.h"

// The cosmic whale. Drawn from the sound, not an image:
//   - each body line is a frequency band (belly = lows in magenta, back = highs in cyan) and ripples with it;
//   - the tail beats with the kick, the trails widen with the stereo image;
//   - the dotted surface above is the loudness target: the whale rises towards it while AUTO works, sits just under
//     it when the master is on target, breaks through it (orange) when too loud;
//   - problems: ghost double (phase), sparks (clipping); validated: "le souffle" (star geyser + ripples + Cetus).
class WhaleScene : public juce::Component
{
public:
    enum class Mood { idle, ok, working, validated, tooLoud, tooSoft, clipping, phase };

    struct Input
    {
        float bands[8] {};          // output band levels, dB
        float kick = 0;             // 0..1
        float width = 0;            // side / (mid + side)
        float loudErr = -8;         // output loud-passage loudness - target (dB)
        Mood mood = Mood::idle;
        int souffleCount = 0;
        bool reduced = false;       // "animations réduites"
        float autoProgress = 0;
        bool autoRunning = false;
        // texts
        juce::String lufsText = "--", targetText, verdictText, caption;
        juce::Colour verdictColour = Theme::dim;
        float meter = 0;            // 0..1 loudness meter
        // sea floor (1.3): what is heard vs the style's references, 1/3 octaves 25 Hz - 16 kHz
        bool hasSpectrum = false;
        float spectrum[29] {}, refMed[29] {}, refLo[29] {}, refHi[29] {};
        juce::String styleName;
    };

    WhaleScene();
    void advance (const Input& in, float dt);
    void paint (juce::Graphics&) override;
    void resized() override;
    float surfaceY() const { return (float) getHeight() * 0.30f; }

private:
    struct Frame { float x, y, tx, ty, nx, ny; };
    Frame frame (float s) const;
    juce::Point<float> bodyPoint (float s, float u, float wob, int k) const;
    static float profile (float s);
    void drawWhale (juce::Graphics& g, juce::Colour a, juce::Colour b, float alpha, juce::Point<float> offset, bool ghost);
    void drawTrails (juce::Graphics& g, juce::Colour c);
    void drawSurface (juce::Graphics& g);
    void drawSouffle (juce::Graphics& g);
    void drawHud (juce::Graphics& g);
    void drawSeabed (juce::Graphics& g);
    void drawPulse (juce::Graphics& g, juce::Colour c);
    void buildBackground();

    Input in;
    juce::Image background;
    juce::Random rng { 7 };
    float t = 0, swim = 0, cx = 0, cy = 0, tilt = 0, vy = 0, chaos = 0.5f, glow = 0.6f, hot = 0;
    float L = 600, H = 70;
    float bandAvg[8] {}, bandE[8] {};
    float kickSm = 0, widthSm = 0;
    // 1.3: living whale (turquoise when validated, glow + ripples + bubbles on the kick) and sea floor
    float teal = 0, kickFast = 0, kickPrev = 0, kickCool = 0;
    struct Ring { float age; };
    std::vector<Ring> rings;
    struct Bubble { juce::Point<float> p; float vy, r, life, phase; };
    std::vector<Bubble> bubbles;
    float seaShown[29] {}, refShown[29] {}, refLoShown[29] {}, refHiShown[29] {};
    bool seaInit = false, refInit = false;
    juce::Point<float> flukeTips[2], tail, finTip, blowhole;

    // le souffle
    int lastSouffle = 0;
    float souffleT = -1.0f;
    struct Particle { juce::Point<float> p, v; float life, size; juce::Colour c; };
    std::vector<Particle> particles, plankton;
    struct Spark { juce::Point<float> a, b; float life; };
    std::vector<Spark> sparks;
};
