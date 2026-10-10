#pragma once
#include "ComposeTypes.h"

// Getting the generated arrangement into the DAW:
//   * files: one multitrack MIDI file of the whole arrangement, one MIDI file per section and part,
//     one WAV per section for the transition FX, and a short text guide (all named INTRO, BUILD, DROP 2...);
//   * Ableton Live: a Remote Script (installed by FINISH) that creates new tracks and clips in the open set.
namespace ArrangementExport
{
    juce::File defaultRoot();                 // ~/Music/FINISH
    juce::File liveInbox();                   // ~/Music/FINISH/inbox (shared with the Live Remote Script)

    struct Result
    {
        bool ok = false;
        juce::File folder, fullMidi, guide;
        juce::Array<juce::File> files;
        std::map<juce::String, juce::File> clipFiles;    // key "section:lane"
        juce::String error;
    };

    // Everything in a new folder. Reference (user's own) sections export only their FX lane.
    Result exportAll (const Arrangement& arr, const MusicalDNA& dna, const juce::File& folder, double sampleRate);

    // Single pieces (for drag and drop).
    juce::File writeClipMidi (const Arrangement& arr, int section, Lane lane, const juce::File& folder);
    juce::File writeSectionFx (const Arrangement& arr, int section, const juce::File& folder, double sampleRate);
    juce::File writeFullMidi (const Arrangement& arr, const juce::File& file, bool includeReference);
    juce::String fileStem (const Arrangement& arr, int section, const juce::String& part);

    // ---- Ableton Live bridge -------------------------------------------------------------
    // Installs/updates the Remote Script in every Live User Library found. Returns the folders written.
    juce::Array<juce::File> installLiveScript (juce::String& report);
    // Writes the request for the Remote Script. `startBar` = where the arrangement's bar 1 lands in the set.
    bool sendToLive (const Arrangement& arr, const MusicalDNA& dna, const Result& exported, int startBar,
                     bool loadInstruments, juce::String& requestId, juce::String& error);
    // Reads the Remote Script's answer for `requestId`. Returns false while there is none yet.
    bool readLiveResponse (const juce::String& requestId, bool& ok, juce::String& message, juce::StringArray& warnings);
    bool isBridgeAlive (double withinSeconds = 1.0e9);   // the script has started at least once

    juce::String buildRequestJson (const Arrangement& arr, const MusicalDNA& dna, const Result& exported, int startBar,
                                   bool loadInstruments, const juce::String& requestId);
}
