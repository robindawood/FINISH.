#pragma once
#include "LiveProject.h"

// ======================================================================================
// Understanding the user's Live set from its real content (tracks, devices, clips, MIDI notes):
// what each track is, the key, the structure already written, the material each track plays at its
// strongest moment, what is missing and what is underdeveloped.

enum class SecKind { Intro = 0, Build, Drop, Break, Build2, Drop2, Outro, Rise, Gap, Unknown };
const char* secKindName (SecKind k);

struct DrumMap
{
    std::vector<int> kick, snare, hats, crash, perc;
    int rollPitch = -1;              // what a fill / snare roll is played on (snare, clap... or the busiest non-kick)
    bool hasKick() const noexcept { return ! kick.empty(); }
};

struct ClipRef
{
    bool valid = false, session = false;
    int index = -1;                  // arrangement index or session slot
    double start = 0.0;              // arrangement start (for the bridge to find it)
};

struct TrackProfile
{
    int track = -1;
    TrackRole role = TrackRole::Other;
    float confidence = 0.0f;
    juce::String why;
    ClipRef material;                // the clip FINISH builds from (the track's part at the strongest moment)
    double materialLoop = 0.0;       // its loop length in beats
    float medianPitch = 0.0f, polyphony = 0.0f, notesPerBar = 0.0f, meanLength = 0.0f;
    DrumMap drums;
    bool usable = false;             // has material and is not muted
};

struct ExistingSection
{
    SecKind kind = SecKind::Unknown;
    int startBar = 0, bars = 0;      // 0-based bars of the set
    float energy = 0.0f;             // 0..1 relative to the strongest moment
    std::vector<int> tracks;         // active tracks
    juce::String label() const;
};

struct ProjectAnalysis
{
    bool valid = false;
    double bpm = 120.0, beatsPerBar = 4.0;
    int keyRoot = 0; bool minor = true;
    float keyConfidence = 0.0f;
    juce::String keySource;          // "notes", "Live"
    std::vector<TrackProfile> tracks;          // one per project track (same order)
    std::vector<ExistingSection> sections;     // what is already in the Arrangement
    std::vector<std::vector<float>> activity;  // [track][bar] 0..1
    int firstBar = 0, endBar = 0;              // used Arrangement range (bars)
    int mainSection = -1;                      // the strongest section (the drop)
    bool sessionSketch = false;                // material only in the Session view
    std::vector<SecKind> missing;              // canonical parts not found
    juce::StringArray findings;                // facts about the project, one line each
    juce::StringArray manual;                  // what FINISH cannot do itself
    juce::String summary;

    juce::String keyName() const;
    int countRole (TrackRole r) const noexcept;
    const TrackProfile* profileFor (int track) const noexcept { return track >= 0 && track < (int) tracks.size() ? &tracks[(size_t) track] : nullptr; }
};

namespace ProjectAnalyzer
{
    // Classifies the tracks (writes role fields into `project`) and analyses the set.
    ProjectAnalysis analyse (LiveProject& project);
    // Human report for "écoute et analyse ce que j'ai créé, dis-moi ce qui manque".
    juce::String report (const ProjectAnalysis& a, const LiveProject& p);
    // Diatonic helper used by the variations.
    int scaleStep (int pitch, int steps, int keyRoot, bool minor);
    float energyWeight (TrackRole r);
}
