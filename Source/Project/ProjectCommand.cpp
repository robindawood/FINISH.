#include "ProjectCommand.h"
#include "../Intent/IntentParser.h"

namespace
{
    struct Text
    {
        juce::StringArray w;
        juce::String flat;   // " a b c "
        bool phrase (std::initializer_list<const char*> ps) const
        {
            for (auto* p : ps) if (flat.contains (" " + juce::String (p) + " ")) return true;
            return false;
        }
        bool word (std::initializer_list<const char*> ws) const
        {
            for (auto* x : ws) if (w.contains (x)) return true;
            return false;
        }
        bool stem (std::initializer_list<const char*> ss) const
        {
            for (auto& t : w) for (auto* s : ss) if (t.startsWith (s)) return true;
            return false;
        }
        int indexOfPhrase (const char* p) const
        {
            juce::StringArray pw;
            pw.addTokens (p, " ", "");
            for (int i = 0; i + pw.size() <= w.size(); ++i)
            {
                bool ok = true;
                for (int k = 0; k < pw.size() && ok; ++k) ok = w[i + k] == pw[k];
                if (ok) return i + pw.size();
            }
            return -1;
        }
    };

    bool roleOfWord (const juce::String& t, TrackRole& r)
    {
        static const std::pair<const char*, TrackRole> words[] = {
            { "melodie", TrackRole::Lead }, { "melodies", TrackRole::Lead }, { "melody", TrackRole::Lead }, { "lead", TrackRole::Lead },
            { "theme", TrackRole::Lead }, { "hook", TrackRole::Lead }, { "motif", TrackRole::Lead }, { "topline", TrackRole::Lead },
            { "basse", TrackRole::Bass }, { "bass", TrackRole::Bass }, { "bassline", TrackRole::Bass }, { "sub", TrackRole::Bass },
            { "drums", TrackRole::Drums }, { "drum", TrackRole::Drums }, { "batterie", TrackRole::Drums }, { "rythmique", TrackRole::Drums },
            { "beat", TrackRole::Drums }, { "kick", TrackRole::Drums }, { "percussions", TrackRole::Perc }, { "percs", TrackRole::Perc },
            { "accords", TrackRole::Chords }, { "chords", TrackRole::Chords }, { "harmonie", TrackRole::Chords }, { "harmony", TrackRole::Chords },
            { "piano", TrackRole::Chords }, { "keys", TrackRole::Chords }, { "nappe", TrackRole::Pad }, { "nappes", TrackRole::Pad },
            { "pad", TrackRole::Pad }, { "pads", TrackRole::Pad }, { "voix", TrackRole::Vocal }, { "vocal", TrackRole::Vocal },
            { "vocals", TrackRole::Vocal }, { "vox", TrackRole::Vocal }, { "chant", TrackRole::Vocal } };
        for (auto& [w, role] : words) if (t == w) { r = role; return true; }
        return false;
    }

    // Roles named in the few words after a marker ("sans toucher à ma mélodie principale").
    std::set<TrackRole> rolesAfter (const Text& t, std::initializer_list<const char*> markers, int span = 6)
    {
        std::set<TrackRole> out;
        for (auto* m : markers)
        {
            int i = t.indexOfPhrase (m);
            while (i >= 0)
            {
                for (int k = i; k < juce::jmin (t.w.size(), i + span); ++k)
                {
                    if (t.w[k] == "|" || t.w[k] == "puis" || t.w[k] == "then" || t.w[k] == "mais" || t.w[k] == "but") break;
                    TrackRole r;
                    if (roleOfWord (t.w[k], r)) { out.insert (r); if (r == TrackRole::Drums) out.insert (TrackRole::Perc); }
                }
                // next occurrence
                juce::StringArray rest;
                for (int k = i; k < t.w.size(); ++k) rest.add (t.w[k]);
                Text sub { rest, " " + rest.joinIntoString (" ") + " " };
                const int j = sub.indexOfPhrase (m);
                i = j >= 0 ? i + j : -1;
            }
        }
        return out;
    }

    juce::String roleListLabel (const std::set<TrackRole>& roles)
    {
        juce::StringArray s;
        for (auto r : roles) if (r != TrackRole::Perc || ! roles.count (TrackRole::Drums)) s.add (trackRoleLabel (r).toUpperCase());
        return s.joinIntoString (", ");
    }
}

ProjectCommand ProjectCommand::parse (const juce::String& raw)
{
    ProjectCommand c;
    const auto norm = IntentParser::normalise (raw.replaceCharacter ('\'', ' ').replace (juce::String::charToString (0x2019), " ").replace ("-", " "));
    Text t;
    t.w.addTokens (norm, " ", "");
    t.w.removeEmptyStrings();
    if (t.w.isEmpty()) return c;
    t.flat = " " + t.w.joinIntoString (" ") + " ";
    auto chip = [&] (const juce::String& s) { c.understood.addIfNotAlreadyThere (s); };

    // ---- constraints, scope
    c.lockRoles = rolesAfter (t, { "sans toucher", "sans modifier", "sans changer", "sans toucher a", "en gardant", "en conservant", "garde", "conserve",
                                   "without touching", "without changing", "keep", "keeping", "dont touch", "dont change", "ne touche pas", "ne change pas", "touche pas" });
    c.onlyRoles = rolesAfter (t, { "uniquement", "seulement", "only", "juste", "just" }, 4);
    for (auto r : c.onlyRoles) c.lockRoles.erase (r);
    if (! c.lockRoles.empty()) chip (utf8 ("SANS TOUCHER : ") + roleListLabel (c.lockRoles));
    if (! c.onlyRoles.empty()) chip (utf8 ("UNIQUEMENT : ") + roleListLabel (c.onlyRoles));

    // ---- numbers
    for (int i = 0; i + 1 < t.w.size(); ++i)
    {
        const auto n = t.w[i].getIntValue();
        const bool barsWord = t.w[i + 1] == "mesures" || t.w[i + 1] == "bars" || t.w[i + 1] == "mesure"
                           || (i + 2 < t.w.size() && (t.w[i + 1] == "prochaines" || t.w[i + 1] == "next" || t.w[i + 1] == "dernieres")
                               && (t.w[i + 2] == "mesures" || t.w[i + 2] == "bars"));
        if (n > 0 && n <= 128 && barsWord) c.rangeBars = n;
    }
    if (t.phrase ({ "trois versions", "3 versions", "three versions", "trois propositions", "3 propositions", "plusieurs versions", "trois variantes",
                    "3 variantes", "three options", "trois idees", "3 idees" })) c.versions = 3;

    // ---- moods
    if (t.word ({ "sombre", "dark", "darker", "noir", "noire", "melancolique" })) c.mood.darker = 1;
    if (t.word ({ "lumineux", "lumineuse", "bright", "brighter", "joyeux", "solaire" })) c.mood.brighter = 1;
    if (t.word ({ "puissant", "puissante", "powerful", "intense", "energique", "agressif", "agressive", "aggressive", "percutant", "percutante",
                  "punchy", "bigger", "harder", "violent" })) c.mood.intense = 1;
    if (t.word ({ "subtil", "subtile", "subtle", "subtler", "discret", "discrete", "leger", "legere", "minimal", "minimale", "sobre", "epure" })) c.mood.minimal = 1;
    if (t.word ({ "fluide", "smooth", "smoother", "doux", "douce", "progressif", "progressive", "progressivement", "gradually" })) c.mood.progressive = 1;

    const bool masterWord = t.stem ({ "master", "masteris", "masteriz" });
    const bool keepVerb = t.stem ({ "conserv", "gard", "keep", "preserv" });
    c.keepDark = masterWord && keepVerb && c.mood.darker > 0;
    c.keepBright = masterWord && keepVerb && c.mood.brighter > 0;

    // ---- kinds, by priority
    if (t.phrase ({ "annule l envoi", "annule lenvoi", "retire de live", "enleve de live", "undo send", "annule ce que tu as ecrit", "annule dans live",
                    "retire ce que tu as ecrit", "enleve ce que tu as ecrit" }))
    { c.kind = Kind::UndoSend; chip (utf8 ("ANNULER L'ENVOI")); return c; }
    if (t.phrase ({ "quels sons", "quels instruments", "mes instruments", "mes sons", "inventaire", "liste les sons", "liste mes sons",
                    "quels presets", "mes presets", "quels plugins", "mes plugins", "what sounds", "which instruments", "sound inventory" }))
    { c.kind = Kind::Inventory; chip (utf8 ("SONS DU PROJET")); return c; }
    if (t.phrase ({ "ecoute dans live", "ecouter dans live", "fais moi ecouter", "joue le dans live", "play it in live", "listen in live",
                    "fais ecouter", "lance la lecture" }))
    { c.kind = Kind::Listen; chip (utf8 ("ÉCOUTER DANS LIVE")); return c; }
    if (t.phrase ({ "envoie", "envoie le", "envoie dans live", "envoyer dans live", "send to live", "send it", "ecris dans live", "ecris le dans live",
                    "applique dans live", "mets le dans live", "pose le dans live", "go", "valide", "applique" }) && ! t.word ({ "analyse", "analyze" }))
    { c.kind = Kind::Send; chip (utf8 ("ENVOYER DANS LIVE")); return c; }
    if (t.phrase ({ "garde la version precedente", "version precedente", "la precedente", "reviens a la version", "reviens en arriere", "previous version",
                    "go back", "keep the previous", "annule", "undo" }))
    { c.kind = Kind::KeepPrevious; chip (utf8 ("VERSION PRÉCÉDENTE")); return c; }
    if (t.phrase ({ "ce qui manque", "ce qu il manque", "qu est ce qui manque", "whats missing", "what is missing", "analyse ce que", "analyse mon projet",
                    "analyse mon morceau", "analyse ma track", "analyse le projet", "ecoute et analyse", "analyze my project", "analyse my project",
                    "analyze my track", "que penses tu de mon morceau", "lis mon projet", "regarde mon projet" }))
    { c.kind = Kind::AnalyseProject; chip (utf8 ("ANALYSE DU PROJET")); return c; }
    const bool mixWords = t.word ({ "mix", "mixage", "son", "sound", "punch", "clarte", "clarity", "largeur", "width", "basses", "aigus", "boueux", "muddy",
                                    "dynamique", "dynamics", "volume", "fort", "loud", "faible", "plat", "flat", "stereo", "mono" });
    if ((t.word ({ "pourquoi", "why", "explique", "explain", "expliquer" }) && mixWords) || t.phrase ({ "analyse mon mix", "analyse le mix", "analyze my mix",
                                                                                                   "analyse mon son", "check my mix" }))
    {
        c.kind = Kind::AnalyseMix;
        if (t.word ({ "punch", "percutant", "patate" })) c.mixTopic = "punch";
        else if (t.word ({ "clarte", "clarity", "boueux", "muddy", "clair" })) c.mixTopic = "clarte";
        else if (t.word ({ "largeur", "width", "stereo", "large" })) c.mixTopic = "largeur";
        else if (t.word ({ "fort", "loud", "volume", "faible" })) c.mixTopic = "volume";
        chip (utf8 ("ANALYSE DU MIX") + (c.mixTopic.isNotEmpty() ? " : " + c.mixTopic.toUpperCase() : juce::String()));
        return c;
    }

    const bool finishWords = t.phrase ({ "termine mon morceau", "termine ma track", "termine le morceau", "termine mon son", "termine mon titre", "finis mon morceau",
                                         "finis ma track", "finir mon morceau", "finish my track", "finish the track", "finish my song", "complete mon morceau",
                                         "complete ma track", "acheve mon morceau", "finalise mon morceau", "finalise ma track", "finalise tout", "continue mon morceau",
                                         "continue ma track", "continue the track", "termine le", "finis le", "termine" , "finis", "finish" });
    const bool riseWords = t.phrase ({ "monter l energie", "monter progressivement l energie", "monte l energie", "monter lenergie", "build energy",
                                       "energy rise", "raise the energy", "faire monter", "fais monter", "montee d energie", "monter la tension" })
                        || (c.rangeBars > 0 && t.word ({ "energie", "energy", "tension", "intensite" }));
    std::vector<SecKind> secs;
    auto addSec = [&] (SecKind k) { if (std::find (secs.begin(), secs.end(), k) == secs.end()) secs.push_back (k); };
    if (t.phrase ({ "entre le break et le drop", "between the break and the drop", "avant le drop", "before the drop", "vers le drop" })
        || t.word ({ "build", "buildup", "montee", "transition", "transitions", "riser" }) || t.phrase ({ "build up" }))
        addSec (SecKind::Build);
    if (t.word ({ "break", "breakdown", "respiration", "pont", "bridge" }) && ! t.phrase ({ "entre le break et le drop", "between the break and the drop" })) addSec (SecKind::Break);
    if (t.word ({ "intro", "introduction" })) addSec (SecKind::Intro);
    if (t.word ({ "outro", "ending" }) || t.phrase ({ "la fin du morceau", "une fin" })) addSec (SecKind::Outro);
    const bool drop2 = t.phrase ({ "drop 2", "drop2", "deuxieme drop", "2eme drop", "2e drop", "second drop", "dernier drop", "last drop" });
    const bool dropWord = t.word ({ "drop", "refrain", "chorus" });
    if (drop2 || (dropWord && (c.mood.intense > 0 || t.word ({ "variation", "variations", "varie" })) && secs.empty()))
    {
        addSec (SecKind::Drop2);
        if (! drop2)
            c.notes.add (utf8 ("Ton drop existant n'est pas réécrit (FINISH ne modifie jamais tes clips) : FINISH écrit un DROP 2 renforcé à partir de ton drop. "
                               "Pour le drop lui-même, voir aussi l'analyse du mix (punch, basses)."));
    }

    // Variation of one element ("rends cette basse plus agressive", "ajoute des variations de drums")
    std::set<TrackRole> subject;
    for (auto& w : t.w)
    {
        TrackRole r;
        if (roleOfWord (w, r) && ! c.lockRoles.count (r)) { subject.insert (r); if (r == TrackRole::Drums) subject.insert (TrackRole::Perc); }
    }
    const bool variationVerb = t.stem ({ "variation", "varie", "rends", "rend", "make", "retravaille", "rework", "change", "modifie" });

    if (masterWord && ! finishWords && secs.empty() && ! riseWords)
    {
        c.kind = Kind::Master;
        chip (c.keepDark ? utf8 ("MASTER : GARDER LE CARACTÈRE SOMBRE") : c.keepBright ? utf8 ("MASTER : GARDER LA BRILLANCE") : utf8 ("MASTERING"));
        return c;
    }
    if (riseWords)
    {
        c.kind = Kind::Rise;
        if (c.rangeBars == 0) c.rangeBars = 16;
        chip (utf8 ("MONTÉE SUR ") + juce::String (c.rangeBars) + utf8 (" MESURES"));
        return c;
    }
    if (! secs.empty())
    {
        c.kind = Kind::Sections;
        c.sections = secs;
        juce::StringArray n;
        for (auto k : secs) n.add (secKindName (k));
        chip (n.joinIntoString (" + ") + (c.versions > 1 ? utf8 (" × 3 VERSIONS") : juce::String()));
        if (c.mood.intense > 0) chip ("PLUS PUISSANT");
        if (c.mood.progressive > 0) chip ("PLUS FLUIDE");
        if (c.mood.darker > 0) chip ("PLUS SOMBRE");
        return c;
    }
    if (finishWords)
    {
        c.kind = Kind::Finish;
        if (t.stem ({ "finalis" }) || t.phrase ({ "tout finaliser", "finalise tout", "finalize everything" })) { c.level = (int) Level::Finalise; c.masterAlso = true; }
        else if (t.stem ({ "enrichi" })) c.level = (int) Level::Enrich;
        else if (t.stem ({ "amelior", "improve" })) c.level = (int) Level::Improve;
        else if (t.phrase ({ "juste complete", "seulement completer", "complete seulement", "juste les parties manquantes", "only the missing" })) c.level = (int) Level::Complete;
        if (masterWord) c.masterAlso = true;
        chip (utf8 ("TERMINER MON MORCEAU") + (c.level >= 0 ? juce::String (" : ") + levelName ((Level) c.level) : juce::String()));
        return c;
    }
    if (! subject.empty() && variationVerb)
    {
        c.kind = Kind::Variation;
        c.onlyRoles = subject;
        chip (utf8 ("VARIATIONS : ") + roleListLabel (subject));
        if (t.word ({ "agressif", "agressive", "aggressive", "agressivite" }) && subject.count (TrackRole::Bass))
            c.notes.add (utf8 ("Le timbre de ta basse se règle dans Live (FINISH n'a pas accès aux réglages de ton synthé) : ajoute un Saturator (Drive 6-10 dB, courbe Warm) "
                               "ou ouvre le filtre de ton synthé. FINISH écrit une ligne plus nerveuse avec tes notes (octaves, attaques) dans les nouvelles parties."));
        if (t.word ({ "groove" })) c.notes.add (utf8 ("Ton groove est gardé : les variations sont des roulements et des allègements de ton propre pattern, sans changer son placement."));
        return c;
    }
    if (c.mood.any() || ! c.onlyRoles.empty() || t.word ({ "plus", "moins", "more", "less" }))
    {
        c.kind = Kind::Iterate;
        if (c.mood.minimal > 0 || t.phrase ({ "moins charge", "moins de notes", "less busy" })) c.levelDelta = -1;
        if (c.mood.intense > 0 || t.phrase ({ "plus pousse", "plus de variations", "more variation" })) c.levelDelta = +1;
        juce::StringArray n;
        if (c.mood.minimal > 0) n.add ("PLUS SUBTIL");
        if (c.mood.darker > 0) n.add ("PLUS SOMBRE");
        if (c.mood.brighter > 0) n.add ("PLUS LUMINEUX");
        if (c.mood.intense > 0) n.add ("PLUS INTENSE");
        if (! n.isEmpty()) chip (n.joinIntoString (" + "));
        return c;
    }
    return c;
}
