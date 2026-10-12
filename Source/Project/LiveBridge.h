#pragma once
#include <JuceHeader.h>
#include "LiveProject.h"

// ======================================================================================
// The plugin side of the Ableton Live connection (message thread).
// Files in ~/Music/FINISH/inbox, shared with the FINISH Remote Script running inside Live:
//   bridge.json (heartbeat from Live), request.json (from FINISH), response.json, project.json.
// One request at a time. Everything is diagnosed: script installed? selected in Live? alive? which version?
class LiveBridge
{
public:
    enum class Status { NotInstalled = 0, NotSelected, LiveNotRunning, OldScript, Connected };

    struct Diagnosis
    {
        Status status = Status::NotInstalled;
        juce::String title;              // one line for the status LCD
        juce::StringArray steps;         // what to do, in order
        juce::String liveVersion, scriptVersion;
        double heartbeatAge = -1.0;      // seconds, -1 = never
        juce::StringArray installedIn;
        bool canArrange = false;         // Live 12 clip creation in the Arrangement
        bool connected() const noexcept { return status == Status::Connected; }
    };

    struct Response
    {
        bool ok = false;
        juce::String type, id, message, error;
        juce::var report;                // apply: written / skipped / manual / errors
        juce::String describe() const;   // multi-line French report
    };

    static juce::File inbox();
    Diagnosis diagnose() const;
    static juce::String linkVersusControlSurface();   // explanation for the UI

    bool busy() const noexcept { return pendingId.isNotEmpty(); }
    juce::String pendingType() const noexcept { return pendingKind; }
    double waitingSeconds() const noexcept;

    bool requestSnapshot (juce::String& error);
    bool requestApply (juce::var request, juce::String& error);       // request["id"] is set here
    bool requestUndo (const juce::String& applyId, juce::String& error);
    bool requestPing (juce::String& error);
    bool requestPlay (double beat, bool stop, juce::String& error);   // start Live's transport at `beat` (or stop it)
    void cancel() { pendingId.clear(); pendingKind.clear(); }

    // Call regularly. True when the pending request got its answer (or timed out: ok=false, error set).
    bool poll (Response& out);

    LiveProject loadProject() const { return LiveProject::fromFile (inbox().getChildFile ("project.json")); }
    const juce::String& lastApplyId() const noexcept { return lastApply; }

    double timeoutSeconds = 8.0;

private:
    bool send (juce::var request, const juce::String& kind, juce::String& error);
    juce::String pendingId, pendingKind, lastApply;
    double sentAt = 0.0;
};
