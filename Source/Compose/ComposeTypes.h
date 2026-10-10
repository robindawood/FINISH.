#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

// UTF-8 literal -> juce::String (French text in the UI and exported notes).
inline juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// ======================================================================================
// FINISH COMPOSE - shared data types.
// Times are in beats (quarter notes) relative to the start of the clip / section.

struct NoteEvent
{
    double beat = 0.0;       // start
    double length = 0.25;    // duration in beats
    int pitch = 60;          // MIDI note
    float velocity = 0.8f;   // 0..1
};

struct CCPoint
{
    double beat = 0.0;
    float value = 1.0f;      // 0..1 (exported as CC74 "brightness / filter")
};

enum class Lane { Drums = 0, Bass, Chords, Lead, Pad, Fx };
constexpr int numLanes = 6;
constexpr int numMidiLanes = 5;
inline const char* laneName (Lane l)
{
    static const char* names[] = { "DRUMS", "BASS", "CHORDS", "LEAD", "PAD", "FX" };
    return names[(int) l];
}
// What to load on the track in the DAW (the MIDI carries notes, not sounds).
inline const char* laneInstrumentHint (Lane l)
{
    static const char* hints[] = {
        "Drum Rack / any GM-mapped kit (C1 kick, D1 snare, D#1 clap, F#1 closed hat, A#1 open hat, C#2 crash)",
        "Mono bass synth (sub + saw), glide off",
        "Poly synth stab / keys",
        "Lead or pluck synth",
        "Pad / strings / texture with slow attack",
        "Audio clips rendered by FINISH (risers, impacts, sweeps)" };
    return hints[(int) l];
}

// General-MIDI drum notes used by the generator.
namespace Drum
{
    constexpr int kick = 36, rim = 37, snare = 38, clap = 39, closedHat = 42, lowTom = 45, openHat = 46,
                  midTom = 47, highTom = 50, crash = 49, ride = 51, shaker = 70, perc = 56 /* cowbell slot */;
}

enum class FxKind { None = 0, Riser, Downlifter, Impact, ReverseCymbal, Sweep };
inline const char* fxName (FxKind k)
{
    static const char* names[] = { "-", "RISER", "DOWNLIFTER", "IMPACT", "REVERSE CYMBAL", "SWEEP" };
    return names[(int) k];
}

struct FxEvent
{
    FxKind kind = FxKind::None;
    double beat = 0.0, length = 4.0;
    float level = 0.8f;
};

struct Clip
{
    std::vector<NoteEvent> notes;
    std::vector<CCPoint> cc;      // filter automation (empty = none)
    std::vector<FxEvent> fx;      // FX lane only
    bool empty() const noexcept { return notes.empty() && fx.empty(); }
};

enum class Role { Intro = 0, Build, Drop, Break, Bridge, Drop2, Outro };
constexpr int numRoles = 7;
inline const char* roleName (Role r)
{
    static const char* names[] = { "INTRO", "BUILD", "DROP", "BREAK", "BRIDGE", "DROP 2", "OUTRO" };
    return names[(int) r];
}

enum class Flavour { Faithful = 0, Atmospheric, Intense };
constexpr int numFlavours = 3;
inline const char* flavourName (Flavour f)
{
    static const char* names[] = { "FIDELE", "ATMO", "INTENSE" };
    return names[(int) f];
}

enum class Style { House = 0, Techno, Melodic, Trance, DnB, Trap, HipHop, Ambient };
constexpr int numStyles = 8;
inline const char* styleName (Style s)
{
    static const char* names[] = { "HOUSE", "TECHNO", "MELODIC TECHNO", "TRANCE", "DRUM & BASS", "TRAP", "HIP-HOP", "AMBIENT" };
    return names[(int) s];
}

inline juce::String pitchClassName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

// Per-section directions coming from the free command field.
struct SectionMood
{
    float darker = 0, brighter = 0, melodic = 0, atmos = 0, intense = 0, progressive = 0, minimal = 0;
    bool any() const noexcept { return darker + brighter + melodic + atmos + intense + progressive + minimal > 0; }
};

// ======================================================================================
// What FINISH heard: the musical identity of the material the user played (a drop, a loop, just a melody...).
struct MusicalDNA
{
    bool valid = false;
    double bpm = 120.0;
    double barsAnalysed = 0.0;
    bool tempoFromHost = true;

    // Key
    int keyRoot = 9;              // pitch class (A)
    bool minor = true;
    float keyConfidence = 0.0f;   // 0..1
    int altKeyRoot = 0; bool altMinor = false;
    std::array<float, 12> chroma {};

    // Groove (one bar, 16 steps, 0..1 strength)
    std::array<float, 16> kick {}, snare {}, hats {};
    float swing = 0.0f;           // 0 = straight, 0.33 = heavy shuffle
    bool fourOnFloor = false, halfTime = false;

    // Harmony: one chord per bar, looping (root pitch class + minor flag)
    std::vector<int> chordRoots;
    std::vector<bool> chordMinor;
    float harmonyConfidence = 0.0f;

    // Lines (looping), beats from the loop start
    std::vector<NoteEvent> bassLine;  int bassBars = 0;
    std::vector<NoteEvent> melody;    int melodyBars = 0;
    float melodyConfidence = 0.0f;

    // Character
    float energy = 0.0f;          // 0..1
    float density = 0.0f;         // onsets per beat
    float brightness = 0.0f;      // 0..1
    float loudnessLufs = -70.0f;
    bool hasDrums = false, hasBass = false, hasChords = false, hasMelody = false;

    Style style = Style::Techno;
    float styleConfidence = 0.0f;
    bool styleOverridden = false, keyOverridden = false;

    juce::String keyName() const { return pitchClassName (keyRoot) + (minor ? " MIN" : " MAJ"); }
    juce::StringArray uncertainties() const
    {
        juce::StringArray u;
        if (keyConfidence < 0.45f && ! keyOverridden) u.add ("Key uncertain (" + keyName() + " or " + pitchClassName (altKeyRoot) + (altMinor ? " MIN" : " MAJ") + ")");
        if (styleConfidence < 0.5f && ! styleOverridden) u.add ("Style is a guess");
        if (hasMelody && melodyConfidence < 0.4f) u.add ("Melody transcription approximate");
        if (! tempoFromHost) u.add ("Tempo estimated from audio");
        return u;
    }
};

// ======================================================================================
struct GenSection
{
    Role role = Role::Intro;
    juce::String name;            // "INTRO", "BUILD 2", ...
    int bars = 16;
    int startBar = 0;             // position in the arrangement
    bool locked = false;
    bool isReference = false;     // the user's own material (exported as a sketch only on request)
    std::array<Clip, numLanes> clips;
    std::array<Clip, numLanes> layers;   // extra parts layered over the user's own section (e.g. accents on their drop)
    SectionMood mood;
    juce::uint32 seed = 1;
    juce::String recipe;          // one short line: what this section does

    double lengthBeats() const noexcept { return bars * 4.0; }
};

struct Arrangement
{
    Flavour flavour = Flavour::Faithful;
    float creativity = 0.35f;
    double bpm = 120.0;
    std::vector<GenSection> sections;
    juce::uint32 paletteSeed = 1;   // shared material (motif, bass, drum palette) for every section
    juce::String summary;           // what this version does, in one or two lines
    int version = 0;

    int totalBars() const noexcept
    {
        int t = 0;
        for (auto& s : sections) t = std::max (t, s.startBar + s.bars);
        return t;
    }
    void layout() noexcept
    {
        int bar = 0;
        for (auto& s : sections) { s.startBar = bar; bar += s.bars; }
    }
    bool empty() const noexcept { return sections.empty(); }
};
