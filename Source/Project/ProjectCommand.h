#pragma once
#include "ProjectPlanner.h"

// ======================================================================================
// A free request (French or English) about the user's own project, e.g.
//   "termine mon morceau sans toucher à ma mélodie principale"
//   "fais monter progressivement l'énergie pendant les 16 prochaines mesures"
//   "rends mon drop plus puissant sans modifier la ligne de basse"
//   "ajoute des variations de drums en conservant mon groove"
//   "crée une transition plus fluide entre le break et le drop"
//   "analyse mon mix et explique-moi pourquoi il manque de punch"
//   "améliore le mastering en conservant le caractère sombre"
//   "rends cette basse plus agressive"
//   "propose trois versions différentes de mon build-up"
//   "écoute et analyse ce que j'ai déjà créé, puis dis-moi ce qui manque"
//   then: "plus subtil", "plus sombre", "garde la version précédente", "modifie uniquement la basse", "envoie dans Live"...
struct ProjectCommand
{
    enum class Kind { None = 0, Finish, Sections, Rise, Variation, AnalyseProject, AnalyseMix, Master, Iterate, Send, UndoSend, KeepPrevious };
    Kind kind = Kind::None;

    std::set<TrackRole> lockRoles;       // "sans toucher à ..."
    std::set<TrackRole> onlyRoles;       // "uniquement ..." / the subject of a variation request
    std::vector<SecKind> sections;
    int rangeBars = 0;
    int versions = 1;
    int level = -1;                      // Level, -1 = keep
    int levelDelta = 0;                  // "plus subtil" -1, "plus poussé" +1
    SectionMood mood;
    bool keepDark = false, keepBright = false;   // mastering character
    bool masterAlso = false;             // the request also asks for mastering
    juce::String mixTopic;               // "punch", "clarte"...
    juce::StringArray understood;        // chips for the UI
    juce::StringArray notes;             // honest remarks (what cannot be done as asked, and what FINISH does instead)

    bool isEmpty() const noexcept { return kind == Kind::None; }
    static ProjectCommand parse (const juce::String& text);
};
