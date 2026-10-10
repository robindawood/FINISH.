#include "ComposeCommand.h"
#include "../Intent/IntentParser.h"
#include "../Params.h"

namespace
{
    bool has (const juce::StringArray& w, std::initializer_list<const char*> words)
    {
        for (auto* x : words) if (w.contains (x)) return true;
        return false;
    }
    bool hasPhrase (const juce::String& clause, std::initializer_list<const char*> phrases)
    {
        const auto padded = " " + clause + " ";
        for (auto* p : phrases) if (padded.contains (" " + juce::String (p) + " ")) return true;
        return false;
    }
    bool startsWithAny (const juce::StringArray& w, std::initializer_list<const char*> stems)
    {
        for (auto& t : w) for (auto* s : stems) if (t.startsWith (s)) return true;
        return false;
    }

    // Which part of the song a clause talks about. -1 = none. Drop 2 is checked before "drop".
    int roleOf (const juce::String& c, const juce::StringArray& w, int& beforeDrop)
    {
        beforeDrop = 0;
        const bool drop2 = hasPhrase (c, { "drop 2", "drop2", "deuxieme drop", "2eme drop", "2e drop", "second drop", "seconde drop", "dernier drop",
                                           "last drop", "final drop", "drop final", "2nd drop" });
        const bool towardsDrop = hasPhrase (c, { "avant le drop", "avant le premier drop", "before the drop", "vers le drop", "into the drop", "before drop", "pour le drop" });
        if (has (w, { "build", "buildup", "montee", "riser", "tension" }) || hasPhrase (c, { "build up", "monter la tension" }))
        {
            beforeDrop = drop2 ? 2 : (towardsDrop ? 1 : 0);
            return (int) Role::Build;
        }
        if (has (w, { "transition", "transitions" }))
        {
            beforeDrop = drop2 ? 2 : 1;
            return (int) Role::Build;
        }
        if (has (w, { "bridge", "pont" })) return (int) Role::Bridge;
        if (has (w, { "break", "breakdown", "respiration", "pause" })) return (int) Role::Break;
        if (has (w, { "intro", "introduction", "debut", "ouverture" })) return (int) Role::Intro;
        if (has (w, { "outro", "fin", "ending", "conclusion", "final", "finale" }) && ! drop2) return (int) Role::Outro;
        if (drop2) return (int) Role::Drop2;
        if (has (w, { "drop", "refrain", "chorus" })) return (int) Role::Drop;
        return -1;
    }

    SectionMood moodOf (const juce::StringArray& w, const juce::String& c, juce::StringArray& tags)
    {
        SectionMood m;
        auto tag = [&] (const char* t) { tags.addIfNotAlreadyThere (utf8 (t)); };
        if (has (w, { "sombre", "sombres", "dark", "darker", "noir", "noire", "melancolique", "triste", "mineur", "inquietant" })) { m.darker = 1; tag ("SOMBRE"); }
        if (has (w, { "lumineux", "lumineuse", "bright", "brighter", "joyeux", "joyeuse", "happy", "euphorique", "solaire", "majeur" })) { m.brighter = 1; tag ("LUMINEUX"); }
        if (startsWithAny (w, { "melodi", "melody" }) || has (w, { "chantant", "lead" })) { m.melodic = 1; tag ("MÉLODIQUE"); }
        if (startsWithAny (w, { "atmo", "aerien", "planant", "ambient", "ambiance", "spacieu", "reveu", "dreamy", "airy", "cinemat" })) { m.atmos = 1; tag ("ATMO"); }
        if (has (w, { "intense", "intensite", "energique", "energetic", "agressif", "aggressive", "violent", "hard", "explosif", "explosive",
                      "percutant", "percutante", "punchy", "punch", "puissant", "powerful", "fort", "forte", "harder", "bigger", "energie", "energy" })
            || hasPhrase (c, { "plus fort", "tape plus", "hit harder" })) { m.intense = 1; tag ("INTENSE"); }
        if (startsWithAny (w, { "progressi", "graduel", "gradual", "lentement", "slowly", "evolutif" })) { m.progressive = 1; tag ("PROGRESSIF"); }
        if (has (w, { "minimal", "minimale", "minimaux", "minimaliste", "epure", "epuree", "sobre", "simple", "stripped" })) { m.minimal = 1; tag ("MINIMAL"); }
        return m;
    }
}

ComposeCommand ComposeCommand::parse (const juce::String& text)
{
    ComposeCommand cmd;
    const auto norm = IntentParser::normalise (text);
    juce::StringArray all;
    all.addTokens (norm, " |", "");
    all.removeEmptyStrings();
    if (all.isEmpty()) return cmd;
    const auto flat = all.joinIntoString (" ");

    // ---- the whole job
    if (hasPhrase (flat, { "finish my track", "finish the track", "finish my song", "finis mon morceau", "finis ma track", "finis mon son", "finir mon morceau",
                           "finir ma track", "termine mon morceau", "termine ma track", "terminer mon morceau", "termine le morceau", "complete mon morceau",
                           "complete ma track", "complete the track", "finish it", "acheve mon morceau", "finalise mon morceau", "finalise ma track" })
        || (all.size() <= 3 && has (all, { "finish", "finis", "termine", "terminer", "finir", "complete", "finalise" })))
    {
        cmd.finishTrack = true;
        cmd.understood.add ("FINISH MY TRACK");
    }

    // ---- mastering
    const bool masterWord = has (all, { "master", "mastering", "masterise", "masteriser", "masterize", "matrise", "mastere" });
    if (masterWord || has (all, { "spotify", "streaming", "youtube", "deezer", "applemusic" }))
    {
        int t = 2;
        if (has (all, { "spotify", "streaming", "youtube", "deezer", "applemusic", "doux", "dynamique", "dynamic" })) t = 1;
        else if (has (all, { "club", "dj", "festival", "boite" })) t = 3;
        else if (has (all, { "fort", "loud", "louder", "max", "maximum", "agressif" })) t = 4;
        cmd.masterTarget = t;
        cmd.understood.add ("MASTER " + juce::String (loudnessTargets[t].shortName));
    }

    // ---- proposals
    if (has (all, { "fidele", "faithful", "proche" })) cmd.flavour = (int) Flavour::Faithful;
    if (hasPhrase (flat, { "version atmospherique", "version atmo", "plus atmospherique", "more atmospheric", "version aerienne" })) cmd.flavour = (int) Flavour::Atmospheric;
    int unusedBefore = 0;
    if (hasPhrase (flat, { "version intense", "plus intense", "more intense", "version energique", "plus energique" }) && roleOf (flat, all, unusedBefore) < 0)
        cmd.flavour = (int) Flavour::Intense;
    if (startsWithAny (all, { "creati", "origin", "surpren", "experiment", "fou", "audaci", "osee" })) { cmd.creativityDelta += 0.3f; cmd.understood.add (utf8 ("+CRÉATIF")); }
    if (has (all, { "subtil", "subtile", "subtle", "discret", "discrete", "leger", "legere", "sobre" })) { cmd.creativityDelta -= 0.25f; cmd.understood.add ("SUBTIL"); }
    if (hasPhrase (flat, { "autre arrangement", "nouvel arrangement", "new arrangement", "another arrangement", "autres propositions", "propose moi",
                           "proposes moi", "genere un arrangement", "genere l arrangement", "regenere tout", "refais tout", "autre version", "another version" }))
    {
        cmd.regenerateAll = true;
        cmd.understood.add ("NOUVEL ARRANGEMENT");
    }
    if (cmd.flavour >= 0) cmd.understood.add (juce::String ("VERSION ") + flavourName ((Flavour) cmd.flavour));

    // ---- clause by clause: sections, actions, moods
    juce::StringArray clauses;
    {
        juce::String cur;
        for (auto& t : all)
        {
            if (t == "|" || t == "et" || t == "and" || t == "puis" || t == "then" || t == "ensuite" || t == "aussi" || t == "also")
            {
                if (cur.isNotEmpty()) clauses.add (cur.trim());
                cur = {};
            }
            else cur << t << " ";
        }
        if (cur.isNotEmpty()) clauses.add (cur.trim());
        // "intro courte et minimale": a clause with only adjectives belongs to the previous one.
        for (int i = clauses.size() - 1; i > 0; --i)
        {
            juce::StringArray w; w.addTokens (clauses[i], " ", ""); w.removeEmptyStrings();
            int bd = 0;
            const bool hasRole = roleOf (clauses[i], w, bd) >= 0;
            const bool hasVerb = startsWithAny (w, { "cree", "crea", "create", "ajout", "add", "gener", "fais", "faire", "make", "rends", "rend", "refai", "change" });
            if (! hasRole && ! hasVerb && w.size() <= 3)
            {
                clauses.set (i - 1, clauses[i - 1] + " " + clauses[i]);
                clauses.remove (i);
            }
        }
    }
    for (auto& clause : clauses)
    {
        juce::StringArray w; w.addTokens (clause, " ", ""); w.removeEmptyStrings();
        int beforeDrop = 0;
        const int role = roleOf (clause, w, beforeDrop);
        const bool createVerb = startsWithAny (w, { "cree", "crea", "create", "ajout", "add", "gener", "fais", "faire", "make", "compos", "ecri", "write",
                                                    "invente", "construi", "build", "insere", "insert", "mets", "met", "donne", "propos", "imagine" });
        const bool reworkVerb = startsWithAny (w, { "refai", "regener", "change", "remplace", "redo", "rework", "retravaille", "ameliore", "improve",
                                                    "rends", "rend", "make", "varie", "vary", "modifie" });
        juce::StringArray tags;
        auto mood = moodOf (w, clause, tags);
        if (has (w, { "long", "longue", "longer", "16" })) tags.add ("16 MES.");
        if (has (w, { "court", "courte", "short", "shorter", "8" })) tags.add ("8 MES.");

        // sound wish in the same clause ("plus percutant", "plus large")
        const auto sound = IntentParser::parse (clause);
        const bool soundy = ! sound.intent.isEmpty();

        if (role < 0)
        {
            if (soundy) cmd.soundRequest = true;
            continue;
        }
        const auto r = (Role) role;
        if ((r == Role::Drop) && soundy && ! createVerb)
        {
            // "rends le drop plus percutant": processing on the drop + (if the groove is sparse) accents.
            cmd.soundRequest = true;
            cmd.soundFocus = "DROP";
        }
        if (! createVerb && ! reworkVerb && ! mood.any() && ! soundy) continue;
        Action a;
        a.role = r;
        a.create = createVerb && ! (r == Role::Drop || r == Role::Drop2);
        a.mood = mood;
        if (soundy && sound.intent.punch > 0.0f) a.mood.intense = std::max (a.mood.intense, 1.0f);
        a.beforeDrop = beforeDrop;
        a.bars = tags.contains ("16 MES.") ? 16 : (tags.contains ("8 MES.") ? 8 : (mood.progressive > 0 ? 16 : 0));
        a.label = juce::String (roleName (r)) + (beforeDrop == 2 ? utf8 (" → DROP 2") : (beforeDrop == 1 ? utf8 (" → DROP") : juce::String()));
        for (auto& t : tags) a.label << " " << t;
        cmd.actions.push_back (a);
        cmd.understood.add ((a.create ? "+ " : "~ ") + a.label);
    }
    return cmd;
}
