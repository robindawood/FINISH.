#pragma once
#include "ComposeTypes.h"
#include "ComposeCommand.h"
#include "ArrangementGenerator.h"
#include "ArrangementExport.h"

// FINISH COMPOSE, message-thread side: the musical DNA of what the user played, three arrangement
// proposals (FIDELE / ATMO / INTENSE), edits (regenerate, lock, insert, moods from commands), undo,
// export and the Ableton Live hand-over.
class Composer
{
public:
    // ---- analysis result
    void setDNA (const MusicalDNA& dna, int userBarOfMaterial, juce::uint32 seed);
    bool hasDNA() const noexcept { return dnaState.valid; }
    const MusicalDNA& getDNA() const noexcept { return dnaState; }
    int getUserBar() const noexcept { return userBar; }

    // ---- proposals
    bool hasArrangement() const noexcept { return dnaState.valid && ! proposals[(size_t) current].empty(); }
    const Arrangement& getArrangement() const noexcept { return proposals[(size_t) current]; }
    const Arrangement& getProposal (Flavour f) const noexcept { return proposals[(size_t) f]; }
    Flavour getFlavour() const noexcept { return (Flavour) current; }
    float getCreativity() const noexcept { return creativityValue; }
    int getVersion() const noexcept { return version; }

    void generateProposals (juce::uint32 seed);
    void selectFlavour (Flavour f);
    void setCreativity (float c);
    void regenerateSection (int index);
    void applyToSection (int index, const SectionMood& mood, int bars);   // mood moves from the arrangement view
    void toggleLock (int index);
    void overrideKey (int root, bool minor);
    void overrideStyle (Style s);
    void overrideTempo (double bpm);
    bool undo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }

    struct CommandResult
    {
        juce::StringArray done;        // what changed, one short line each
        bool needsMaterial = false;    // the command needs an analysis first
        int masterTarget = -1;
        bool soundRequest = false;
        juce::String soundFocus;
        bool finishTrack = false;
    };
    CommandResult apply (const ComposeCommand& cmd);

    // ---- placement in the DAW: bar (0-based) where the arrangement's bar 1 goes so that its DROP
    // lines up with where the user's own material sits in their set.
    int suggestedStartBar() const;

    // ---- export / Live
    ArrangementExport::Result exportAll (double sampleRate);
    const ArrangementExport::Result& getLastExport() const noexcept { return lastExport; }
    bool sendToLive (double sampleRate, juce::String& message);
    // Returns true when the Live status changed.
    bool pollLive();
    const juce::String& getLiveStatus() const noexcept { return liveStatus; }
    bool isWaitingForLive() const noexcept { return liveRequest.isNotEmpty(); }
    bool getLiveOk() const noexcept { return liveOk; }

    // ---- state
    juce::ValueTree toState() const;
    void fromState (const juce::ValueTree& v);
    static juce::ValueTree dnaToTree (const MusicalDNA& d);
    static MusicalDNA dnaFromTree (const juce::ValueTree& v);

private:
    void pushUndo();
    void touched() { ++version; }
    int findSection (Role role, int beforeDrop) const;
    int insertPositionFor (Role role, int beforeDrop) const;
    void rebuildUnlocked (Arrangement& arr, bool newSeeds);

    MusicalDNA dnaState;
    int userBar = 0;
    std::array<Arrangement, numFlavours> proposals;
    int current = 0;
    float creativityValue = 0.35f;
    juce::uint32 seedCounter = 1;
    std::vector<std::pair<std::array<Arrangement, numFlavours>, int>> undoStack;
    int version = 0;

    ArrangementExport::Result lastExport;
    int lastExportVersion = -1;
    juce::String liveRequest, liveStatus;
    bool liveOk = false;
    double liveSentAt = 0.0;
};
