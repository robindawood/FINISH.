#include "ProjectSession.h"
#include "../Compose/ArrangementExport.h"

static double nowS() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

void ProjectSession::log (Entry::Kind k, const juce::String& text)
{
    journal.push_back ({ k, text, juce::Time::getCurrentTime().toMilliseconds() / 1000.0 });
    if (journal.size() > 200) journal.erase (journal.begin());
    ++version;
}

void ProjectSession::refreshDiagnosis()
{
    const auto before = diag.status;
    diag = bridge.diagnose();
    lastDiag = nowS();
    if (before != diag.status) ++version;
}

bool ProjectSession::readProject (juce::String& error)
{
    refreshDiagnosis();
    if (! diag.connected()) { error = diag.title; return false; }
    if (bridge.busy()) { error = utf8 ("Live est déjà en train de répondre, une seconde…"); return false; }
    if (! bridge.requestSnapshot (error)) return false;
    phase = Phase::Reading;
    ++version;
    return true;
}

void ProjectSession::loadProject (LiveProject p)
{
    project = std::move (p);
    analysis = ProjectAnalyzer::analyse (project);
    log (Entry::Kind::Analysed, analysis.summary);
    history.clear();
    replan (false);
    freshPlan = plan.valid;
    phase = plan.valid ? Phase::Ready : Phase::Idle;
}

bool ProjectSession::tick()
{
    bool changed = false;
    if (nowS() - lastDiag > 1.0)
    {
        const auto v = version;
        refreshDiagnosis();
        changed |= v != version;
    }
    LiveBridge::Response r;
    if (! bridge.poll (r)) return changed;

    if (r.type == "snapshot")
    {
        phase = Phase::Idle;
        if (! r.ok) { log (Entry::Kind::Error, r.error.isNotEmpty() ? r.error : r.message); return true; }
        auto p = bridge.loadProject();
        if (! p.valid) { log (Entry::Kind::Error, utf8 ("Projet illisible : ") + p.error); return true; }
        project = std::move (p);
        analysis = ProjectAnalyzer::analyse (project);
        log (Entry::Kind::Analysed, analysis.summary);
        // The plan always follows the set as it is now (after a send, what was written shows as part of the set).
        if (autoPlanAfterRead)
        {
            history.clear();
            offCells.clear();
            replan (false);
            freshPlan = plan.valid;
        }
        else
            replan (true);
        autoPlanAfterRead = true;
        phase = plan.valid ? Phase::Ready : Phase::Idle;
        return true;
    }
    if (r.type == "apply")
    {
        phase = plan.valid ? Phase::Ready : Phase::Idle;
        lastReport = r.describe();
        liveResult = true;
        log (r.ok ? Entry::Kind::Sent : Entry::Kind::Error, lastReport);
        if (r.ok)
            if (auto* manual = r.report["manual"].getArray())
                for (auto& m : *manual) log (Entry::Kind::Manual, m.toString());
        // Read the set again so the analysis matches what is now in Live.
        juce::String e;
        autoPlanAfterRead = false;
        bridge.requestSnapshot (e);
        phase = Phase::Reading;
        return true;
    }
    if (r.type == "undo")
    {
        phase = plan.valid ? Phase::Ready : Phase::Idle;
        lastReport = r.ok ? r.message : r.describe();
        liveResult = true;
        log (r.ok ? Entry::Kind::Undone : Entry::Kind::Error, lastReport);
        juce::String e;
        autoPlanAfterRead = false;
        bridge.requestSnapshot (e);
        phase = Phase::Reading;
        return true;
    }
    phase = plan.valid ? Phase::Ready : Phase::Idle;
    autoPlanAfterRead = true;
    return true;
}

void ProjectSession::applyUserEdits()
{
    for (auto& s : plan.sections)
    {
        if (s.existing) continue;
        for (auto& c : s.cells)
            if (offCells.count ({ s.name, c.track })) c.off = true;
    }
}

void ProjectSession::replan (bool keepHistory, const std::set<TrackRole>& onlyChange)
{
    if (! analysis.valid) return;
    if (keepHistory && plan.valid)
    {
        history.push_back ({ options, plan });
        if (history.size() > 30) history.erase (history.begin());
    }
    auto previous = plan;
    plan = ProjectPlanner::makePlan (project, analysis, options);
    if (! onlyChange.empty() && previous.valid)
    {
        // "modifie uniquement la basse": every other track keeps what it had in the previous plan.
        for (auto& s : plan.sections)
        {
            if (s.existing) continue;
            for (auto& ps : previous.sections)
                if (! ps.existing && ps.kind == s.kind && ps.startBar == s.startBar && ps.bars == s.bars)
                    for (size_t i = 0; i < s.cells.size() && i < ps.cells.size(); ++i)
                        if (! onlyChange.count (analysis.tracks[i].role)) s.cells[i] = ps.cells[i];
        }
    }
    applyUserEdits();
    log (Entry::Kind::Generated, utf8 ("Plan ") + levelName (options.level) + " / version " + juce::String (getVersionIndex() + 1) + " : "
                                 + juce::String (plan.newSectionCount()) + utf8 (" partie(s), ") + juce::String (plan.countWrites()) + utf8 (" clip(s) à écrire."));
    ++version;
}

void ProjectSession::setLevel (Level l) { if (options.level != l) { options.level = l; replan(); } }

void ProjectSession::setVersion (int v)
{
    const auto s = (juce::uint32) juce::jlimit (0, 2, v);
    if (options.seed % 3 == s) return;
    options.seed = s;
    replan();
}

void ProjectSession::toggleCell (int section, int track)
{
    if (section < 0 || section >= (int) plan.sections.size()) return;
    auto& s = plan.sections[(size_t) section];
    if (s.existing || track < 0 || track >= (int) s.cells.size()) return;
    const auto key = std::make_pair (s.name, track);
    if (offCells.count (key)) offCells.erase (key); else offCells.insert (key);
    s.cells[(size_t) track].off = offCells.count (key) > 0;
    ++version;
}

bool ProjectSession::isTrackLocked (int track) const { return options.lockedTracks.count (track) > 0; }

void ProjectSession::toggleTrackLock (int track)
{
    if (options.lockedTracks.count (track)) options.lockedTracks.erase (track); else options.lockedTracks.insert (track);
    replan();
}

void ProjectSession::cycleSectionVariant (int section)
{
    if (section < 0 || section >= (int) plan.sections.size() || plan.sections[(size_t) section].existing) return;
    auto& v = options.variants[(size_t) plan.sections[(size_t) section].kind];
    v = (v + 1) % 3;
    replan();
}

bool ProjectSession::undoPlan()
{
    if (history.empty()) return false;
    options = history.back().first;
    plan = history.back().second;
    history.pop_back();
    log (Entry::Kind::Generated, utf8 ("Retour à la version précédente du plan."));
    ++version;
    return true;
}

ProjectSession::Outcome ProjectSession::apply (const ProjectCommand& cmd)
{
    Outcome out;
    using K = ProjectCommand::Kind;
    auto needProject = [&]
    {
        if (analysis.valid) return false;
        out.needsProject = true;
        out.answer = utf8 ("Je lis d'abord ton projet dans Live…");
        return true;
    };
    for (auto& n : cmd.notes) log (Entry::Kind::Info, n);

    switch (cmd.kind)
    {
        case K::None: break;
        case K::Send: out.wantsSend = true; break;
        case K::UndoSend: out.wantsUndoSend = true; break;
        case K::KeepPrevious:
            out.answer = undoPlan() ? utf8 ("Version précédente du plan rétablie.") : utf8 ("Pas de version précédente.");
            break;
        case K::AnalyseProject:
            if (needProject()) break;
            out.answer = ProjectAnalyzer::report (analysis, project);
            if (plan.valid) out.answer << "\n" << planText();
            log (Entry::Kind::Analysed, utf8 ("Analyse du projet présentée."));
            break;
        case K::AnalyseMix:
            out.wantsMasterAnalysis = true;
            out.mixTopic = cmd.mixTopic;
            break;
        case K::Master:
            out.wantsMaster = true;
            out.keepDark = cmd.keepDark;
            out.keepBright = cmd.keepBright;
            break;
        case K::Finish:
        case K::Sections:
        case K::Rise:
        case K::Variation:
        case K::Iterate:
        {
            if (needProject()) break;
            const auto before = options;
            std::set<TrackRole> onlyChange;
            if (cmd.kind != K::Iterate)
            {
                options.lockedRoles = cmd.lockRoles;
                options.onlyRoles.clear();
                options.onlySections.clear();
                options.rangeBars = 0;
                options.mood = cmd.mood;
            }
            else
            {
                if (cmd.mood.darker > 0) { options.mood.darker = 1; options.mood.brighter = 0; }
                if (cmd.mood.brighter > 0) { options.mood.brighter = 1; options.mood.darker = 0; }
                if (cmd.mood.intense > 0) { options.mood.intense = 1; options.mood.minimal = 0; }
                if (cmd.mood.minimal > 0) { options.mood.minimal = 1; options.mood.intense = 0; }
                for (auto r : cmd.lockRoles) options.lockedRoles.insert (r);
            }
            if (cmd.level >= 0) options.level = (Level) cmd.level;
            if (cmd.levelDelta != 0) options.level = (Level) juce::jlimit (0, (int) Level::Improve, (int) options.level + cmd.levelDelta);
            if (cmd.kind == K::Sections) options.onlySections = cmd.sections;
            if (cmd.kind == K::Rise) options.rangeBars = cmd.rangeBars;
            if (cmd.kind == K::Variation)
            {
                // Same plan, only the named element changes (new idea for it).
                options.seed = (options.seed + 1) % 3;
                onlyChange = cmd.onlyRoles;
                options.lockedRoles = before.lockedRoles;
                options.onlySections = before.onlySections;
                options.rangeBars = before.rangeBars;
                if (cmd.mood.intense > 0) options.mood.intense = 1;
                if (options.level < Level::Enrich) options.level = Level::Enrich;
            }
            if (! cmd.onlyRoles.empty() && cmd.kind == K::Iterate)
            {
                options.seed = (options.seed + 1) % 3;
                onlyChange = cmd.onlyRoles;
            }
            replan (true, onlyChange);
            juce::StringArray a;
            if (cmd.kind == K::Sections && cmd.versions > 1)
                a.add (utf8 ("Trois versions prêtes : passe de l'une à l'autre avec VERSION 1 / 2 / 3, écoute, puis ENVOYER DANS LIVE."));
            for (auto& l : plan.explain) a.add (l);
            for (auto& n : cmd.notes) a.add (n);
            a.add (utf8 ("Rien n'est encore écrit dans Live : vérifie le plan, puis ENVOYER DANS LIVE."));
            out.answer = a.joinIntoString ("\n");
            out.wantsMaster = cmd.masterAlso || options.level == Level::Finalise;
            break;
        }
    }
    if (out.answer.isNotEmpty() && cmd.kind != K::AnalyseProject) log (Entry::Kind::Info, out.answer.upToFirstOccurrenceOf ("\n", false, false));
    ++version;
    return out;
}

juce::String ProjectSession::confirmText() const
{
    if (! plan.valid) return {};
    juce::StringArray tracks;
    std::set<int> used;
    for (auto& s : plan.sections)
        if (! s.existing)
            for (auto& c : s.cells) if (! c.empty()) used.insert (c.track);
    for (int t : used) if (t < (int) project.tracks.size()) tracks.add (project.tracks[(size_t) t].name);
    juce::String s;
    s << utf8 ("FINISH va écrire ") << plan.countWrites() << utf8 (" clip(s) dans ton set, à partir de la mesure ") << (plan.firstNewBar + 1) << " :\n";
    for (auto& sec : plan.sections)
        if (! sec.existing)
            s << "  " << sec.name << utf8 (" — mesures ") << (sec.startBar + 1) << "-" << (sec.startBar + sec.bars) << "\n";
    s << utf8 ("\nSur tes pistes : ") << tracks.joinIntoString (", ") << "\n";
    if (! plan.layers.empty()) s << utf8 ("+ une copie vide de ta piste ") << project.tracks[(size_t) plan.layers[0].track].name << utf8 (" pour les roulements\n");
    for (auto& sec : plan.sections) if (! sec.existing && ! sec.fx.empty()) { s << utf8 ("+ une piste FINISH FX (risers, impacts)\n"); break; }
    s << utf8 ("\nAucun de tes clips n'est modifié ni déplacé. Une place déjà occupée est sautée. Annulable (bouton ANNULER L'ENVOI ou Cmd/Ctrl+Z dans Live).");
    return s;
}

bool ProjectSession::send (double sampleRate, juce::String& error)
{
    if (! plan.valid || plan.countWrites() == 0) { error = utf8 ("Rien à envoyer."); return false; }
    refreshDiagnosis();
    if (! diag.connected()) { error = diag.title; return false; }
    if (bridge.busy()) { error = utf8 ("Live est occupé, réessaie dans une seconde."); return false; }
    juce::StringArray notes;
    const auto fxFolder = ArrangementExport::defaultRoot().getChildFile ("fx").getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
    auto req = ProjectPlanner::buildApplyRequest (plan, project, analysis, {}, fxFolder, sampleRate, notes);
    for (auto& n : notes) log (Entry::Kind::Manual, n);
    if (! bridge.requestApply (req, error)) return false;
    phase = Phase::Sending;
    ++version;
    return true;
}

bool ProjectSession::undoSend (juce::String& error)
{
    if (! hasSent()) { error = utf8 ("Rien à annuler."); return false; }
    if (bridge.busy()) { error = utf8 ("Live est occupé, réessaie dans une seconde."); return false; }
    if (! bridge.requestUndo (bridge.lastApplyId(), error)) return false;
    phase = Phase::Undoing;
    ++version;
    return true;
}

Arrangement ProjectSession::previewArrangement() const
{
    return plan.valid ? ProjectPlanner::toPreview (plan, project, analysis) : Arrangement {};
}

juce::String ProjectSession::planText() const
{
    juce::String s;
    if (! plan.valid) return s;
    s << utf8 ("PLAN (") << levelName (plan.level) << ", version " << (getVersionIndex() + 1) << ")\n";
    for (auto& l : plan.explain) s << "- " << l << "\n";
    for (auto& sec : plan.sections)
    {
        if (sec.existing) continue;
        s << "\n" << sec.name << utf8 (" — mesures ") << (sec.startBar + 1) << "-" << (sec.startBar + sec.bars) << " : " << sec.recipe << "\n";
        for (auto& c : sec.cells)
            if (! c.empty()) s << "   " << project.tracks[(size_t) c.track].name << " : " << c.summary() << (c.locked ? utf8 (" (verrouillée)") : juce::String()) << "\n";
    }
    if (! plan.manual.isEmpty())
    {
        s << utf8 ("\nÀ FAIRE TOI-MÊME\n");
        for (auto& m : plan.manual) s << "- " << m << "\n";
    }
    return s;
}

juce::String ProjectSession::getStatus() const
{
    switch (phase)
    {
        case Phase::Reading: return utf8 ("Lecture de ton projet dans Live…");
        case Phase::Sending: return utf8 ("Écriture dans Live…");
        case Phase::Undoing: return utf8 ("Annulation dans Live…");
        default: break;
    }
    if (plan.valid) return analysis.summary;
    if (! diag.connected()) return diag.title;
    return utf8 ("Connecté à Live : appuie sur FINISH pour lire ton projet.");
}

juce::ValueTree ProjectSession::toState() const
{
    juce::ValueTree v ("PROJECT");
    v.setProperty ("level", (int) options.level, nullptr);
    v.setProperty ("seed", (int) options.seed, nullptr);
    return v;
}

void ProjectSession::fromState (const juce::ValueTree& v)
{
    if (! v.isValid()) return;
    options.level = (Level) juce::jlimit (0, numLevels - 1, (int) v.getProperty ("level", (int) Level::Enrich));
    options.seed = (juce::uint32) juce::jlimit (0, 2, (int) v.getProperty ("seed", 0));
}
