#include "ArrangementExport.h"
#include "PreviewSynth.h"
#include "BinaryData.h"

namespace ArrangementExport
{
namespace
{
    constexpr int tpq = 480;

    juce::String twoDigits (int i) { return juce::String (i + 1).paddedLeft ('0', 2); }

    int midiChannelFor (Lane l) { return l == Lane::Drums ? 10 : 1 + (int) l; }

    void addLane (juce::MidiMessageSequence& seq, const Clip& clip, double offsetBeats, int channel)
    {
        for (auto& n : clip.notes)
        {
            const double on = (offsetBeats + n.beat) * tpq, off = (offsetBeats + n.beat + n.length) * tpq;
            const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));
            seq.addEvent (juce::MidiMessage::noteOn (channel, n.pitch, vel), on);
            seq.addEvent (juce::MidiMessage::noteOff (channel, n.pitch), std::max (on + 1.0, off - 1.0));
        }
        for (auto& c : clip.cc)
            seq.addEvent (juce::MidiMessage::controllerEvent (channel, 74, juce::jlimit (0, 127, juce::roundToInt (c.value * 127.0f))),
                          (offsetBeats + c.beat) * tpq);
    }

    void addHeader (juce::MidiMessageSequence& seq, double bpm, const juce::String& name)
    {
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0);
        seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / std::max (20.0, bpm))), 0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    }

    bool writeMidi (juce::MidiFile& mf, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk()) return false;
        return mf.writeTo (out, 1);
    }

    juce::String safeName (juce::String s)
    {
        return s.replaceCharacters ("/\\:*?\"<>|", "---------").trim();
    }

    bool writeWav (const juce::AudioBuffer<float>& buffer, double sr, const juce::File& file)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk()) return false;
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        if (writer == nullptr) return false;
        return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    juce::AudioBuffer<float> renderFxLane (const Clip& fxClip, double lengthBeats, double bpm, double sr)
    {
        const int total = (int) std::ceil (lengthBeats * 60.0 / bpm * sr);
        juce::AudioBuffer<float> out (2, std::max (1, total));
        out.clear();
        for (auto& f : fxClip.fx)
        {
            const auto one = PreviewSynth::renderFx (f.kind, f.length, bpm, sr, f.level);
            const int start = (int) std::round (f.beat * 60.0 / bpm * sr);
            const int n = std::min (one.getNumSamples(), total - start);
            if (n <= 0 || start < 0) continue;
            for (int ch = 0; ch < 2; ++ch) out.addFrom (ch, start, one, ch, 0, n);
        }
        const float peak = out.getMagnitude (0, out.getNumSamples());
        if (peak > 0.89f) out.applyGain (0.89f / peak);   // -1 dBFS
        return out;
    }

    // What goes to the DAW for a lane: the generated clip, or for the user's own section only the layers.
    const Clip* exportClip (const GenSection& s, Lane l)
    {
        if (l == Lane::Fx) return s.clips[(size_t) l].fx.empty() ? nullptr : &s.clips[(size_t) l];
        const auto& c = s.isReference ? s.layers[(size_t) l] : s.clips[(size_t) l];
        return c.notes.empty() ? nullptr : &c;
    }
    bool laneExported (const GenSection& s, Lane l) { return exportClip (s, l) != nullptr; }

    juce::Colour laneColour (Lane l)
    {
        static const juce::uint32 cols[] = { 0xffff5a8a, 0xff9a6bff, 0xff4fd6ff, 0xffffc857, 0xff5cf2b0, 0xffff8f3c };
        return juce::Colour (cols[(int) l]);
    }
}

juce::File defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("FINISH");
}

juce::File liveInbox()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Music").getChildFile ("FINISH").getChildFile ("inbox");
}

juce::String fileStem (const Arrangement& arr, int section, const juce::String& part)
{
    const auto& s = arr.sections[(size_t) section];
    return twoDigits (section) + " " + safeName (s.name) + " - " + part;
}

juce::File writeClipMidi (const Arrangement& arr, int section, Lane lane, const juce::File& folder)
{
    if (section < 0 || section >= (int) arr.sections.size()) return {};
    const auto& s = arr.sections[(size_t) section];
    const auto* clipPtr = exportClip (s, lane);
    if (clipPtr == nullptr || lane == Lane::Fx) return {};
    const auto& clip = *clipPtr;
    folder.createDirectory();
    juce::MidiMessageSequence seq;
    addHeader (seq, arr.bpm, s.name + " " + laneName (lane) + (s.isReference ? " ACCENTS" : ""));
    addLane (seq, clip, 0.0, midiChannelFor (lane));
    seq.addEvent (juce::MidiMessage::endOfTrack(), s.lengthBeats() * tpq);
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    const auto file = folder.getChildFile (fileStem (arr, section, juce::String (laneName (lane)) + (s.isReference ? " ACCENTS" : "")) + ".mid");
    return writeMidi (mf, file) ? file : juce::File();
}

juce::File writeSectionFx (const Arrangement& arr, int section, const juce::File& folder, double sampleRate)
{
    if (section < 0 || section >= (int) arr.sections.size()) return {};
    const auto& s = arr.sections[(size_t) section];
    if (s.clips[(size_t) Lane::Fx].fx.empty()) return {};
    folder.createDirectory();
    const auto buffer = renderFxLane (s.clips[(size_t) Lane::Fx], s.lengthBeats(), arr.bpm, sampleRate);
    const auto file = folder.getChildFile (fileStem (arr, section, "FX") + ".wav");
    return writeWav (buffer, sampleRate, file) ? file : juce::File();
}

juce::File writeFullMidi (const Arrangement& arr, const juce::File& file, bool includeReference)
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    juce::MidiMessageSequence conductor;
    addHeader (conductor, arr.bpm, "FINISH " + juce::String (flavourName (arr.flavour)));
    for (auto& s : arr.sections)
        conductor.addEvent (juce::MidiMessage::textMetaEvent (6, s.name), s.startBar * 4.0 * tpq);   // markers
    conductor.addEvent (juce::MidiMessage::endOfTrack(), arr.totalBars() * 4.0 * tpq);
    mf.addTrack (conductor);
    for (int l = 0; l < numMidiLanes; ++l)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String ("FINISH ") + laneName ((Lane) l)), 0);
        bool any = false;
        for (auto& s : arr.sections)
        {
            const Clip* c = (s.isReference && includeReference) ? &s.clips[(size_t) l] : exportClip (s, (Lane) l);
            if (c == nullptr) continue;
            any |= ! c->notes.empty();
            addLane (seq, *c, s.startBar * 4.0, midiChannelFor ((Lane) l));
        }
        if (! any) continue;
        seq.addEvent (juce::MidiMessage::endOfTrack(), arr.totalBars() * 4.0 * tpq);
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    file.getParentDirectory().createDirectory();
    return writeMidi (mf, file) ? file : juce::File();
}

Result exportAll (const Arrangement& arr, const MusicalDNA& dna, const juce::File& folder, double sampleRate)
{
    Result r;
    r.folder = folder;
    if (arr.empty()) { r.error = utf8 ("Rien à exporter : génère d'abord un arrangement."); return r; }
    if (! folder.createDirectory()) { r.error = "Impossible de créer " + folder.getFullPathName(); return r; }

    r.fullMidi = writeFullMidi (arr, folder.getChildFile ("FINISH - " + juce::String (flavourName (arr.flavour)) + " - arrangement complet.mid"), false);
    if (r.fullMidi.existsAsFile()) r.files.add (r.fullMidi);

    // FX of the whole arrangement on one timeline (drop at bar 1 next to the full MIDI file).
    {
        Clip all;
        for (auto& s : arr.sections)
            for (auto f : s.clips[(size_t) Lane::Fx].fx) { f.beat += s.startBar * 4.0; all.fx.push_back (f); }
        if (! all.fx.empty())
        {
            const auto full = folder.getChildFile ("FINISH - FX arrangement complet.wav");
            if (writeWav (renderFxLane (all, arr.totalBars() * 4.0, arr.bpm, sampleRate), sampleRate, full)) r.files.add (full);
        }
    }

    auto parts = folder.getChildFile ("Clips par section");
    for (int i = 0; i < (int) arr.sections.size(); ++i)
    {
        const auto& s = arr.sections[(size_t) i];
        for (int l = 0; l < numMidiLanes; ++l)
            if (laneExported (s, (Lane) l))
            {
                const auto f = writeClipMidi (arr, i, (Lane) l, parts);
                if (f.existsAsFile()) { r.files.add (f); r.clipFiles[juce::String (i) + ":" + juce::String (l)] = f; }
            }
        if (laneExported (s, Lane::Fx))
        {
            const auto f = writeSectionFx (arr, i, parts, sampleRate);
            if (f.existsAsFile()) { r.files.add (f); r.clipFiles[juce::String (i) + ":" + juce::String ((int) Lane::Fx)] = f; }
        }
    }

    // Guide
    juce::String g;
    g << "FINISH - " << flavourName (arr.flavour) << "\n" << juce::String::repeatedString ("=", 40) << "\n\n";
    g << "Tempo : " << juce::String (arr.bpm, 2) << " BPM    Tonalite : " << dna.keyName()
      << utf8 (" (confiance ") << juce::roundToInt (dna.keyConfidence * 100.0f) << " %)    Style : " << styleName (dna.style) << "\n";
    for (auto& u : dna.uncertainties()) g << "  ! " << u << "\n";
    g << "\n" << arr.summary << "\n\nSTRUCTURE\n";
    for (auto& s : arr.sections)
        g << "  mesure " << juce::String (s.startBar + 1).paddedLeft (' ', 3) << "  " << s.name.paddedRight (' ', 9) << " "
          << juce::String (s.bars).paddedLeft (' ', 2) << " mesures   " << s.recipe << (s.isReference ? utf8 ("  [TON DROP]") : juce::String()) << "\n";
    g << utf8 ("\nLE MIDI NE CONTIENT PAS DE SONS : charge un instrument sur chaque piste.\n");
    for (int l = 0; l < numLanes; ++l) g << "  " << juce::String (laneName ((Lane) l)).paddedRight (' ', 7) << " -> " << laneInstrumentHint ((Lane) l) << "\n";
    g << utf8 ("\nCC74 (filtre) : les montées et ouvertures de filtre sont écrites en CC74. Dans Ableton, mappe CC74\n"
               "sur le filtre de l'instrument (ou supprime-les si tu n'en veux pas).\n\n"
               "DANS ABLETON\n"
               "  - Glisse \"arrangement complet.mid\" dans la vue Arrangement : une piste par partie.\n"
               "  - Glisse \"FX arrangement complet.wav\" sur une piste audio, mesure 1.\n"
               "  - Ou prends les clips un par un dans \"Clips par section\".\n"
               "  - Ton drop reste le tien : place-le à la mesure de la section DROP.\n");
    r.guide = folder.getChildFile ("LISEZ-MOI FINISH.txt");
    r.guide.replaceWithText (g);
    r.files.add (r.guide);

    r.ok = r.fullMidi.existsAsFile();
    if (! r.ok) r.error = utf8 ("Écriture du fichier MIDI impossible dans ") + folder.getFullPathName();
    return r;
}

// ======================================================================================
// Ableton Live bridge

static juce::Array<juce::File> liveUserLibraries()
{
    juce::Array<juce::File> libs;
    auto addIfDir = [&] (const juce::File& f) { if (f.isDirectory() && ! libs.contains (f)) libs.add (f); };
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    // Library.cfg in Live's preferences knows a custom User Library location.
    juce::Array<juce::File> prefRoots;
   #if JUCE_MAC
    prefRoots.add (home.getChildFile ("Library/Preferences/Ableton"));
   #elif JUCE_WINDOWS
    prefRoots.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ableton"));
   #else
    prefRoots.add (home.getChildFile (".ableton"));
   #endif
    for (auto& root : prefRoots)
        for (auto& cfg : root.findChildFiles (juce::File::findFiles, true, "Library.cfg"))
        {
            const auto text = cfg.loadFileAsString();
            const int ul = text.indexOf ("UserLibrary");
            if (ul < 0) continue;
            const int p = text.indexOf (ul, "ProjectPath Value=\"");
            if (p < 0) continue;
            const int start = p + 19, end = text.indexOf (start, "\"");
            if (end <= start) continue;
            const juce::File path (text.substring (start, end));
            addIfDir (path.getChildFile ("User Library"));
            if (path.getFileName() == "User Library") addIfDir (path);
        }
    addIfDir (home.getChildFile ("Music/Ableton/User Library"));
    addIfDir (home.getChildFile ("Documents/Ableton/User Library"));
    if (libs.isEmpty())   // Live never ran? create the default one so the script is there when it does.
    {
       #if JUCE_WINDOWS
        libs.add (home.getChildFile ("Documents/Ableton/User Library"));
       #else
        libs.add (home.getChildFile ("Music/Ableton/User Library"));
       #endif
    }
    return libs;
}

juce::Array<juce::File> installLiveScript (juce::String& report)
{
    juce::Array<juce::File> written;
    for (auto& lib : liveUserLibraries())
    {
        const auto dir = lib.getChildFile ("Remote Scripts").getChildFile ("FINISH");
        if (! dir.createDirectory()) continue;
        bool ok = dir.getChildFile ("__init__.py").replaceWithData (BinaryData::__init___py, (size_t) BinaryData::__init___pySize);
        ok = ok && dir.getChildFile ("finish_bridge.py").replaceWithData (BinaryData::finish_bridge_py, (size_t) BinaryData::finish_bridge_pySize);
        if (ok) written.add (dir);
    }
    liveInbox().createDirectory();
    report = written.isEmpty() ? utf8 ("Impossible d'installer le script Live.")
                               : utf8 ("Script Live installé : ") + written.getFirst().getFullPathName();
    return written;
}

juce::String buildRequestJson (const Arrangement& arr, const MusicalDNA& dna, const Result& exported, int startBar,
                               bool loadInstruments, const juce::String& requestId)
{
    auto* root = new juce::DynamicObject();
    juce::var rootVar (root);
    root->setProperty ("id", requestId);
    root->setProperty ("version", 1);
    root->setProperty ("bpm", arr.bpm);
    root->setProperty ("key", dna.keyName());
    root->setProperty ("startBar", startBar);
    root->setProperty ("loadInstruments", loadInstruments);
    root->setProperty ("name", juce::String ("FINISH ") + flavourName (arr.flavour));

    juce::Array<juce::var> sections;
    for (auto& s : arr.sections)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", s.name);
        o->setProperty ("startBar", s.startBar);
        o->setProperty ("bars", s.bars);
        o->setProperty ("reference", s.isReference);
        sections.add (juce::var (o));
    }
    root->setProperty ("sections", sections);

    juce::Array<juce::var> tracks;
    for (int l = 0; l < numLanes; ++l)
    {
        const auto lane = (Lane) l;
        auto* t = new juce::DynamicObject();
        t->setProperty ("name", juce::String ("FINISH ") + laneName (lane));
        t->setProperty ("lane", laneName (lane));
        t->setProperty ("kind", lane == Lane::Fx ? "audio" : "midi");
        t->setProperty ("hint", laneInstrumentHint (lane));
        const auto c = laneColour (lane);
        t->setProperty ("color", (int) ((c.getRed() << 16) | (c.getGreen() << 8) | c.getBlue()));
        juce::Array<juce::var> clips;
        for (int i = 0; i < (int) arr.sections.size(); ++i)
        {
            const auto& s = arr.sections[(size_t) i];
            if (! laneExported (s, lane)) continue;
            auto* cl = new juce::DynamicObject();
            cl->setProperty ("name", s.isReference ? s.name + " ACCENTS" : s.name);
            cl->setProperty ("section", s.name);
            cl->setProperty ("start", s.startBar * 4.0);
            cl->setProperty ("length", s.lengthBeats());
            if (lane == Lane::Fx)
            {
                auto it = exported.clipFiles.find (juce::String (i) + ":" + juce::String (l));
                if (it == exported.clipFiles.end()) continue;
                cl->setProperty ("file", it->second.getFullPathName());
            }
            else
            {
                juce::Array<juce::var> notes;
                for (auto& n : exportClip (s, lane)->notes)
                    notes.add (juce::Array<juce::var> { n.pitch, std::round (n.beat * 1000.0) / 1000.0, std::round (n.length * 1000.0) / 1000.0,
                                                        juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f)) });
                cl->setProperty ("notes", notes);
            }
            clips.add (juce::var (cl));
        }
        if (clips.isEmpty()) continue;
        t->setProperty ("clips", clips);
        tracks.add (juce::var (t));
    }
    root->setProperty ("tracks", tracks);
    return juce::JSON::toString (rootVar, true);
}

bool sendToLive (const Arrangement& arr, const MusicalDNA& dna, const Result& exported, int startBar,
                 bool loadInstruments, juce::String& requestId, juce::String& error)
{
    const auto inbox = liveInbox();
    if (! inbox.createDirectory()) { error = "Dossier inaccessible : " + inbox.getFullPathName(); return false; }
    requestId = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
    inbox.getChildFile ("response.json").deleteFile();
    const auto tmp = inbox.getChildFile ("request.json.tmp");
    if (! tmp.replaceWithText (buildRequestJson (arr, dna, exported, startBar, loadInstruments, requestId)))
    {
        error = utf8 ("Écriture impossible dans ") + inbox.getFullPathName();
        return false;
    }
    if (! tmp.moveFileTo (inbox.getChildFile ("request.json")))
    {
        error = "Impossible de publier la demande pour Live.";
        return false;
    }
    return true;
}

bool readLiveResponse (const juce::String& requestId, bool& ok, juce::String& message, juce::StringArray& warnings)
{
    const auto f = liveInbox().getChildFile ("response.json");
    if (! f.existsAsFile()) return false;
    const auto v = juce::JSON::parse (f);
    if (! v.isObject()) return false;
    const auto id = v.getProperty ("id", juce::var()).toString();
    if (id.isNotEmpty() && id != requestId) return false;
    ok = (bool) v.getProperty ("ok", false);
    message = v.getProperty (ok ? "message" : "error", juce::var()).toString();
    warnings.clear();
    if (auto* w = v.getProperty ("warnings", juce::var()).getArray())
        for (auto& x : *w) warnings.add (x.toString());
    return true;
}

bool isBridgeAlive (double withinSeconds)
{
    const auto f = liveInbox().getChildFile ("bridge.json");
    if (! f.existsAsFile()) return false;
    const auto age = (juce::Time::getCurrentTime() - f.getLastModificationTime()).inSeconds();
    return age <= withinSeconds;
}

} // namespace ArrangementExport
