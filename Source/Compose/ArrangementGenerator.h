#pragma once
#include "ComposeTypes.h"

// Builds a full arrangement (intro, build, break/bridge, drop 2, outro + transitions) from the user's material.
// Everything is derived from the MusicalDNA (key, chords, groove, bass, motif) so the new parts extend the idea
// instead of being a generic template. Deterministic for a given seed.
namespace ArrangementGenerator
{
    // Musical material shared by every section of one arrangement.
    struct Palette
    {
        int keyRoot = 9; bool minor = true;
        Style style = Style::Techno;
        double bpm = 120.0;
        float swing = 0.0f;
        std::array<bool, 12> scale {};
        std::vector<int> prog;     std::vector<bool> progMinor;    // main loop (one chord per bar)
        std::vector<int> altProg;  std::vector<bool> altMinor;     // break / bridge colour
        std::vector<NoteEvent> motif;  int motifBars = 2;  bool motifFromUser = false;
        std::vector<NoteEvent> bass;   int bassBars = 1;   bool bassFromUser = false;
        std::array<float, 16> kick {}, snare {}, clap {}, chh {}, ohh {}, perc {};
        bool drumsFromUser = false;
        bool fourOnFloor = true;
        int bassOctaveBase = 31;
    };

    Palette makePalette (const MusicalDNA& dna, Flavour flavour, float creativity, juce::uint32 seed);

    // The section list for a flavour. `referenceDrop` = the user already has a drop (it stays theirs).
    std::vector<std::pair<Role, int>> defaultPlan (Flavour flavour, bool referenceDrop, int dropBars);

    Arrangement generate (const MusicalDNA& dna, Flavour flavour, float creativity, juce::uint32 seed);

    // (Re)build one section in place, keeping its role/bars/mood/lock. Transition FX are refreshed for the whole arrangement.
    void buildSection (Arrangement& arr, int index, const MusicalDNA& dna, juce::uint32 seed);
    void refreshTransitions (Arrangement& arr);

    // Insert a new section (e.g. a BRIDGE asked for in the command field) at `position`, built immediately.
    int insertSection (Arrangement& arr, int position, Role role, int bars, const MusicalDNA& dna, SectionMood mood, juce::uint32 seed);

    juce::String flavourSummary (Flavour flavour, const MusicalDNA& dna);
}
