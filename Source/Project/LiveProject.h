#pragma once
#include <JuceHeader.h>
#include <vector>

// ======================================================================================
// The user's Ableton Live set, as read by the FINISH Remote Script (project.json).
// Times are in beats (quarter notes). Arrangement clip times are song positions;
// note times are relative to the clip's content (Live's clip time).

struct LiveNote
{
    int pitch = 60;
    double start = 0.0, length = 0.25;
    int velocity = 100;    // 1..127
};

struct LiveClip
{
    bool session = false;
    int index = 0;                 // arrangement index or session slot
    juce::String name;
    bool midi = true;
    double start = 0.0, end = 0.0; // song time (arrangement); 0..length for session clips
    double length = 0.0;
    double loopStart = 0.0, loopEnd = 0.0;
    bool looping = true, muted = false;
    std::vector<LiveNote> notes;   // MIDI only, clip time
    juce::String file;             // audio only

    double loopLength() const noexcept { return juce::jmax (0.25, loopEnd - loopStart); }
    double duration() const noexcept   { return session ? loopLength() : juce::jmax (0.0, end - start); }
};

struct LiveDevice
{
    juce::String name, className;
    std::vector<std::pair<int, juce::String>> pads;   // drum racks: note -> pad name
    bool isDrumRack() const noexcept { return className == "DrumGroupDevice"; }
    bool isInstrument() const noexcept;
};

enum class TrackRole { Drums = 0, Perc, Bass, Chords, Lead, Pad, Fx, Vocal, Audio, Other };
constexpr int numTrackRoles = 10;
const char* trackRoleName (TrackRole r);           // "DRUMS", ...
juce::String trackRoleLabel (TrackRole r);         // French label for the UI

struct LiveTrack
{
    int index = 0;
    juce::String name;
    bool midi = true, mute = false, group = false, grouped = false, frozen = false, selected = false;
    std::vector<LiveDevice> devices;
    std::vector<LiveClip> arrangement, session;

    // Filled by the analysis
    TrackRole role = TrackRole::Other;
    float roleConfidence = 0.0f;
    juce::String roleWhy;

    juce::String instrumentName() const;
    bool hasDrumRack() const;
    bool hasContent() const noexcept { return ! arrangement.empty() || ! session.empty(); }
};

struct LiveProject
{
    bool valid = false;
    juce::String bridgeVersion, liveVersion, error;
    double readTime = 0.0;          // seconds since epoch (as written by the script)
    struct Caps { bool arrangementRead = false, createMidiClip = false, createAudioClip = false, duplicateToArrangement = false,
                       deleteClip = false, undoStep = false; } caps;

    double tempo = 120.0;
    int sigNum = 4, sigDen = 4;
    int rootNote = -1;              // Live 12 scale, -1 = unknown
    juce::String scale;
    double songLength = 0.0;
    double loopStart = 0.0, loopLength = 0.0;
    bool loopOn = false;
    double position = 0.0;
    std::vector<std::pair<juce::String, double>> cues;
    juce::StringArray scenes;
    int selectedTrack = -1;
    std::vector<LiveTrack> tracks;

    double beatsPerBar() const noexcept { return sigNum * 4.0 / juce::jmax (1, sigDen); }
    double arrangementEnd() const noexcept;     // last arrangement clip end (beats)
    int countClips (bool arrangementOnly) const noexcept;
    int countNotes() const noexcept;

    static LiveProject fromJson (const juce::var& v);
    static LiveProject fromFile (const juce::File& f);
};

// ---- note helpers
namespace LiveNotes
{
    // Notes an Arrangement clip plays in song time [from, to), returned relative to `from` (loops unfolded).
    std::vector<LiveNote> playedIn (const LiveClip& c, double from, double to);
    // The clip's loop content tiled over `lengthBeats` starting at its loop start (material for new parts).
    std::vector<LiveNote> tiled (const LiveClip& c, double lengthBeats);
    // Notes of one loop cycle, relative to the loop start.
    std::vector<LiveNote> loopContent (const LiveClip& c);
}
