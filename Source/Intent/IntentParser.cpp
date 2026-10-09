#include "IntentParser.h"

namespace
{
    using Field = float IntentVector::*;

    struct Effect { Field field; float weight; };

    struct Keyword
    {
        const char* phrase;              // normalised (lowercase, no accents), space separated
        std::vector<Effect> effects;
        bool complaint;                  // describes a problem: modifiers are ignored ("too squashed")
    };

    const Effect punchFx[]  = { { &IntentVector::punch, 1.0f }, { &IntentVector::impact, 0.5f } };

    // Order matters only for multi-word phrases, which are tried first (longest match wins).
    const std::vector<Keyword>& keywords()
    {
        static const std::vector<Keyword> k = {
            // punch / impact
            { "hit harder", { { &IntentVector::punch, 1 }, { &IntentVector::impact, 0.7f } }, false },
            { "hits harder", { { &IntentVector::punch, 1 }, { &IntentVector::impact, 0.7f } }, false },
            { "tape plus fort", { { &IntentVector::punch, 1 }, { &IntentVector::impact, 0.7f } }, false },
            { "tapent plus fort", { { &IntentVector::punch, 1 }, { &IntentVector::impact, 0.7f } }, false },
            { "taper plus fort", { { &IntentVector::punch, 1 }, { &IntentVector::impact, 0.7f } }, false },
            { "frapper plus fort", { { &IntentVector::punch, 1 }, { &IntentVector::impact, 0.7f } }, false },
            { "punch", { punchFx[0], punchFx[1] }, false }, { "punchy", { punchFx[0], punchFx[1] }, false },
            { "punchier", { punchFx[0], punchFx[1] }, false }, { "percutant", { punchFx[0], punchFx[1] }, false },
            { "percutante", { punchFx[0], punchFx[1] }, false }, { "impact", { { &IntentVector::impact, 1 }, { &IntentVector::punch, 0.6f } }, false },
            { "impactant", { { &IntentVector::impact, 1 }, { &IntentVector::punch, 0.6f } }, false },
            { "harder", { punchFx[0], punchFx[1] }, false }, { "hit", { punchFx[0], punchFx[1] }, false },
            { "claque", { punchFx[0], punchFx[1] }, false }, { "claquer", { punchFx[0], punchFx[1] }, false },
            { "tape", { punchFx[0], punchFx[1] }, false }, { "taper", { punchFx[0], punchFx[1] }, false },
            { "frappe", { punchFx[0], punchFx[1] }, false }, { "attack", { punchFx[0], punchFx[1] }, false },
            { "attaque", { punchFx[0], punchFx[1] }, false }, { "snappy", { punchFx[0], punchFx[1] }, false },
            { "transients", { punchFx[0], punchFx[1] }, false }, { "transitoires", { punchFx[0], punchFx[1] }, false },
            { "drums", { punchFx[0] }, false }, { "batterie", { punchFx[0] }, false }, { "kick", { punchFx[0] }, false },

            // energy / mass
            { "massive", { { &IntentVector::energy, 1 }, { &IntentVector::loudness, 0.5f } }, false },
            { "massif", { { &IntentVector::energy, 1 }, { &IntentVector::loudness, 0.5f } }, false },
            { "huge", { { &IntentVector::energy, 1 }, { &IntentVector::width, 0.3f } }, false },
            { "enorme", { { &IntentVector::energy, 1 }, { &IntentVector::width, 0.3f } }, false },
            { "bigger", { { &IntentVector::energy, 1 }, { &IntentVector::width, 0.3f } }, false },
            { "big", { { &IntentVector::energy, 1 } }, false }, { "gros", { { &IntentVector::energy, 1 } }, false },
            { "grosse", { { &IntentVector::energy, 1 } }, false }, { "fat", { { &IntentVector::energy, 1 } }, false },
            { "energy", { { &IntentVector::energy, 1 } }, false }, { "energie", { { &IntentVector::energy, 1 } }, false },
            { "power", { { &IntentVector::energy, 1 }, { &IntentVector::loudness, 0.4f } }, false },
            { "powerful", { { &IntentVector::energy, 1 }, { &IntentVector::loudness, 0.4f } }, false },
            { "puissant", { { &IntentVector::energy, 1 }, { &IntentVector::loudness, 0.4f } }, false },
            { "puissance", { { &IntentVector::energy, 1 }, { &IntentVector::loudness, 0.4f } }, false },
            { "dense", { { &IntentVector::energy, 0.8f }, { &IntentVector::impact, 0.5f } }, false },
            { "heavy", { { &IntentVector::energy, 0.8f }, { &IntentVector::bass, 0.5f } }, false },
            { "lourd", { { &IntentVector::energy, 0.8f }, { &IntentVector::bass, 0.5f } }, false },
            { "epic", { { &IntentVector::energy, 1 }, { &IntentVector::width, 0.4f } }, false },
            { "epique", { { &IntentVector::energy, 1 }, { &IntentVector::width, 0.4f } }, false },

            // loudness
            { "loud", { { &IntentVector::loudness, 1 } }, false }, { "louder", { { &IntentVector::loudness, 1 } }, false },
            { "loudness", { { &IntentVector::loudness, 1 } }, false }, { "volume", { { &IntentVector::loudness, 1 } }, false },
            { "fort", { { &IntentVector::loudness, 1 } }, false }, { "forte", { { &IntentVector::loudness, 1 } }, false },

            // width
            { "wide", { { &IntentVector::width, 1 } }, false }, { "wider", { { &IntentVector::width, 1 } }, false },
            { "width", { { &IntentVector::width, 1 } }, false }, { "stereo", { { &IntentVector::width, 1 } }, false },
            { "large", { { &IntentVector::width, 1 } }, false }, { "larges", { { &IntentVector::width, 1 } }, false },
            { "largeur", { { &IntentVector::width, 1 } }, false }, { "ample", { { &IntentVector::width, 1 } }, false },
            { "spacious", { { &IntentVector::width, 1 } }, false }, { "spacieux", { { &IntentVector::width, 1 } }, false },
            { "immersive", { { &IntentVector::width, 1 } }, false }, { "immersif", { { &IntentVector::width, 1 } }, false },
            { "espace", { { &IntentVector::width, 1 } }, false },
            { "narrow", { { &IntentVector::width, -1 } }, false }, { "mono", { { &IntentVector::width, -1 } }, false },
            { "serre", { { &IntentVector::width, -1 } }, false }, { "etroit", { { &IntentVector::width, -1 } }, false },

            // warmth / brightness
            { "warm", { { &IntentVector::warmth, 1 } }, false }, { "warmer", { { &IntentVector::warmth, 1 } }, false },
            { "chaud", { { &IntentVector::warmth, 1 } }, false }, { "chaude", { { &IntentVector::warmth, 1 } }, false },
            { "chaleur", { { &IntentVector::warmth, 1 } }, false }, { "rond", { { &IntentVector::warmth, 1 } }, false },
            { "round", { { &IntentVector::warmth, 1 } }, false }, { "analog", { { &IntentVector::warmth, 1 } }, false },
            { "analogique", { { &IntentVector::warmth, 1 } }, false }, { "vintage", { { &IntentVector::warmth, 1 } }, false },
            { "smooth", { { &IntentVector::warmth, 0.8f } }, false }, { "doux", { { &IntentVector::warmth, 0.8f } }, false },
            { "bright", { { &IntentVector::brightness, 1 } }, false }, { "brighter", { { &IntentVector::brightness, 1 } }, false },
            { "brillant", { { &IntentVector::brightness, 1 } }, false }, { "brillance", { { &IntentVector::brightness, 1 } }, false },
            { "clair", { { &IntentVector::brightness, 1 } }, false }, { "air", { { &IntentVector::brightness, 1 } }, false },
            { "airy", { { &IntentVector::brightness, 1 } }, false }, { "aigus", { { &IntentVector::brightness, 1 } }, false },
            { "aigu", { { &IntentVector::brightness, 1 } }, false }, { "treble", { { &IntentVector::brightness, 1 } }, false },
            { "highs", { { &IntentVector::brightness, 1 } }, false }, { "crisp", { { &IntentVector::brightness, 1 } }, false },
            { "presence", { { &IntentVector::brightness, 0.8f } }, false }, { "open", { { &IntentVector::brightness, 0.8f } }, false },
            { "ouvert", { { &IntentVector::brightness, 0.8f } }, false },

            // bass
            { "low end", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, false },
            { "bass", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, false },
            { "basse", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, false },
            { "basses", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, false },
            { "sub", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, false },
            { "grave", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, false },
            { "graves", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, false },

            // dynamics
            { "dynamic", { { &IntentVector::dynamics, 1 } }, false }, { "dynamics", { { &IntentVector::dynamics, 1 } }, false },
            { "dynamique", { { &IntentVector::dynamics, 1 } }, false }, { "breathe", { { &IntentVector::dynamics, 1 } }, false },
            { "respire", { { &IntentVector::dynamics, 1 } }, false }, { "respirer", { { &IntentVector::dynamics, 1 } }, false },
            { "natural", { { &IntentVector::dynamics, 0.7f } }, false }, { "naturel", { { &IntentVector::dynamics, 0.7f } }, false },
            { "compressed", { { &IntentVector::dynamics, -1 } }, false }, { "compresse", { { &IntentVector::dynamics, -1 } }, false },

            // complaints (describe the problem, not the wish)
            { "squashed", { { &IntentVector::dynamics, 1 }, { &IntentVector::loudness, -0.6f } }, true },
            { "crushed", { { &IntentVector::dynamics, 1 }, { &IntentVector::loudness, -0.6f } }, true },
            { "ecrase", { { &IntentVector::dynamics, 1 }, { &IntentVector::loudness, -0.6f } }, true },
            { "muddy", { { &IntentVector::brightness, 0.7f }, { &IntentVector::bass, -0.5f } }, true },
            { "boueux", { { &IntentVector::brightness, 0.7f }, { &IntentVector::bass, -0.5f } }, true },
            { "dull", { { &IntentVector::brightness, 1 } }, true }, { "terne", { { &IntentVector::brightness, 1 } }, true },
            { "etouffe", { { &IntentVector::brightness, 1 } }, true }, { "muffled", { { &IntentVector::brightness, 1 } }, true },
            { "harsh", { { &IntentVector::brightness, -1 }, { &IntentVector::warmth, 0.6f } }, true },
            { "agressif", { { &IntentVector::brightness, -1 }, { &IntentVector::warmth, 0.6f } }, true },
            { "aggressive", { { &IntentVector::brightness, -1 }, { &IntentVector::warmth, 0.6f } }, true },
            { "thin", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, true },
            { "maigre", { { &IntentVector::bass, 1 }, { &IntentVector::warmth, 0.5f } }, true },
            { "flat", { { &IntentVector::punch, 0.7f }, { &IntentVector::dynamics, 0.6f } }, true },
            { "plat", { { &IntentVector::punch, 0.7f }, { &IntentVector::dynamics, 0.6f } }, true },
            { "weak", { { &IntentVector::punch, 0.8f }, { &IntentVector::energy, 0.8f } }, true },
            { "faible", { { &IntentVector::punch, 0.8f }, { &IntentVector::energy, 0.8f } }, true },
            { "mou", { { &IntentVector::punch, 1 }, { &IntentVector::impact, 0.6f } }, true },
            { "small", { { &IntentVector::energy, 0.8f }, { &IntentVector::width, 0.6f } }, true },
            { "petit", { { &IntentVector::energy, 0.8f }, { &IntentVector::width, 0.6f } }, true },
        };
        return k;
    }

    // Words that flip the meaning of the next keyword ("less", "too much", "not", "pas assez" = flip twice).
    bool isNegator (const juce::String& t)
    {
        static const juce::StringArray n { "less", "moins", "not", "no", "pas", "too", "trop", "reduce", "reduire",
                                           "reduis", "baisse", "baisser", "remove", "enleve", "enlever", "without",
                                           "sans", "fewer", "lower", "enough", "assez", "never", "jamais", "dont", "don" };
        return n.contains (t);
    }

    bool isBoundary (const juce::String& t)
    {
        static const juce::StringArray b { "but", "mais", "and", "et", "while", "tout", "en", "with", "avec", "|" };
        return b.contains (t);
    }
}

juce::String IntentParser::normalise (const juce::String& text)
{
    juce::String out;
    for (auto c : text.toLowerCase())
    {
        switch (c)
        {
            case 0xe0: case 0xe2: case 0xe4: c = 'a'; break;
            case 0xe9: case 0xe8: case 0xea: case 0xeb: c = 'e'; break;
            case 0xee: case 0xef: c = 'i'; break;
            case 0xf4: case 0xf6: c = 'o'; break;
            case 0xf9: case 0xfb: case 0xfc: c = 'u'; break;
            case 0xe7: c = 'c'; break;
            default: break;
        }
        if (c == ',' || c == ';' || c == '.' || c == '!' || c == '?') { out << " | "; continue; }
        if (c == '\'' || c == 0x2019) { continue; }     // don't -> dont, l'energie -> lenergie handled below
        out << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String (" "));
    }
    // French elisions: "lenergie", "dimpact" -> keep the word after the elided article.
    juce::StringArray tokens;
    tokens.addTokens (out, " ", "");
    tokens.removeEmptyStrings();
    for (auto& t : tokens)
    {
        for (auto prefix : { "l", "d" })
            if (t.length() > 3 && t.startsWith (prefix))
            {
                const auto rest = t.substring (1);
                for (auto& k : keywords())
                    if (rest == k.phrase) { t = rest; break; }
            }
    }
    return tokens.joinIntoString (" ");
}

IntentParser::Result IntentParser::parse (const juce::String& text)
{
    Result res;
    juce::StringArray tokens;
    tokens.addTokens (normalise (text), " ", "");
    tokens.removeEmptyStrings();

    std::vector<bool> isKeyword ((size_t) tokens.size(), false);

    // Pre-split phrases into token lists (multi-word phrases first).
    struct Compiled { juce::StringArray words; const Keyword* kw; };
    std::vector<Compiled> compiled;
    for (auto& k : keywords())
    {
        juce::StringArray w; w.addTokens (k.phrase, " ", "");
        compiled.push_back ({ w, &k });
    }
    std::stable_sort (compiled.begin(), compiled.end(), [] (const Compiled& a, const Compiled& b) { return a.words.size() > b.words.size(); });

    for (int i = 0; i < tokens.size(); ++i)
    {
        if (isKeyword[(size_t) i]) continue;
        for (auto& c : compiled)
        {
            if (i + c.words.size() > tokens.size()) continue;
            bool match = true;
            for (int w = 0; w < c.words.size() && match; ++w)
                match = tokens[i + w] == c.words[w];
            if (! match) continue;

            for (int w = 0; w < c.words.size(); ++w) isKeyword[(size_t) (i + w)] = true;

            float sign = 1.0f;
            if (! c.kw->complaint)
                for (int b = i - 1; b >= std::max (0, i - 4); --b)
                {
                    if (isKeyword[(size_t) b] || isBoundary (tokens[b])) break;
                    if (isNegator (tokens[b])) sign = -sign;
                }

            for (auto& e : c.kw->effects)
                res.intent.*(e.field) = juce::jlimit (-1.0f, 1.0f, res.intent.*(e.field) + sign * e.weight);
            break;
        }
    }

    const std::pair<const char*, Field> names[] = {
        { "PUNCH", &IntentVector::punch }, { "IMPACT", &IntentVector::impact }, { "ENERGY", &IntentVector::energy },
        { "LOUDNESS", &IntentVector::loudness }, { "WIDTH", &IntentVector::width }, { "WARMTH", &IntentVector::warmth },
        { "BRIGHTNESS", &IntentVector::brightness }, { "BASS", &IntentVector::bass }, { "DYNAMICS", &IntentVector::dynamics } };
    for (auto& [name, field] : names)
    {
        const float v = res.intent.*field;
        if (std::abs (v) >= 0.3f) res.understood.add ((v > 0 ? "+" : "-") + juce::String (name));
    }
    return res;
}
