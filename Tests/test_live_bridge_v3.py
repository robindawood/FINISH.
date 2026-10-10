"""FINISH bridge v3 against a mock of Ableton Live's object model.

    python3 Tests/test_live_bridge_v3.py --fixture out.json        # writes the snapshot of the demo set
    python3 Tests/test_live_bridge_v3.py [apply_request.json]      # snapshot + apply + undo checks

The demo set mirrors a typical sketch: 8 tracks in D minor at 115 BPM, an intro-ish part (bars 1-16:
oboe + keys) and a drop (bars 17-24: everything). The checks:
  * the snapshot carries tempo, tracks, devices, drum pads, Arrangement + Session clips and every MIDI note;
  * an apply request only ADDS clips, on the user's own tracks, never where a clip already is;
  * nothing the user made is modified (names, notes, positions);
  * undo removes exactly what the apply created.
Both "Live 12" (create_midi_clip, delete_clip...) and "Live 11" (duplicate_clip_to_arrangement only) APIs.
"""
import copy
import importlib
import json
import os
import shutil
import sys
import tempfile
import types


class MidiNoteSpecification(object):
    def __init__(self, pitch, start_time, duration, velocity, mute):
        self.pitch, self.start_time, self.duration, self.velocity, self.mute = pitch, start_time, duration, velocity, mute


class MidiNote(object):
    def __init__(self, pitch, start, dur, vel):
        self.pitch, self.start_time, self.duration, self.velocity, self.mute = pitch, start, dur, vel, False


class MockClip(object):
    def __init__(self, name, start, length, midi=True, notes=None, file_path="", looping=True):
        self.name = name
        self.start_time = float(start)
        self.length = float(length)
        self.end_time = self.start_time + self.length
        self.is_midi_clip = midi
        self.is_audio_clip = not midi
        self.loop_start, self.loop_end = 0.0, float(length)
        self.looping = looping
        self.muted = False
        self.color = 0
        self.file_path = file_path
        self.warping = True
        self._notes = [MidiNote(*n) for n in (notes or [])]

    def get_notes_extended(self, from_pitch, pitch_span, from_time, time_span):
        assert self.is_midi_clip
        return tuple(n for n in self._notes if from_pitch <= n.pitch < from_pitch + pitch_span
                     and from_time <= n.start_time < from_time + time_span)

    def add_new_notes(self, specs):
        for s in specs:
            assert 0 <= s.pitch < 128 and s.duration > 0 and 0 < s.velocity <= 127, (s.pitch, s.duration, s.velocity)
            assert -1e-6 <= s.start_time < self.length + 1e-6, "note outside clip (%s >= %s)" % (s.start_time, self.length)
            self._notes.append(MidiNote(s.pitch, s.start_time, s.duration, s.velocity))

    def remove_notes_extended(self, *a):
        self._notes = []

    def signature(self):
        return (self.name, self.start_time, self.end_time, tuple((n.pitch, n.start_time, n.duration, n.velocity) for n in self._notes))


class MockSlot(object):
    def __init__(self):
        self.clip = None

    @property
    def has_clip(self):
        return self.clip is not None

    def create_clip(self, length):
        self.clip = MockClip("", 0.0, length)

    def delete_clip(self):
        self.clip = None


class MockDevice(object):
    def __init__(self, name, class_name, pads=None):
        self.name, self.class_name, self.type = name, class_name, 1
        self.can_have_drum_pads = pads is not None
        self.drum_pads = [types.SimpleNamespace(note=n, name=nm, chains=[1]) for n, nm in (pads or [])]


class MockTrack(object):
    def __init__(self, song, name, midi, live12):
        self.song, self.name, self.has_midi_input = song, name, midi
        self.has_audio_input = not midi
        self.color, self.mute, self.is_foldable, self.is_grouped, self.is_frozen = 0, False, False, False, False
        self.devices = []
        self.arrangement_clips = []
        self._slots = []
        self.live12 = live12
        if live12:
            self.create_midi_clip = self._create_midi_clip
            self.create_audio_clip = self._create_audio_clip
            self.delete_clip = self._delete_clip

    @property
    def clip_slots(self):
        while len(self._slots) < len(self.song.scenes):
            self._slots.append(MockSlot())
        return self._slots[:len(self.song.scenes)]

    def _check_free(self, start, end):
        for c in self.arrangement_clips:
            assert not (c.start_time < end - 1e-6 and c.end_time > start + 1e-6), \
                "bridge wrote over an existing clip on %s at %.2f" % (self.name, start)

    def _create_midi_clip(self, start, length):
        assert self.has_midi_input
        self._check_free(start, start + length)
        c = MockClip("", start, length)
        self.arrangement_clips.append(c)
        self.arrangement_clips.sort(key=lambda c: c.start_time)
        return c

    def _create_audio_clip(self, path, position):
        assert os.path.exists(path)
        c = MockClip("", position, 8.0, midi=False, file_path=path)
        self._check_free(c.start_time, c.end_time)
        self.arrangement_clips.append(c)
        return c

    def duplicate_clip_to_arrangement(self, clip, time):
        assert (clip.is_midi_clip == self.has_midi_input) or not clip.is_midi_clip
        c = MockClip(clip.name, time, clip.length, clip.is_midi_clip, file_path=clip.file_path)
        c._notes = [MidiNote(n.pitch, n.start_time, n.duration, n.velocity) for n in clip._notes]
        self._check_free(c.start_time, c.end_time)
        self.arrangement_clips.append(c)
        self.arrangement_clips.sort(key=lambda c: c.start_time)
        return c

    def _delete_clip(self, clip):
        self.arrangement_clips.remove(clip)


class MockSong(object):
    def __init__(self, live12):
        self.live12 = live12
        self.tempo = 115.0
        self.signature_numerator, self.signature_denominator = 4, 4
        self.root_note, self.scale_name = 2, "Minor"
        self.loop_start, self.loop_length, self.loop = 64.0, 32.0, True
        self.current_song_time = 0.0
        self.is_playing = False
        self.tracks = []
        self.scenes = [types.SimpleNamespace(name="") for _ in range(4)]
        self.cue_points = []
        self.view = types.SimpleNamespace(selected_track=None, detail_clip=None)
        self.undo_steps = 0
        self._open_steps = 0

    @property
    def song_length(self):
        return max([c.end_time for t in self.tracks for c in t.arrangement_clips] + [0.0])

    def begin_undo_step(self):
        self._open_steps += 1

    def end_undo_step(self):
        self._open_steps -= 1
        self.undo_steps += 1

    def create_midi_track(self, index):
        self.tracks.insert(index, MockTrack(self, "", True, self.live12))

    def create_audio_track(self, index):
        self.tracks.insert(index, MockTrack(self, "", False, self.live12))

    def duplicate_track(self, index):
        src = self.tracks[index]
        t = MockTrack(self, src.name, src.has_midi_input, self.live12)
        t.devices = list(src.devices)
        for c in src.arrangement_clips:
            d = MockClip(c.name, c.start_time, c.length, c.is_midi_clip, file_path=c.file_path)
            d._notes = list(c._notes)
            t.arrangement_clips.append(d)
        self.tracks.insert(index + 1, t)

    def delete_track(self, index):
        del self.tracks[index]

    def create_scene(self, index):
        self.scenes.append(types.SimpleNamespace(name=""))

    def delete_scene(self, index):
        del self.scenes[index]
        for t in self.tracks:
            if len(t._slots) > index:
                del t._slots[index]

    def set_or_delete_cue(self):
        for c in self.cue_points:
            if abs(c.time - self.current_song_time) < 1e-6:
                self.cue_points.remove(c)
                return
        self.cue_points.append(types.SimpleNamespace(name="", time=self.current_song_time))


# ---------------------------------------------------------------- the demo set
D_MINOR_MOTIF = [  # 2 bars, oboe
    (62, 0.0, 0.5, 96), (65, 0.5, 0.5, 90), (69, 1.0, 1.0, 100), (67, 2.0, 0.5, 88), (65, 2.5, 0.5, 86),
    (64, 3.0, 1.0, 92), (62, 4.0, 0.75, 96), (65, 4.75, 0.25, 80), (69, 5.0, 0.5, 98), (72, 5.5, 0.5, 100),
    (70, 6.0, 1.0, 94), (69, 7.0, 1.0, 90)]
CHORDS = []  # Dm Bb F C, one per bar
for bar, chord in enumerate([(50, 53, 57), (46, 50, 53), (53, 57, 60), (48, 52, 55)]):
    for p in chord:
        CHORDS.append((p, bar * 4.0, 3.9, 84))
FLUTE = [(74, 0.0, 1.5, 90), (72, 1.5, 0.5, 80), (69, 2.0, 2.0, 88), (77, 4.0, 1.0, 92), (76, 5.0, 1.0, 86),
         (74, 6.0, 2.0, 90)]
BASS = []
for bar, root in enumerate([38, 34, 41, 36]):
    for k in range(4):
        BASS.append((root, bar * 4.0 + k + 0.5, 0.45, 100))
DRUMS = []
for b in range(4):
    DRUMS.append((36, float(b), 0.25, 120))
    DRUMS.append((42, b + 0.5, 0.2, 90))
    if b % 2 == 1:
        DRUMS.append((39, float(b), 0.25, 105))
DRUMS.append((46, 3.5, 0.4, 80))
PERC = [(60, 0.75, 0.2, 80), (62, 1.75, 0.2, 70), (60, 2.75, 0.2, 82), (64, 3.25, 0.2, 76)]
CLAP = [(39, 1.0, 0.25, 110), (39, 3.0, 0.25, 110)]


def tile(notes, unit, count):
    return [(p, s + unit * i, d, v) for i in range(count) for (p, s, d, v) in notes]


def demo_set(live12, wav_path):
    song = MockSong(live12)

    def add(name, midi, devices=()):
        t = MockTrack(song, name, midi, live12)
        t.devices = list(devices)
        song.tracks.append(t)
        return t

    oboe = add("Oboe", True, [MockDevice("Oboe", "InstrumentGroupDevice"), MockDevice("Reverb", "Reverb")])
    oboe.arrangement_clips = [MockClip("Idea | D Minor | Dreamy | Oboe", 0.0, 64.0, notes=tile(D_MINOR_MOTIF, 8.0, 8)),
                              MockClip("Idea | D Minor | Dreamy | Oboe", 64.0, 32.0, notes=tile(D_MINOR_MOTIF, 8.0, 4))]
    keys = add("Key Synth Pad", True, [MockDevice("Wavetable", "InstrumentVector")])
    keys.arrangement_clips = [MockClip("MIDI Key Synth Pad", 0.0, 64.0, notes=tile(CHORDS, 16.0, 4)),
                              MockClip("MIDI Key Synth Pad", 64.0, 32.0, notes=tile(CHORDS, 16.0, 2))]
    flute = add("Flute", True, [MockDevice("Flute", "InstrumentGroupDevice")])
    flute.arrangement_clips = [MockClip("Idea | D Minor | Flute", 64.0, 32.0, notes=tile(FLUTE, 8.0, 4))]
    bass = add("Vintage Low Synth", True, [MockDevice("Analog", "UltraAnalog"), MockDevice("Saturator", "Saturator")])
    bass.arrangement_clips = [MockClip("Low Synth D min", 64.0, 32.0, notes=tile(BASS, 16.0, 2))]
    drums = add("Flavonoid Drums", True, [MockDevice("Flavonoid Kit", "DrumGroupDevice",
                                                   [(36, "Kick"), (39, "Clap"), (42, "Closed Hat"), (46, "Open Hat")])])
    drums.arrangement_clips = [MockClip("Flavonoid Drums", 64.0, 32.0, notes=tile(DRUMS, 4.0, 8))]
    perc = add("Escape Perc", True, [MockDevice("Escape Perc", "DrumGroupDevice", [(60, "Shaker"), (62, "Tom"), (64, "Rim")])])
    perc.arrangement_clips = [MockClip("Escape Perc Snaps", 64.0, 32.0, notes=tile(PERC, 4.0, 8))]
    clap = add("Doppler Clap", True, [MockDevice("Doppler Clap", "DrumGroupDevice", [(39, "Clap")])])
    clap.arrangement_clips = [MockClip("Doppler2 Clap", 64.0, 32.0, notes=tile(CLAP, 4.0, 8))]
    hats = add("Uplifting House Hat", False, [MockDevice("EQ Eight", "Eq8")])
    hats.arrangement_clips = [MockClip("Uplifting House Hat", 64.0, 8.0, midi=False, file_path=wav_path),
                              MockClip("Uplifting House Hat", 72.0, 8.0, midi=False, file_path=wav_path),
                              MockClip("Uplifting House Hat", 80.0, 8.0, midi=False, file_path=wav_path),
                              MockClip("Uplifting House Hat", 88.0, 8.0, midi=False, file_path=wav_path)]
    # One idea still in the Session view
    oboe._slots = [MockSlot()]
    oboe._slots[0].clip = MockClip("Oboe idea B", 0.0, 8.0, notes=D_MINOR_MOTIF)
    song.view.selected_track = oboe
    return song


# ---------------------------------------------------------------- harness
def load_bridge(song, home):
    app = types.SimpleNamespace(get_major_version=lambda: 12 if song.live12 else 11, get_minor_version=lambda: 1,
                                get_bugfix_version=lambda: 0)
    live = types.ModuleType("Live")
    live.Clip = types.SimpleNamespace(MidiNoteSpecification=MidiNoteSpecification)
    live.Application = types.SimpleNamespace(get_application=lambda: app)
    sys.modules["Live"] = live
    os.environ["HOME"] = home
    os.environ["USERPROFILE"] = home
    here = os.path.dirname(os.path.abspath(__file__))
    pkg_root = os.path.join(home, "pkg")
    shutil.copytree(os.path.join(here, "..", "Resources", "LiveScript"), os.path.join(pkg_root, "FINISH"))
    sys.path.insert(0, pkg_root)
    for m in list(sys.modules):
        if m == "FINISH" or m.startswith("FINISH."):
            del sys.modules[m]
    finish = importlib.import_module("FINISH")
    c = types.SimpleNamespace(song=lambda: song, log_message=lambda m: None, show_message=lambda m: None)
    bridge = finish.create_instance(c)
    sys.path.remove(pkg_root)
    return bridge, os.path.join(home, "Music", "FINISH", "inbox")


def roundtrip(bridge, inbox, request):
    with open(os.path.join(inbox, "request.json"), "w") as f:
        json.dump(request, f)
    if os.path.exists(os.path.join(inbox, "response.json")):
        os.remove(os.path.join(inbox, "response.json"))
    for _ in range(12):
        bridge.update_display()
    with open(os.path.join(inbox, "response.json")) as f:
        resp = json.load(f)
    assert resp.get("id") == request.get("id"), resp
    return resp


def make_wav(path):
    import struct
    import wave
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(struct.pack("<%dh" % 4410, *([0] * 4410)))


def snapshot_fixture(out_path):
    home = tempfile.mkdtemp()
    wav = os.path.join(home, "hat.wav")
    make_wav(wav)
    song = demo_set(True, "/Users/demo/Samples/Uplifting House Hat.wav")
    bridge, inbox = load_bridge(song, home)
    resp = roundtrip(bridge, inbox, {"type": "snapshot", "id": "fx"})
    assert resp["ok"], resp
    shutil.copy(os.path.join(inbox, "project.json"), out_path)
    shutil.rmtree(home, ignore_errors=True)
    print("fixture written:", out_path, resp["message"])


def run_v3(apply_path=None, live12=True):
    home = tempfile.mkdtemp()
    wav = os.path.join(home, "hat.wav")
    make_wav(wav)
    song = demo_set(live12, wav)
    bridge, inbox = load_bridge(song, home)
    tag = "live12" if live12 else "live11"

    hb = json.load(open(os.path.join(inbox, "bridge.json")))
    assert hb["alive"] and hb["version"] == "3.0", hb
    assert hb["caps"]["duplicate_to_arrangement"]
    assert hb["caps"]["create_midi_clip"] == live12

    # ---- snapshot
    resp = roundtrip(bridge, inbox, {"type": "snapshot", "id": "s1"})
    assert resp["ok"], resp
    proj = json.load(open(os.path.join(inbox, "project.json")))
    assert abs(proj["tempo"] - 115.0) < 1e-9 and proj["signature"] == [4, 4]
    assert len(proj["tracks"]) == 8
    names = [t["name"] for t in proj["tracks"]]
    assert names[0] == "Oboe" and names[-1] == "Uplifting House Hat"
    total_notes = sum(len(c.get("notes", [])) for t in proj["tracks"] for c in t["arrangement"] + t["session"])
    expected = sum(len(c._notes) for t in song.tracks for c in t.arrangement_clips) + len(D_MINOR_MOTIF)
    assert total_notes == expected, (total_notes, expected)
    drums = proj["tracks"][4]
    assert drums["devices"][0]["class"] == "DrumGroupDevice" and [36, "Kick"] in drums["devices"][0]["pads"]
    assert proj["tracks"][7]["midi"] is False and proj["tracks"][7]["arrangement"][0]["file"] == wav
    assert proj["tracks"][0]["session"][0]["name"] == "Oboe idea B"
    assert proj["selectedTrack"] == 0

    before = {t.name: [c.signature() for c in t.arrangement_clips] for t in song.tracks}

    # ---- apply: either the plan written by the C++ test (built from the fixture) or a hand-made one
    if apply_path:
        req = json.load(open(apply_path))
        for op in req["ops"]:   # FX files of the C++ run live elsewhere; point them at a local WAV
            if op.get("file"):
                op["file"] = wav
    else:
        req = {"type": "apply", "id": "a1", "ops": [
            {"op": "clip", "track": 0, "trackName": "Oboe", "start": 96.0, "length": 32.0, "name": "FINISH BREAK",
             "notes": [[62, 0.0, 4.0, 90], [65, 4.0, 4.0, 90]]},
            {"op": "clip", "track": 4, "trackName": "Flavonoid Drums", "start": 128.0, "length": 32.0, "name": "FINISH DROP 2",
             "source": {"where": "arrangement", "start": 64.0}},
            {"op": "clip", "track": 7, "trackName": "Uplifting House Hat", "start": 128.0, "length": 16.0,
             "source": {"where": "arrangement", "start": 64.0}},
            {"op": "clip", "track": 1, "trackName": "Key Synth Pad", "start": 64.0, "length": 16.0, "name": "collides",
             "notes": [[50, 0.0, 1.0, 90]]},
            {"op": "newTrack", "ref": "@fx", "kind": "audio", "name": "FINISH FX"},
            {"op": "clip", "track": "@fx", "start": 120.0, "length": 8.0, "file": wav, "name": "RISER"},
            {"op": "cue", "time": 96.0, "name": "BREAK"}]}
    req["id"] = "a1"
    resp = roundtrip(bridge, inbox, req)
    assert resp["ok"], resp
    rep = resp["report"]
    assert not rep["errors"], rep["errors"]
    assert song.undo_steps == 1 and song._open_steps == 0

    # The user's clips are untouched
    for t in song.tracks:
        if t.name in before:
            originals = before[t.name]
            now = [c.signature() for c in t.arrangement_clips]
            for sig in originals:
                assert sig in now, "user clip modified on %s" % t.name
    added = sum(len(t.arrangement_clips) for t in song.tracks) - sum(len(v) for v in before.values())
    assert added > 0
    if not apply_path:
        assert any("occupees" in s for s in rep["skipped"]), rep
    assert len(song.scenes) == 4, "temporary scenes left behind"
    # No overlaps anywhere
    for t in song.tracks:
        cs = sorted(t.arrangement_clips, key=lambda c: c.start_time)
        for a, b in zip(cs, cs[1:]):
            assert a.end_time <= b.start_time + 1e-6, "overlap on %s" % t.name

    # ---- undo
    resp_u = roundtrip(bridge, inbox, {"type": "undo", "id": "u1", "target": "a1"})
    if live12:
        assert resp_u["ok"], resp_u
        after = {t.name: [c.signature() for c in t.arrangement_clips] for t in song.tracks}
        assert after == before, "undo did not restore the set"
        assert not song.cue_points
    print("  v3 %-7s %s | %s | skipped %d, manual %d | undo: %s" % (
        tag, resp["message"], "plan from C++" if apply_path else "hand-made", len(rep["skipped"]), len(rep["manual"]),
        resp_u.get("message", resp_u.get("error"))))
    shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--fixture":
        snapshot_fixture(sys.argv[2])
    else:
        plan = sys.argv[1] if len(sys.argv) > 1 else None
        run_v3(plan, True)
        run_v3(plan, False)
        print("Live bridge v3: OK")
