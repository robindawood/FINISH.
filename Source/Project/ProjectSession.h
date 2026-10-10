#pragma once
#include "LiveBridge.h"
#include "ProjectPlanner.h"
#include "ProjectCommand.h"

// ======================================================================================
// FINISH MY TRACK on the user's Live project (message thread): read the set through the bridge,
// analyse it, keep a plan the user can shape (level, versions, locks, cells on/off, commands),
// write it into Live on confirmation, undo it. Keeps a journal of what was analysed / generated /
// sent / left to do, shared with the mastering side.
class ProjectSession
{
public:
    enum class Phase { Idle = 0, Reading, Ready, Sending, Undoing };

    struct Entry
    {
        enum class Kind { Analysed = 0, Generated, Sent, Undone, Manual, Master, Info, Error } kind = Kind::Info;
        juce::String text;
        double time = 0.0;
    };

    // ---- connection
    LiveBridge& getBridge() noexcept { return bridge; }
    const LiveBridge::Diagnosis& getDiagnosis() const noexcept { return diag; }
    void refreshDiagnosis();

    // ---- flow
    bool readProject (juce::String& error);     // snapshot -> analysis -> plan
    void loadProject (LiveProject p);           // a set read elsewhere (tests, screenshots)
    // Call from a timer. Returns true when something visible changed.
    bool tick();
    Phase getPhase() const noexcept { return phase; }
    bool hasProject() const noexcept { return analysis.valid; }
    bool hasPlan() const noexcept { return plan.valid; }
    const LiveProject& getProject() const noexcept { return project; }
    const ProjectAnalysis& getAnalysis() const noexcept { return analysis; }
    const ProjectPlan& getPlan() const noexcept { return plan; }
    const PlanOptions& getOptions() const noexcept { return options; }
    int getVersion() const noexcept { return version; }

    bool takeFreshPlan() { const bool r = freshPlan; freshPlan = false; return r; }       // a plan from a user-initiated read
    bool takeLiveResult() { const bool r = liveResult; liveResult = false; return r; }     // an apply / undo answer arrived

    // ---- shaping the plan (nothing is written to Live)
    void setLevel (Level l);
    void setVersion (int v);                    // 0..2: the three versions of every new section
    int getVersionIndex() const noexcept { return (int) (options.seed % 3); }
    void toggleCell (int section, int track);   // switch one track off/on in one new section
    void toggleTrackLock (int track);
    bool isTrackLocked (int track) const;
    void cycleSectionVariant (int section);
    bool undoPlan();                            // "garde la version précédente"
    bool canUndoPlan() const noexcept { return ! history.empty(); }

    // A free request. Returns the answer to show (and journal); sets `wantsSend` for "envoie".
    struct Outcome { juce::String answer; bool wantsSend = false, wantsUndoSend = false, wantsMasterAnalysis = false, wantsMaster = false;
                     bool keepDark = false, keepBright = false; juce::String mixTopic; bool needsProject = false; };
    Outcome apply (const ProjectCommand& cmd);

    // ---- writing into Live
    bool send (double sampleRate, juce::String& error);
    bool undoSend (juce::String& error);
    bool hasSent() const noexcept { return bridge.lastApplyId().isNotEmpty(); }
    const juce::String& getLastReport() const noexcept { return lastReport; }
    juce::String confirmText() const;           // what "send" will do, for the confirmation box

    // ---- preview / text
    Arrangement previewArrangement() const;
    juce::String planText() const;              // the plan in words
    const std::vector<Entry>& getJournal() const noexcept { return journal; }
    void log (Entry::Kind k, const juce::String& text);
    juce::String getStatus() const;             // one line

    // Persistence of the user's choices (level, version, locks)
    juce::ValueTree toState() const;
    void fromState (const juce::ValueTree&);

private:
    void replan (bool keepHistory = true, const std::set<TrackRole>& keepOthersExcept = {});
    void applyUserEdits();

    LiveBridge bridge;
    LiveBridge::Diagnosis diag;
    double lastDiag = 0.0;
    Phase phase = Phase::Idle;
    LiveProject project;
    ProjectAnalysis analysis;
    ProjectPlan plan;
    PlanOptions options;
    std::set<std::pair<juce::String, int>> offCells;     // (section name, track)
    std::vector<std::pair<PlanOptions, ProjectPlan>> history;
    std::vector<Entry> journal;
    juce::String lastReport;
    int version = 0;
    bool autoPlanAfterRead = true;
    bool freshPlan = false, liveResult = false;
};
