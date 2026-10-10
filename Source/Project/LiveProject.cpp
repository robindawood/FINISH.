#include "LiveProject.h"
#include "../Compose/ComposeTypes.h"

const char* trackRoleName (TrackRole r)
{
    static const char* n[] = { "DRUMS", "PERC", "BASS", "CHORDS", "LEAD", "PAD", "FX", "VOCAL", "AUDIO", "OTHER" };
    return n[(int) r];
}

juce::String trackRoleLabel (TrackRole r)
{
    static const char* n[] = { "batterie", "percussions", "basse", "accords", "mélodie", "nappe", "effets", "voix", "boucle audio", "autre" };
    return utf8 (n[(int) r]);
}

bool LiveDevice::isInstrument() const noexcept
{
    static const char* inst[] = { "InstrumentGroupDevice", "DrumGroupDevice", "InstrumentVector", "UltraAnalog", "Drift", "Operator",
                                  "OriginalSimpler", "MultiSampler", "Collision", "StringStudio", "InstrumentImpulse", "LoungeLizard",
                                  "PluginDevice", "AuPluginDevice", "MxDeviceInstrument", "Tension", "Meld", "Electric" };
    for (auto* s : inst) if (className == s) return true;
    return false;
}

juce::String LiveTrack::instrumentName() const
{
    for (auto& d : devices) if (d.isInstrument()) return d.name;
    return {};
}

bool LiveTrack::hasDrumRack() const
{
    for (auto& d : devices) if (d.isDrumRack()) return true;
    return false;
}

double LiveProject::arrangementEnd() const noexcept
{
    double e = 0.0;
    for (auto& t : tracks)
        for (auto& c : t.arrangement) e = std::max (e, c.end);
    return e;
}

int LiveProject::countClips (bool arrangementOnly) const noexcept
{
    int n = 0;
    for (auto& t : tracks) n += (int) t.arrangement.size() + (arrangementOnly ? 0 : (int) t.session.size());
    return n;
}

int LiveProject::countNotes() const noexcept
{
    int n = 0;
    for (auto& t : tracks)
    {
        for (auto& c : t.arrangement) n += (int) c.notes.size();
        for (auto& c : t.session) n += (int) c.notes.size();
    }
    return n;
}

// --------------------------------------------------------------------------------------
static double num (const juce::var& v, double def = 0.0) { return v.isVoid() ? def : (double) v; }

static LiveClip clipFrom (const juce::var& c, bool session)
{
    LiveClip k;
    k.session = session;
    k.index = (int) num (c["index"]);
    k.name = c["name"].toString();
    k.midi = (bool) c["midi"];
    k.start = num (c["start"]);
    k.end = num (c["end"]);
    k.length = num (c["length"]);
    k.loopStart = num (c["loopStart"]);
    k.loopEnd = num (c["loopEnd"], k.loopStart + k.length);
    if (k.loopEnd <= k.loopStart + 1.0e-6) k.loopEnd = k.loopStart + juce::jmax (0.25, k.length);
    k.looping = (bool) c["looping"];
    k.muted = (bool) c["muted"];
    if (session) { k.start = 0.0; k.end = k.loopLength(); }
    if (auto* arr = c["notes"].getArray())
    {
        k.notes.reserve ((size_t) arr->size());
        for (auto& n : *arr)
            if (auto* a = n.getArray(); a != nullptr && a->size() >= 4)
            {
                LiveNote ln;
                ln.pitch = juce::jlimit (0, 127, (int) a->getReference (0));
                ln.start = (double) a->getReference (1);
                ln.length = juce::jmax (0.01, (double) a->getReference (2));
                ln.velocity = juce::jlimit (1, 127, (int) a->getReference (3));
                k.notes.push_back (ln);
            }
        std::sort (k.notes.begin(), k.notes.end(), [] (auto& a, auto& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    }
    k.file = c["file"].toString();
    return k;
}

LiveProject LiveProject::fromJson (const juce::var& v)
{
    LiveProject p;
    if (! v.isObject()) { p.error = "project.json illisible"; return p; }
    p.bridgeVersion = v["bridge"].toString();
    p.liveVersion = v["live"].toString();
    p.readTime = num (v["time"]);
    auto caps = v["caps"];
    p.caps.arrangementRead = (bool) caps["arrangement_read"];
    p.caps.createMidiClip = (bool) caps["create_midi_clip"];
    p.caps.createAudioClip = (bool) caps["create_audio_clip"];
    p.caps.duplicateToArrangement = (bool) caps["duplicate_to_arrangement"];
    p.caps.deleteClip = (bool) caps["delete_clip"];
    p.caps.undoStep = (bool) caps["undo_step"];
    p.tempo = num (v["tempo"], 120.0);
    if (auto* s = v["signature"].getArray(); s != nullptr && s->size() == 2)
    {
        p.sigNum = juce::jlimit (1, 32, (int) s->getReference (0));
        p.sigDen = juce::jlimit (1, 32, (int) s->getReference (1));
    }
    p.rootNote = (int) num (v["rootNote"], -1);
    p.scale = v["scale"].toString();
    p.songLength = num (v["songLength"]);
    if (auto* l = v["loop"].getArray(); l != nullptr && l->size() == 3)
    {
        p.loopStart = (double) l->getReference (0);
        p.loopLength = (double) l->getReference (1);
        p.loopOn = (bool) l->getReference (2);
    }
    p.position = num (v["position"]);
    if (auto* cues = v["cues"].getArray())
        for (auto& c : *cues)
            if (auto* a = c.getArray(); a != nullptr && a->size() == 2)
                p.cues.push_back ({ a->getReference (0).toString(), (double) a->getReference (1) });
    if (auto* sc = v["scenes"].getArray())
        for (auto& s : *sc) p.scenes.add (s.toString());
    p.selectedTrack = (int) num (v["selectedTrack"], -1);

    if (auto* tracks = v["tracks"].getArray())
        for (auto& t : *tracks)
        {
            LiveTrack lt;
            lt.index = (int) num (t["index"], (double) p.tracks.size());
            lt.name = t["name"].toString();
            lt.midi = (bool) t["midi"];
            lt.mute = (bool) t["mute"];
            lt.group = (bool) t["group"];
            lt.grouped = (bool) t["grouped"];
            lt.frozen = (bool) t["frozen"];
            lt.selected = (bool) t["selected"];
            if (auto* devs = t["devices"].getArray())
                for (auto& d : *devs)
                {
                    LiveDevice ld;
                    ld.name = d["name"].toString();
                    ld.className = d["class"].toString();
                    if (auto* pads = d["pads"].getArray())
                        for (auto& pd : *pads)
                            if (auto* a = pd.getArray(); a != nullptr && a->size() == 2)
                                ld.pads.push_back ({ (int) a->getReference (0), a->getReference (1).toString() });
                    lt.devices.push_back (ld);
                }
            if (auto* arr = t["arrangement"].getArray())
                for (auto& c : *arr) lt.arrangement.push_back (clipFrom (c, false));
            if (auto* ses = t["session"].getArray())
                for (auto& c : *ses) lt.session.push_back (clipFrom (c, true));
            std::sort (lt.arrangement.begin(), lt.arrangement.end(), [] (auto& a, auto& b) { return a.start < b.start; });
            p.tracks.push_back (std::move (lt));
        }
    p.valid = true;
    return p;
}

LiveProject LiveProject::fromFile (const juce::File& f)
{
    if (! f.existsAsFile()) { LiveProject p; p.error = "project.json absent"; return p; }
    return fromJson (juce::JSON::parse (f.loadFileAsString()));
}

// --------------------------------------------------------------------------------------
namespace LiveNotes
{
std::vector<LiveNote> loopContent (const LiveClip& c)
{
    std::vector<LiveNote> out;
    for (auto n : c.notes)
        if (n.start >= c.loopStart - 1.0e-6 && n.start < c.loopEnd - 1.0e-6)
        {
            n.start -= c.loopStart;
            n.length = std::min (n.length, c.loopLength() - n.start);
            out.push_back (n);
        }
    return out;
}

std::vector<LiveNote> playedIn (const LiveClip& c, double from, double to)
{
    std::vector<LiveNote> out;
    if (c.muted || ! c.midi) return out;
    const double s = std::max (from, c.start), e = std::min (to, c.end);
    if (e <= s) return out;
    if (c.looping)
    {
        const double L = c.loopLength();
        const auto content = loopContent (c);
        const int k0 = (int) std::floor ((s - c.start) / L), k1 = (int) std::ceil ((e - c.start) / L);
        for (int k = k0; k <= k1; ++k)
            for (auto n : content)
            {
                const double t = c.start + k * L + n.start;
                if (t >= s - 1.0e-6 && t < e - 1.0e-6)
                {
                    n.start = t - from;
                    out.push_back (n);
                }
            }
    }
    else
    {
        for (auto n : c.notes)
        {
            const double t = c.start + (n.start - c.loopStart);
            if (t >= s - 1.0e-6 && t < e - 1.0e-6)
            {
                n.start = t - from;
                out.push_back (n);
            }
        }
    }
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    return out;
}

std::vector<LiveNote> tiled (const LiveClip& c, double lengthBeats)
{
    std::vector<LiveNote> out;
    if (! c.midi) return out;
    const auto content = loopContent (c);
    const double L = c.loopLength();
    if (content.empty() || L <= 0.0) return out;
    for (double off = 0.0; off < lengthBeats - 1.0e-6; off += L)
        for (auto n : content)
        {
            n.start += off;
            if (n.start >= lengthBeats - 1.0e-6) break;
            n.length = std::min (n.length, lengthBeats - n.start);
            out.push_back (n);
        }
    return out;
}
}
