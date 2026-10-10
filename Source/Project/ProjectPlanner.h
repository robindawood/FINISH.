#pragma once
#include "ProjectAnalyzer.h"
#include "../Compose/ComposeTypes.h"
#include <set>

// ======================================================================================
// FINISH MY TRACK on the user's own Live set: a plan of the sections to add, built ONLY from the user's
// tracks and clips (exact copies, or variations of their own notes), shown before anything is written.

enum class Level { Complete = 0, Enrich, Improve, Finalise };
constexpr int numLevels = 4;
const char* levelName (Level l);          // "COMPLETER", ...
juce::String levelHelp (Level l);

enum class Tx { Copy = 0, KickOnly, NoKick, NoSnare, HatsOnly, Thin, Fill, BuildRoll, CutEnd, Roots, Sustain, Stabs,
                Tease, Sparse, Answer, Augment, OctUp, OctDown, FadeIn, FadeOut };
juce::String txLabel (Tx t);              // short French label ("copie", "roulement"...)

struct PlanPart
{
    int startBar = 0, bars = 0;           // within the section
    std::vector<Tx> tx;                   // {Copy} = exact copy of the user's clip
    std::vector<LiveNote> notes;          // generated (relative to the part start), MIDI only
    bool exactCopy() const noexcept { return tx.size() == 1 && tx[0] == Tx::Copy; }
};

struct PlanCell
{
    int track = -1;
    std::vector<PlanPart> parts;
    bool locked = false;                  // only exact copies (a constraint from the user)
    bool off = false;                     // the user switched it off in the plan
    bool empty() const noexcept { return off || parts.empty(); }
    juce::String summary() const;         // "copie + roulement"
};

struct PlanSection
{
    SecKind kind = SecKind::Unknown;
    juce::String name;
    int startBar = 0, bars = 0;           // absolute bars in the set (0-based)
    bool existing = false;                // already in the user's set (display only)
    float energy = 0.0f;
    int variant = 0;
    std::vector<PlanCell> cells;          // one per project track (index = track)
    std::vector<FxEvent> fx;              // rendered to audio on a new FINISH FX track (Improve+)
    juce::String recipe;
};

struct PlanLayer                          // notes layered over the user's existing bars, on a copy of their track
{
    int track = -1;
    int startBar = 0, bars = 1;
    std::vector<LiveNote> notes;
    juce::String what;
};

struct PlanOptions
{
    Level level = Level::Enrich;
    enum class Placement { Auto = 0, After, FullVersion } placement = Placement::Auto;
    std::set<int> lockedTracks;           // "sans toucher à ..."
    std::set<TrackRole> lockedRoles;
    std::set<TrackRole> onlyRoles;        // "uniquement la basse" (others: exact copies)
    std::vector<SecKind> onlySections;    // build only these (e.g. a Build between break and drop)
    int rangeBars = 0;                    // "pendant les 16 prochaines mesures" -> one rising section
    std::array<int, 10> variants {};      // per SecKind
    SectionMood mood;
    juce::uint32 seed = 0;              // 0..2 = version 1..3
};

struct ProjectPlan
{
    bool valid = false;
    Level level = Level::Enrich;
    bool fullVersion = false;
    int firstNewBar = 0;
    std::vector<PlanSection> sections;    // existing ones first (display), then the new ones
    std::vector<PlanLayer> layers;
    juce::StringArray explain;            // what the plan does and why
    juce::StringArray manual;             // what the user will have to do
    double bpm = 120.0, beatsPerBar = 4.0;

    int newSectionCount() const noexcept;
    int lastBar() const noexcept;
    int countWrites() const noexcept;     // clips that would be written
};

namespace ProjectPlanner
{
    ProjectPlan makePlan (const LiveProject& p, const ProjectAnalysis& a, const PlanOptions& o);
    // Re-renders the notes of one cell / every cell after an edit (lock, off...).
    void renderNotes (ProjectPlan& plan, const LiveProject& p, const ProjectAnalysis& a);

    // The request for the bridge. FX files (one WAV per section with FX) are written into `fxFolder`.
    juce::var buildApplyRequest (const ProjectPlan& plan, const LiveProject& p, const ProjectAnalysis& a, const juce::String& id,
                                 const juce::File& fxFolder, double sampleRate, juce::StringArray& notes);

    // An approximate preview with FINISH's built-in sounds (lanes by track role).
    Arrangement toPreview (const ProjectPlan& plan, const LiveProject& p, const ProjectAnalysis& a, bool newOnly = true);

    // Note transforms (exposed for tests).
    std::vector<LiveNote> transform (std::vector<LiveNote> material, const std::vector<Tx>& tx, double partBeats,
                                     const TrackProfile& prof, double materialLoop, double bpb, int keyRoot, bool minor);
}
