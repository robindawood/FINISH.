#pragma once
#include "ComposeTypes.h"

// Free-text requests about the music itself (French or English), e.g.
//   "finis mon morceau", "crée un bridge mélodique sombre", "ajoute un build-up avant le drop",
//   "crée un outro progressif", "rends le drop plus percutant et crée une transition plus intense vers le deuxième drop",
//   "masterise pour le club".
// Sound-only requests ("plus large", "trop de basses") are left to IntentParser.
struct ComposeCommand
{
    struct Action
    {
        Role role = Role::Build;
        bool create = true;            // create / add (insert if missing) vs. rework the existing one
        SectionMood mood;
        int bars = 0;                  // 0 = default
        int beforeDrop = 0;            // 1 = before the first drop, 2 = before drop 2, 0 = natural place
        juce::String label;            // what we understood, for the UI
    };

    bool finishTrack = false;          // the whole job: analyse, arrange, master
    bool regenerateAll = false;        // "propose / génère un (autre) arrangement"
    int flavour = -1;                  // -1 = keep
    float creativityDelta = 0.0f;
    int masterTarget = -1;             // index into loudnessTargets (-1 = not asked)
    bool soundRequest = false;         // also contains a mix/master wish (punch, width...) for IntentParser
    juce::String soundFocus;           // section the sound wish is about ("DROP"), empty = whole track
    std::vector<Action> actions;
    juce::StringArray understood;      // short chips for the UI

    bool isEmpty() const noexcept { return ! finishTrack && ! regenerateAll && flavour < 0 && actions.empty() && masterTarget < 0 && std::abs (creativityDelta) < 1.0e-6f; }

    static ComposeCommand parse (const juce::String& text);
};
