#include "LiveBridge.h"
#include "../Compose/ArrangementExport.h"
#include "../Compose/ComposeTypes.h"

static constexpr const char* currentScriptVersion = "3.0";

juce::File LiveBridge::inbox() { return ArrangementExport::liveInbox(); }

static double nowSeconds() { return juce::Time::getCurrentTime().toMilliseconds() / 1000.0; }

juce::String LiveBridge::linkVersusControlSurface()
{
    return utf8 ("Ableton Link ne sert qu'à synchroniser le tempo entre applications : FINISH n'en a pas besoin. "
                 "Ce qui relie FINISH à ton projet, c'est le script « FINISH » choisi comme Control Surface "
                 "(Réglages de Live > Link, Tempo & MIDI > Control Surface, entrée et sortie : Aucun). "
                 "Il lit ton set (pistes, clips, notes) et écrit ce que tu valides.");
}

LiveBridge::Diagnosis LiveBridge::diagnose() const
{
    Diagnosis d;
    for (auto& lib : ArrangementExport::liveUserLibraries (false))
    {
        const auto dir = lib.getChildFile ("Remote Scripts/FINISH");
        if (dir.getChildFile ("finish_bridge.py").existsAsFile()) d.installedIn.add (dir.getFullPathName());
    }
    const auto hb = inbox().getChildFile ("bridge.json");
    juce::var v;
    if (hb.existsAsFile()) v = juce::JSON::parse (hb.loadFileAsString());
    if (v.isObject())
    {
        d.liveVersion = v["live"].toString();
        d.scriptVersion = v.hasProperty ("version") ? v["version"].toString() : juce::String ("2");
        const double t = (double) v.getProperty ("time", 0.0);
        d.heartbeatAge = t > 0 ? juce::jmax (0.0, nowSeconds() - t) : (juce::Time::getCurrentTime() - hb.getLastModificationTime()).inSeconds();
        d.canArrange = (bool) v["caps"]["create_midi_clip"] || (bool) v["caps"]["duplicate_to_arrangement"];
    }

    const auto selectSteps = [&]
    {
        d.steps.add (utf8 ("Dans Live : Réglages (Cmd+, sur Mac, Ctrl+, sur Windows) > onglet « Link, Tempo & MIDI »."));
        d.steps.add (utf8 ("Dans une ligne « Control Surface », choisis FINISH. Entrée et Sortie : Aucun."));
        d.steps.add (utf8 ("C'est tout : le voyant de FINISH passe au vert en 2 secondes. Ableton Link n'a pas besoin d'être activé."));
    };

    if (v.isObject() && d.scriptVersion != currentScriptVersion)
    {
        d.status = Status::OldScript;
        d.title = utf8 ("Live utilise l'ancien script FINISH (v") + d.scriptVersion + ")";
        d.steps.add (utf8 ("Le nouveau script est installé, Live doit le recharger : redémarre Live"));
        d.steps.add (utf8 ("(ou dans Réglages > Link, Tempo & MIDI, remets la Control Surface sur Aucun puis sur FINISH)."));
        return d;
    }
    if (v.isObject() && (bool) v["alive"] && d.heartbeatAge >= 0 && d.heartbeatAge < 8.0)
    {
        d.status = Status::Connected;
        d.title = utf8 ("Connecté à Live ") + d.liveVersion + (d.canArrange ? juce::String() : utf8 (" (version limitée : Live 11 ou plus requis)"));
        return d;
    }
    if (d.installedIn.isEmpty())
    {
        d.status = Status::NotInstalled;
        d.title = utf8 ("Script Live non installé");
        d.steps.add (utf8 ("Clique sur INSTALLER : FINISH copie son script dans ta User Library d'Ableton."));
        d.steps.add (utf8 ("Puis redémarre Live."));
        selectSteps();
        return d;
    }
    if (v.isObject())
    {
        d.status = Status::LiveNotRunning;
        const int age = (int) d.heartbeatAge;
        d.title = utf8 ("Live ne répond pas (dernier signe il y a ") + (age < 120 ? juce::String (age) + " s" : juce::String (age / 60) + " min") + ")";
        d.steps.add (utf8 ("Live est-il ouvert ? Si oui, FINISH n'est plus choisi comme Control Surface :"));
        selectSteps();
        return d;
    }
    d.status = Status::NotSelected;
    d.title = utf8 ("Script installé, pas encore activé dans Live");
    d.steps.add (utf8 ("Si Live était ouvert pendant l'installation, redémarre-le une fois."));
    selectSteps();
    return d;
}

double LiveBridge::waitingSeconds() const noexcept { return busy() ? nowSeconds() - sentAt : 0.0; }

bool LiveBridge::send (juce::var request, const juce::String& kind, juce::String& error)
{
    const auto dir = inbox();
    if (! dir.createDirectory()) { error = utf8 ("Dossier inaccessible : ") + dir.getFullPathName(); return false; }
    const auto id = kind + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
    if (auto* o = request.getDynamicObject()) { o->setProperty ("id", id); o->setProperty ("type", kind); }
    dir.getChildFile ("response.json").deleteFile();
    const auto tmp = dir.getChildFile ("request.json.tmp");
    if (! tmp.replaceWithText (juce::JSON::toString (request, true)) || ! tmp.moveFileTo (dir.getChildFile ("request.json")))
    {
        error = utf8 ("Écriture impossible dans ") + dir.getFullPathName();
        return false;
    }
    pendingId = id;
    pendingKind = kind;
    sentAt = nowSeconds();
    if (kind == "apply") lastApply = id;
    return true;
}

static juce::var object() { return juce::var (new juce::DynamicObject()); }

bool LiveBridge::requestSnapshot (juce::String& error) { return send (object(), "snapshot", error); }
bool LiveBridge::requestPing (juce::String& error)     { return send (object(), "ping", error); }
bool LiveBridge::requestApply (juce::var request, juce::String& error) { return send (request, "apply", error); }

bool LiveBridge::requestUndo (const juce::String& applyId, juce::String& error)
{
    auto r = object();
    r.getDynamicObject()->setProperty ("target", applyId);
    return send (r, "undo", error);
}

bool LiveBridge::poll (Response& out)
{
    if (! busy()) return false;
    const auto f = inbox().getChildFile ("response.json");
    if (f.existsAsFile())
    {
        const auto v = juce::JSON::parse (f.loadFileAsString());
        if (v.isObject() && v["id"].toString() == pendingId)
        {
            out = {};
            out.ok = (bool) v["ok"];
            out.type = pendingKind;
            out.id = pendingId;
            out.message = v["message"].toString();
            out.error = v["error"].toString();
            out.report = v["report"];
            if (pendingKind == "undo" && out.ok) lastApply.clear();
            pendingId.clear();
            pendingKind.clear();
            return true;
        }
    }
    if (waitingSeconds() > timeoutSeconds)
    {
        out = {};
        out.ok = false;
        out.type = pendingKind;
        out.id = pendingId;
        const auto d = diagnose();
        out.error = utf8 ("Live n'a pas répondu. ") + d.title + ".";
        // Leave no stale request behind: Live would execute it later by surprise.
        inbox().getChildFile ("request.json").deleteFile();
        pendingId.clear();
        pendingKind.clear();
        return true;
    }
    return false;
}

juce::String LiveBridge::Response::describe() const
{
    juce::String s;
    if (! ok) return error.isNotEmpty() ? error : message;
    s << message;
    auto list = [&] (const char* key, const juce::String& title)
    {
        if (auto* a = report[key].getArray(); a != nullptr && ! a->isEmpty())
        {
            s << "\n" << title;
            for (auto& x : *a) s << "\n  - " << x.toString();
        }
    };
    list ("skipped", utf8 ("Non écrit (place déjà prise par tes clips) :"));
    list ("manual", utf8 ("À faire toi-même :"));
    list ("errors", utf8 ("Erreurs :"));
    return s;
}
