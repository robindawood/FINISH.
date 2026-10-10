"""Offline test of the FINISH Remote Script against a mock of Ableton Live's API.

    python3 Tests/test_live_bridge.py <request.json>

Checks both placement modes (Live 12 arrangement clips, older Live session scenes), that only NEW tracks
are created, that every note of the request lands in a clip, and that a response is written.
"""
import importlib
import json
import os
import shutil
import sys
import tempfile
import types


# ---------------------------------------------------------------- mock Live
class MidiNoteSpecification(object):
    def __init__(self, pitch, start_time, duration, velocity, mute):
        self.pitch, self.start_time, self.duration, self.velocity, self.mute = pitch, start_time, duration, velocity, mute


class MockClip(object):
    def __init__(self, length, start=0.0, audio_file=None):
        self.length, self.start_time, self.name, self.notes, self.file = length, start, "", [], audio_file

    def add_new_notes(self, specs):
        self.notes.extend(specs)

    def set_notes(self, notes):
        self.notes.extend(notes)


class MockSlot(object):
    def __init__(self):
        self.clip = None

    @property
    def has_clip(self):
        return self.clip is not None

    def create_clip(self, length):
        self.clip = MockClip(length)

    def create_audio_clip(self, path):
        self.clip = MockClip(0, audio_file=path)
        return self.clip


class MockTrack(object):
    def __init__(self, song, kind, arrangement_api):
        self.song, self.kind, self.name, self.color = song, kind, "", 0
        self.arrangement_clips = []
        self.devices = []
        if arrangement_api:
            self.create_midi_clip = self._create_midi_clip
            self.create_audio_clip = self._create_audio_clip

    @property
    def clip_slots(self):
        while len(self._slots) < len(self.song.scenes):
            self._slots.append(MockSlot())
        return self._slots

    _slots = None

    def _create_midi_clip(self, start, length):
        c = MockClip(length, start)
        self.arrangement_clips.append(c)
        return c

    def _create_audio_clip(self, path, position):
        c = MockClip(0, position, audio_file=path)
        self.arrangement_clips.append(c)
        return c


class MockScene(object):
    def __init__(self):
        self.name = ""


class MockSongView(object):
    selected_track = None


class MockSong(object):
    def __init__(self, arrangement_api):
        self.arrangement_api = arrangement_api
        self.tempo = 126.0
        self.tracks = []
        self.scenes = [MockScene() for _ in range(8)]
        self.view = MockSongView()
        for i in range(3):   # the user's own tracks
            t = MockTrack(self, "midi", arrangement_api)
            t._slots = []
            t.name = "USER %d" % i
            self.tracks.append(t)

    def _new(self, kind, index):
        t = MockTrack(self, kind, self.arrangement_api)
        t._slots = []
        self.tracks.insert(index, t)

    def create_midi_track(self, index):
        self._new("midi", index)

    def create_audio_track(self, index):
        self._new("audio", index)

    def create_scene(self, index):
        self.scenes.append(MockScene())


class Item(object):
    def __init__(self, name, children=(), loadable=True, folder=False):
        self.name, self.children, self.is_loadable, self.is_folder = name, list(children), loadable, folder


class MockBrowser(object):
    def __init__(self, song):
        self.song = song
        self.instruments = Item("Instruments", [Item("Analog"), Item("Drift"), Item("Operator"), Item("Wavetable")], False, True)
        self.drums = Item("Drums", [Item("Drum Rack"), Item("Kits", [Item("909 Core Kit.adg")], False, True)], False, True)

    def load_item(self, item):
        self.song.view.selected_track.devices.append(item.name)


class MockApp(object):
    def __init__(self, song):
        self.browser = MockBrowser(song)

    def get_major_version(self):
        return 12

    def get_minor_version(self):
        return 1

    def get_bugfix_version(self):
        return 0


class MockCInstance(object):
    def __init__(self, song):
        self._song = song
        self.messages = []

    def song(self):
        return self._song

    def log_message(self, m):
        pass

    def show_message(self, m):
        self.messages.append(m)


def run(request_path, arrangement_api):
    song = MockSong(arrangement_api)
    app = MockApp(song)
    live = types.ModuleType("Live")
    live.Clip = types.SimpleNamespace(MidiNoteSpecification=MidiNoteSpecification)
    live.Application = types.SimpleNamespace(get_application=lambda: app)
    sys.modules["Live"] = live

    home = tempfile.mkdtemp()
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
    c = MockCInstance(song)
    bridge = finish.create_instance(c)

    inbox = os.path.join(home, "Music", "FINISH", "inbox")
    assert os.path.exists(os.path.join(inbox, "bridge.json")), "bridge did not announce itself"
    shutil.copy(request_path, os.path.join(inbox, "request.json"))
    for _ in range(10):
        bridge.update_display()
    resp_path = os.path.join(inbox, "response.json")
    assert os.path.exists(resp_path), "no response written"
    resp = json.load(open(resp_path))
    req = json.load(open(request_path))

    assert resp["ok"], resp
    user = [t for t in song.tracks if t.name.startswith("USER")]
    assert len(user) == 3, "user tracks changed"
    new = [t for t in song.tracks if t.name.startswith("FINISH")]
    assert len(new) == len(req["tracks"]), "wrong number of new tracks"
    expected_notes = sum(len(c.get("notes", [])) for t in req["tracks"] for c in t["clips"])
    if arrangement_api:
        got = sum(len(c.notes) for t in new for c in t.arrangement_clips)
        starts = sorted(c.start_time for t in new for c in t.arrangement_clips)
        assert starts[0] >= req["startBar"] * 4.0, "clips placed before the requested bar"
    else:
        got = sum(len(s.clip.notes) for t in new for s in t._slots if s.clip is not None)
        names = [s.name for s in song.scenes[8:]]
        assert all(n.startswith("FINISH ") for n in names) and len(names) == len(req["sections"]), names
    assert got == expected_notes, "notes lost: %d of %d" % (got, expected_notes)
    loaded = [d for t in new for d in t.devices]
    assert len(loaded) == len([t for t in req["tracks"] if t["kind"] == "midi"]), "instruments not loaded: %s" % loaded
    print("  %-11s %s | instruments %s | warnings %d" % ("arrangement" if arrangement_api else "session", resp["message"], loaded, len(resp["warnings"])))
    shutil.rmtree(home, ignore_errors=True)


from test_live_bridge_v3 import run_v3  # noqa: E402

if __name__ == "__main__":
    path = sys.argv[1]
    # FX WAV paths in the request must exist for audio clips; they do when produced by the C++ test.
    run(path, True)
    run(path, False)
    plan = sys.argv[2] if len(sys.argv) > 2 else None
    run_v3(plan, live12=True)
    run_v3(plan, live12=False)
    print("Live bridge: OK")
