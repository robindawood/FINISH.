# FINISH bridge for Ableton Live (Remote Script) - version 3.
#
# Select "FINISH" once in Live > Settings > Link, Tempo & MIDI > Control Surface (Input/Output: None).
# This is NOT Ableton Link: Link only shares tempo between apps; this script is what lets FINISH read and
# write your Live set.
#
# Talks with the FINISH plugin through files in ~/Music/FINISH/inbox:
#   bridge.json    heartbeat (every ~2 s): alive, Live version, what this Live allows, current set summary
#   request.json   one request from the plugin:
#       {"type": "snapshot"}  -> writes project.json: tempo, signature, scale, locators, loop, selection,
#                                tracks (devices, drum pads), Arrangement and Session clips with their MIDI notes
#       {"type": "apply", "ops": [...]} -> writes clips onto the user's tracks in EMPTY places only (copies of
#                                the user's own clips, or variations of them), optional new FX track, locators.
#                                One Live undo step; every created object is remembered for "undo".
#       {"type": "undo", "target": "<apply id>"} -> removes what that apply created
#       (no type)             -> legacy v2 request: new FINISH tracks with generated parts
#   response.json  the answer, with a report of what was done / skipped
#
# Never modifies or deletes anything the user made.

import json
import os
import time

try:
    import Live  # noqa: F401  (only available inside Live)
except Exception:  # pragma: no cover - tests outside Live
    Live = None

BRIDGE_VERSION = "3.1"
POLL_EVERY = 3          # update_display ticks (~100 ms each)
MAX_PARAMS = 48
HEARTBEAT_EVERY = 20
MAX_NOTES_PER_CLIP = 6000
EPS = 1e-4

INSTRUMENTS = {
    "BASS": ["Drift", "Analog", "Operator", "Wavetable"],
    "CHORDS": ["Drift", "Wavetable", "Analog", "Operator"],
    "LEAD": ["Wavetable", "Drift", "Analog", "Operator"],
    "PAD": ["Wavetable", "Drift", "Analog", "Operator"],
}
DRUM_KIT_HINTS = ["909", "808", "Core", "Kit"]


def inbox_dir():
    return os.path.join(os.path.expanduser("~"), "Music", "FINISH", "inbox")


def _write_json(name, data):
    path = os.path.join(inbox_dir(), name)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f)
    os.replace(tmp, path)


def _safe(fn, default=None):
    try:
        return fn()
    except Exception:
        return default


class FinishBridge(object):
    def __init__(self, c_instance):
        self._c = c_instance
        self._tick = 0
        self._busy = False
        self._history = {}        # apply id -> created objects
        self._last_apply = ""
        self._log("FINISH bridge %s ready (inbox: %s)" % (BRIDGE_VERSION, inbox_dir()))
        try:
            os.makedirs(inbox_dir(), exist_ok=True)
        except Exception as e:
            self._log("cannot create inbox: %s" % e)
        self._heartbeat()

    # ------------------------------------------------------------------ Live control-surface API
    def disconnect(self):
        try:
            _write_json("bridge.json", {"alive": False, "time": time.time(), "version": BRIDGE_VERSION,
                                        "live": self._live_version()})
        except Exception:
            pass

    def connect_script_instances(self, instanciated_scripts):
        pass

    def can_lock_to_devices(self):
        return False

    def refresh_state(self):
        pass

    def build_midi_map(self, midi_map_handle):
        pass

    def receive_midi(self, midi_bytes):
        pass

    def suggest_input_port(self):
        return ""

    def suggest_output_port(self):
        return ""

    def suggest_map_mode(self, cc_no, channel):
        return -1

    def application(self):
        return Live.Application.get_application()

    def song(self):
        return self._c.song()

    def update_display(self):
        self._tick += 1
        if self._tick % HEARTBEAT_EVERY == 0:
            self._heartbeat()
        if self._tick % POLL_EVERY != 0 or self._busy:
            return
        path = os.path.join(inbox_dir(), "request.json")
        if not os.path.exists(path):
            return
        self._busy = True
        request = {}
        try:
            work = path + ".working"
            try:
                os.replace(path, work)
            except Exception:
                return
            with open(work, "r", encoding="utf-8") as f:
                request = json.load(f)
            try:
                os.remove(work)
            except Exception:
                pass
            kind = request.get("type", "build")
            if kind == "snapshot":
                response = self._snapshot(request)
            elif kind == "apply":
                response = self._apply(request)
            elif kind == "undo":
                response = self._undo(request)
            elif kind == "play":
                response = self._play(request)
            elif kind == "ping":
                response = {"ok": True, "message": "FINISH connecte a Live %s" % self._live_version()}
            else:
                response = self._build(request)
        except Exception as e:
            response = {"ok": False, "error": "FINISH bridge error: %s" % e}
        finally:
            self._busy = False
        response["id"] = request.get("id", "")
        response["type"] = request.get("type", "build")
        self._respond(response)

    # ------------------------------------------------------------------ helpers
    def _log(self, msg):
        try:
            self._c.log_message("FINISH: " + msg)
        except Exception:
            pass

    def _status(self, msg):
        try:
            if msg:
                self._c.show_message("FINISH: " + msg)
        except Exception:
            pass

    def _live_version(self):
        try:
            app = Live.Application.get_application()
            return "%d.%d.%d" % (app.get_major_version(), app.get_minor_version(), app.get_bugfix_version())
        except Exception:
            return "?"

    def _capabilities(self):
        song = self.song()
        t = song.tracks[0] if len(song.tracks) else None
        caps = {
            "arrangement_read": t is not None and hasattr(t, "arrangement_clips"),
            "create_midi_clip": t is not None and hasattr(t, "create_midi_clip"),
            "create_audio_clip": t is not None and hasattr(t, "create_audio_clip"),
            "duplicate_to_arrangement": t is not None and hasattr(t, "duplicate_clip_to_arrangement"),
            "delete_clip": t is not None and hasattr(t, "delete_clip"),
            "undo_step": hasattr(song, "begin_undo_step"),
            "notes_extended": Live is not None and hasattr(Live, "Clip") and hasattr(Live.Clip, "MidiNoteSpecification"),
        }
        return caps

    def _heartbeat(self):
        try:
            song = self.song()
            data = {"alive": True, "time": time.time(), "version": BRIDGE_VERSION, "live": self._live_version(),
                    "tempo": _safe(lambda: float(song.tempo), 0.0),
                    "tracks": _safe(lambda: len(song.tracks), 0),
                    "playing": _safe(lambda: bool(song.is_playing), False),
                    "caps": _safe(self._capabilities, {}),
                    "lastApply": self._last_apply}
            _write_json("bridge.json", data)
        except Exception as e:
            self._log("heartbeat failed: %s" % e)

    def _respond(self, response):
        try:
            response["time"] = time.time()
            _write_json("response.json", response)
        except Exception as e:
            self._log("cannot write response: %s" % e)
        self._status(response.get("message", response.get("error", "")))

    # ------------------------------------------------------------------ reading the set
    def _notes_of(self, clip):
        out = []
        try:
            if hasattr(clip, "get_notes_extended"):
                first = min(0.0, _safe(lambda: float(clip.loop_start), 0.0), _safe(lambda: float(clip.start_marker), 0.0))
                last = max(_safe(lambda: float(clip.loop_end), 0.0), _safe(lambda: float(clip.end_marker), 0.0),
                           _safe(lambda: float(clip.length), 0.0)) + 1.0
                try:
                    notes = clip.get_notes_extended(0, 128, first, last - first)
                except Exception:
                    notes = clip.get_notes_extended(0, 128, 0.0, last)
                for n in notes:
                    if getattr(n, "mute", False):
                        continue
                    out.append([int(n.pitch), round(float(n.start_time), 5), round(float(n.duration), 5),
                                int(round(float(n.velocity)))])
                    if len(out) >= MAX_NOTES_PER_CLIP:
                        break
            elif hasattr(clip, "get_notes"):
                for n in clip.get_notes(-16384.0, 0, 1.0e6, 128):
                    pitch, start, dur, vel, mute = n[0], n[1], n[2], n[3], n[4]
                    if mute:
                        continue
                    out.append([int(pitch), round(float(start), 5), round(float(dur), 5), int(round(float(vel)))])
                    if len(out) >= MAX_NOTES_PER_CLIP:
                        break
        except Exception as e:
            self._log("notes read failed: %s" % e)
        out.sort(key=lambda n: (n[1], n[0]))
        return out

    def _clip_info(self, clip, where, index):
        is_midi = bool(_safe(lambda: clip.is_midi_clip, False))
        info = {
            "where": where, "index": index,
            "name": _safe(lambda: clip.name, "") or "",
            "midi": is_midi,
            "start": _safe(lambda: float(clip.start_time), 0.0),
            "end": _safe(lambda: float(clip.end_time), 0.0),
            "length": _safe(lambda: float(clip.length), 0.0),
            "loopStart": _safe(lambda: float(clip.loop_start), 0.0),
            "loopEnd": _safe(lambda: float(clip.loop_end), 0.0),
            "looping": _safe(lambda: bool(clip.looping), False),
            "muted": _safe(lambda: bool(clip.muted), False),
            "color": _safe(lambda: int(clip.color), 0),
        }
        if where == "session":
            info["start"] = 0.0
            info["end"] = info["length"]
        if is_midi:
            info["notes"] = self._notes_of(clip)
        else:
            info["file"] = _safe(lambda: clip.file_path, "") or ""
            info["warping"] = _safe(lambda: bool(clip.warping), False)
            info["gain"] = _safe(lambda: float(clip.gain), 0.0)
            info["pitch"] = _safe(lambda: int(clip.pitch_coarse), 0)
        return info

    def _param_info(self, p):
        return [_safe(lambda: p.name, ""), _safe(lambda: round(float(p.value), 4), 0.0), _safe(lambda: str(p), ""),
                int(_safe(lambda: int(p.automation_state), 0) or 0)]

    def _device_info(self, d, depth=0):
        cls = _safe(lambda: d.class_name, "")
        info = {"name": _safe(lambda: d.name, ""), "class": cls,
                "display": _safe(lambda: d.class_display_name, "") or "",
                "type": _safe(lambda: int(d.type), 0),
                "on": bool(_safe(lambda: d.parameters[0].value, 1)),
                "plugin": cls in ("PluginDevice", "AuPluginDevice", "MxDeviceAudioEffect", "MxDeviceInstrument", "MxDeviceMidiEffect")}
        params = _safe(lambda: list(d.parameters), [])
        info["params"] = [self._param_info(p) for p in params[:MAX_PARAMS]]
        info["automated"] = [p[0] for p in (self._param_info(x) for x in params) if p[3] != 0]
        sample = _safe(lambda: d.sample, None)
        if sample is not None:
            info["sample"] = _safe(lambda: sample.file_path, "") or ""
        if _safe(lambda: d.can_have_drum_pads, False):
            pads = []
            for p in _safe(lambda: list(d.drum_pads), []):
                chains = _safe(lambda: list(p.chains), [])
                if chains:
                    pad = [_safe(lambda: int(p.note), 0), _safe(lambda: p.name, "")]
                    sm = ""
                    for ch in chains[:1]:
                        for dv in _safe(lambda: list(ch.devices), [])[:2]:
                            smp = _safe(lambda: dv.sample, None)
                            if smp is not None:
                                sm = _safe(lambda: smp.file_path, "") or ""
                    pad.append(sm)
                    pads.append(pad)
            info["pads"] = pads
        elif depth < 2 and _safe(lambda: d.can_have_chains, False):
            info["chains"] = [{"name": _safe(lambda: ch.name, ""),
                               "devices": [self._device_info(x, depth + 1) for x in _safe(lambda: list(ch.devices), [])[:8]]}
                              for ch in _safe(lambda: list(d.chains), [])[:8]]
        return info

    def _signature(self, track):
        """What makes the track's sound: device classes, names, on/off and parameter values."""
        def dev(d, depth=0):
            parts = [_safe(lambda: d.class_name, ""), _safe(lambda: d.name, "")]
            for p in _safe(lambda: list(d.parameters), [])[:MAX_PARAMS]:
                parts.append("%.4f" % _safe(lambda: float(p.value), 0.0))
            if depth < 2 and _safe(lambda: d.can_have_chains, False):
                for ch in _safe(lambda: list(d.chains), []):
                    for x in _safe(lambda: list(ch.devices), []):
                        parts.append("[" + dev(x, depth + 1) + "]")
            return "|".join(parts)
        return ";".join(dev(d) for d in _safe(lambda: list(track.devices), []))

    def _mixer_automation(self, track):
        out = []
        mixer = _safe(lambda: track.mixer_device, None)
        if mixer is None:
            return out
        for label, p in (("Volume", _safe(lambda: mixer.volume, None)), ("Pan", _safe(lambda: mixer.panning, None))):
            if p is not None and int(_safe(lambda: int(p.automation_state), 0) or 0) != 0:
                out.append(label)
        for i, p in enumerate(_safe(lambda: list(mixer.sends), [])):
            if int(_safe(lambda: int(p.automation_state), 0) or 0) != 0:
                out.append("Send %s" % chr(65 + i))
        return out

    def _track_info(self, track, index, selected):
        is_midi = bool(_safe(lambda: track.has_midi_input, False))
        info = {
            "index": index, "name": _safe(lambda: track.name, ""),
            "midi": is_midi, "color": _safe(lambda: int(track.color), 0),
            "mute": _safe(lambda: bool(track.mute), False),
            "group": bool(_safe(lambda: track.is_foldable, False)),
            "grouped": bool(_safe(lambda: track.is_grouped, False)),
            "frozen": bool(_safe(lambda: track.is_frozen, False)),
            "selected": track == selected,
            "devices": [self._device_info(d) for d in _safe(lambda: list(track.devices), [])],
            "mixerAutomation": self._mixer_automation(track),
            "arrangement": [], "session": [],
        }
        if hasattr(track, "arrangement_clips"):
            for i, clip in enumerate(_safe(lambda: list(track.arrangement_clips), [])):
                info["arrangement"].append(self._clip_info(clip, "arrangement", i))
        for si, slot in enumerate(_safe(lambda: list(track.clip_slots), [])):
            if _safe(lambda: slot.has_clip, False):
                ci = self._clip_info(slot.clip, "session", si)
                info["session"].append(ci)
        return info

    def _snapshot(self, req):
        song = self.song()
        t0 = time.time()
        selected = _safe(lambda: song.view.selected_track, None)
        detail = _safe(lambda: song.view.detail_clip, None)
        tracks = []
        detail_ref = None
        for i, track in enumerate(song.tracks):
            info = self._track_info(track, i, selected)
            tracks.append(info)
            if detail is not None and detail_ref is None:
                for c in _safe(lambda: list(track.arrangement_clips), []) if hasattr(track, "arrangement_clips") else []:
                    if c == detail:
                        detail_ref = {"track": i, "start": _safe(lambda: float(c.start_time), 0.0)}
                for slot_i, slot in enumerate(_safe(lambda: list(track.clip_slots), [])):
                    if _safe(lambda: slot.has_clip, False) and slot.clip == detail:
                        detail_ref = {"track": i, "slot": slot_i}
        project = {
            "bridge": BRIDGE_VERSION, "live": self._live_version(), "caps": self._capabilities(),
            "time": time.time(),
            "tempo": float(song.tempo),
            "signature": [_safe(lambda: int(song.signature_numerator), 4), _safe(lambda: int(song.signature_denominator), 4)],
            "rootNote": _safe(lambda: int(song.root_note), -1),
            "scale": _safe(lambda: song.scale_name, "") or "",
            "songLength": _safe(lambda: float(song.song_length), 0.0),
            "loop": [_safe(lambda: float(song.loop_start), 0.0), _safe(lambda: float(song.loop_length), 0.0),
                     _safe(lambda: bool(song.loop), False)],
            "position": _safe(lambda: float(song.current_song_time), 0.0),
            "cues": [[_safe(lambda: c.name, ""), _safe(lambda: float(c.time), 0.0)] for c in _safe(lambda: list(song.cue_points), [])],
            "scenes": [_safe(lambda: s.name, "") for s in _safe(lambda: list(song.scenes), [])],
            "selectedTrack": next((t["index"] for t in tracks if t["selected"]), -1),
            "detailClip": detail_ref,
            "tracks": tracks,
        }
        _write_json("project.json", project)
        n_clips = sum(len(t["arrangement"]) + len(t["session"]) for t in tracks)
        n_notes = sum(len(c.get("notes", [])) for t in tracks for c in t["arrangement"] + t["session"])
        msg = "projet lu : %d pistes, %d clips, %d notes (%.0f ms)" % (len(tracks), n_clips, n_notes, (time.time() - t0) * 1000)
        self._log(msg)
        return {"ok": True, "message": msg, "file": os.path.join(inbox_dir(), "project.json"),
                "tracks": len(tracks), "clips": n_clips, "notes": n_notes}

    # ------------------------------------------------------------------ writing
    def _find_track(self, op, created_tracks):
        song = self.song()
        ref = op.get("track")
        if isinstance(ref, str) and ref.startswith("@"):
            return created_tracks.get(ref)
        name = op.get("trackName", None)
        tracks = list(song.tracks)
        try:
            idx = int(ref)
        except Exception:
            idx = -1
        if 0 <= idx < len(tracks) and (name is None or tracks[idx].name == name):
            return tracks[idx]
        if name is not None:
            for t in tracks:
                if t.name == name:
                    return t
        return None

    def _source_clip(self, track_obj, src):
        if not src:
            return None
        song = self.song()
        tracks = list(song.tracks)
        t = track_obj
        if "track" in src:
            ti = int(src["track"])
            if 0 <= ti < len(tracks):
                t = tracks[ti]
        try:
            if src.get("where") == "session":
                slot = t.clip_slots[int(src["index"])]
                return slot.clip if slot.has_clip else None
            if "start" in src:
                for c in t.arrangement_clips:
                    if abs(float(c.start_time) - float(src["start"])) < EPS:
                        return c
            clips = list(t.arrangement_clips)
            i = int(src.get("index", -1))
            return clips[i] if 0 <= i < len(clips) else None
        except Exception:
            return None

    def _occupied(self, track, start, end):
        try:
            for c in track.arrangement_clips:
                cs, ce = float(c.start_time), float(c.end_time)
                if cs < end - EPS and ce > start + EPS:
                    return c
        except Exception:
            pass
        return None

    def _add_notes(self, clip, notes):
        if not notes:
            return
        if hasattr(clip, "add_new_notes") and Live is not None and hasattr(Live.Clip, "MidiNoteSpecification"):
            specs = []
            for n in notes:
                p, s, d, v = n[0], n[1], n[2], n[3]
                specs.append(Live.Clip.MidiNoteSpecification(pitch=int(p), start_time=float(s), duration=float(d),
                                                             velocity=float(v), mute=False))
            clip.add_new_notes(tuple(specs))
        else:
            clip.set_notes(tuple((int(n[0]), float(n[1]), float(n[2]), float(n[3]), False) for n in notes))

    def _clear_notes(self, clip):
        if hasattr(clip, "remove_notes_extended"):
            clip.remove_notes_extended(0, 128, -16384.0, 1.0e6)
        elif hasattr(clip, "remove_notes"):
            clip.remove_notes(-16384.0, 0, 1.0e6, 128)

    def _midi_clip_at(self, track, start, length, notes, name, record):
        """Creates an arrangement MIDI clip with `notes` (beats relative to start)."""
        song = self.song()
        if hasattr(track, "create_midi_clip"):
            clip = track.create_midi_clip(start, length)
            clip.name = name
            self._add_notes(clip, notes)
            record.append(("clip", track, start, name))
            return "created"
        # Live 11: build it in a temporary Session slot, copy it to the Arrangement, remove the temporary clip.
        if hasattr(track, "duplicate_clip_to_arrangement"):
            song.create_scene(-1)
            scene_i = len(song.scenes) - 1
            slot = track.clip_slots[scene_i]
            slot.create_clip(length)
            tmp = slot.clip
            tmp.name = name
            self._add_notes(tmp, notes)
            track.duplicate_clip_to_arrangement(tmp, start)
            slot.delete_clip()
            song.delete_scene(scene_i)
            record.append(("clip", track, start, name))
            return "created"
        raise RuntimeError("cette version de Live ne permet pas de creer des clips dans l'Arrangement")

    def _place(self, op, track, created_tracks, record, report):
        start = float(op.get("start", 0.0))
        length = max(0.25, float(op.get("length", 4.0)))
        name = op.get("name", "FINISH")
        busy = self._occupied(track, start, start + length)
        if busy is not None:
            report["skipped"].append("%s : mesures %d-%d deja occupees par '%s' (rien ecrit)" % (
                track.name, int(start // 4) + 1, int((start + length) // 4), busy.name))
            return
        notes = op.get("notes", None)
        src = self._source_clip(track, op.get("source"))
        file_path = op.get("file", "")

        if file_path:
            if not os.path.exists(file_path):
                report["skipped"].append("%s : fichier introuvable %s" % (track.name, file_path))
                return
            if not hasattr(track, "create_audio_clip"):
                report["manual"].append("Glisse %s sur la piste %s a la mesure %d (Live 12 requis pour le faire automatiquement)"
                                        % (os.path.basename(file_path), track.name, int(start // 4) + 1))
                return
            clip = track.create_audio_clip(file_path, start)
            clip.name = name
            record.append(("clip", track, start, name))
            report["written"] += 1
            return

        # Exact copy of the user's clip (keeps sound, envelopes, warp...): tile it over the length.
        if notes is None and src is not None and hasattr(track, "duplicate_clip_to_arrangement"):
            if op.get("unit"):
                src_len = max(0.25, float(op["unit"]))
            else:
                src_len = max(0.25, float(_safe(lambda: src.end_time - src.start_time, 0.0) or _safe(lambda: src.length, 4.0)))
            t = start
            while t < start + length - EPS:
                track.duplicate_clip_to_arrangement(src, t)
                record.append(("clip", track, t, _safe(lambda: src.name, "")))
                t += src_len
            report["written"] += 1
            report["copied"] += 1
            return
        # Variation: a copy of the user's clip (keeps its envelopes, groove, transposition, settings) with new notes.
        if notes is not None and src is not None and _safe(lambda: src.is_midi_clip, False) \
                and hasattr(track, "duplicate_clip_to_arrangement") and op.get("unit"):
            unit = max(0.25, float(op["unit"]))
            t = start
            while t < start + length - EPS:
                if t + unit > start + length + EPS:
                    break
                c = track.duplicate_clip_to_arrangement(src, t)
                if c is None:
                    c = next((x for x in track.arrangement_clips if abs(float(x.start_time) - t) < EPS), None)
                record.append(("clip", track, t, _safe(lambda: src.name, "")))
                if c is not None:
                    ls = _safe(lambda: float(c.loop_start), 0.0)
                    if _safe(lambda: bool(c.looping), False) and _safe(lambda: float(c.loop_end) - ls, unit) < unit - EPS:
                        _safe(lambda: setattr(c, "loop_end", ls + unit))
                    self._clear_notes(c)
                    tile = [[n[0], n[1] - (t - start) + ls, n[2], n[3]] for n in notes if t - start - EPS <= n[1] < t - start + unit - EPS]
                    self._add_notes(c, tile)
                    _safe(lambda: setattr(c, "name", name))
                t += unit
            if t < start + length - EPS:   # what is left does not fit a whole copy of the clip: a fresh clip
                rest = [[n[0], n[1] - (t - start), n[2], n[3]] for n in notes if n[1] >= t - start - EPS]
                if rest:
                    self._midi_clip_at(track, t, start + length - t, rest, name, record)
                    report["fresh"].append("%s : mesures %d-%d dans un clip neuf (sans les enveloppes de ton clip)"
                                           % (track.name, int(t // 4) + 1, int((start + length) // 4)))
            report["written"] += 1
            report["varied"] += 1
            return
        if notes is None and src is not None and _safe(lambda: src.is_midi_clip, False):
            notes = op.get("fallbackNotes", None)
        if notes is None:
            if src is not None and not _safe(lambda: src.is_midi_clip, True):
                report["manual"].append("%s : copie le clip audio '%s' a la mesure %d (cette version de Live ne permet pas la copie automatique)"
                                        % (track.name, src.name, int(start // 4) + 1))
            else:
                report["skipped"].append("%s : clip source introuvable" % track.name)
            return
        if not _safe(lambda: track.has_midi_input, True):
            report["skipped"].append("%s : piste audio, impossible d'y ecrire des notes" % track.name)
            return
        self._midi_clip_at(track, start, length, notes, name, record)
        if not op.get("layer"):
                report["fresh"].append("%s : mesures %d-%d dans un clip neuf (sans les enveloppes de ton clip)"
                                       % (track.name, int(start // 4) + 1, int((start + length) // 4)))
        report["written"] += 1
        report["varied"] += 1

    def _alt_track(self, op, created_tracks, record, report):
        """An empty copy of one of the user's tracks (same instrument and effects) for layers over existing bars."""
        song = self.song()
        src = self._find_track(op, created_tracks)
        if src is None:
            report["errors"].append("piste introuvable : %s" % op.get("trackName"))
            return
        if not hasattr(song, "duplicate_track"):
            report["manual"].append("Transitions sur tes sections existantes : cette version de Live ne permet pas de copier une piste.")
            return
        tracks = list(song.tracks)
        index = tracks.index(src)
        song.duplicate_track(index)
        t = song.tracks[index + 1]
        # Empty it: the copy keeps the instrument and effects, not the clips.
        clips = list(_safe(lambda: t.arrangement_clips, []) or [])
        if clips and not hasattr(t, "delete_clip"):
            song.delete_track(index + 1)
            report["manual"].append("Transitions sur tes sections existantes : Live 12 requis (copie de piste a vider).")
            return
        for c in clips:
            t.delete_clip(c)
        for slot in _safe(lambda: list(t.clip_slots), []):
            if _safe(lambda: slot.has_clip, False):
                slot.delete_clip()
        t.name = op.get("name", src.name + " FINISH")
        _safe(lambda: setattr(t, "mute", False))
        created_tracks[op.get("ref", "@alt")] = t
        record.append(("track", t, 0.0, t.name))
        report["tracks"].append(t.name)

    def _apply(self, req):
        song = self.song()
        ops = req.get("ops", [])
        if not ops:
            return {"ok": False, "error": "Rien a envoyer."}
        record = []
        created_tracks = {}
        report = {"written": 0, "copied": 0, "varied": 0, "skipped": [], "manual": [], "errors": [], "tracks": [], "cues": 0,
                  "fresh": [], "fidelity": {}}
        before = dict((id(t), (t.name, self._signature(t))) for t in song.tracks)
        undo_step = hasattr(song, "begin_undo_step")
        if undo_step:
            _safe(song.begin_undo_step)
        try:
            for op in ops:
                kind = op.get("op", "clip")
                try:
                    if kind == "newTrack":
                        if op.get("kind") == "audio" and len(song.tracks) and not hasattr(song.tracks[0], "create_audio_clip"):
                            report["manual"].append("Effets de transition : glisse les WAV du dossier ~/Music/FINISH/fx sur une piste audio "
                                                    "(Live 12 requis pour le faire automatiquement)")
                            continue
                        index = len(song.tracks)
                        if op.get("kind") == "audio":
                            song.create_audio_track(index)
                        else:
                            song.create_midi_track(index)
                        t = song.tracks[index]
                        t.name = op.get("name", "FINISH")
                        if "color" in op:
                            _safe(lambda: setattr(t, "color", int(op["color"])))
                        created_tracks[op.get("ref", "@new")] = t
                        record.append(("track", t, 0.0, t.name))
                        report["tracks"].append(t.name)
                    elif kind == "altTrack":
                        self._alt_track(op, created_tracks, record, report)
                    elif kind == "cue":
                        tm = float(op.get("time", 0.0))
                        if _safe(lambda: bool(song.is_playing), False):
                            report["manual"].append("Locator '%s' non pose (Live jouait)" % op.get("name", ""))
                            continue
                        exists = any(abs(float(c.time) - tm) < EPS for c in _safe(lambda: list(song.cue_points), []))
                        if not exists and hasattr(song, "set_or_delete_cue"):
                            old = _safe(lambda: float(song.current_song_time), 0.0)
                            song.current_song_time = tm
                            song.set_or_delete_cue()
                            for c in song.cue_points:
                                if abs(float(c.time) - tm) < EPS:
                                    _safe(lambda: setattr(c, "name", op.get("name", "FINISH")))
                            song.current_song_time = old
                            record.append(("cue", None, tm, op.get("name", "")))
                            report["cues"] += 1
                    else:
                        track = self._find_track(op, created_tracks)
                        if track is None and str(op.get("track", "")).startswith("@"):
                            continue   # its track could not be created (already reported)
                        if track is None:
                            report["errors"].append("piste introuvable : %s" % op.get("trackName", op.get("track")))
                            continue
                        self._place(op, track, created_tracks, record, report)
                except Exception as e:
                    report["errors"].append("%s : %s" % (op.get("trackName", kind), e))
        finally:
            if undo_step:
                _safe(song.end_undo_step)
        # Fidelity: none of the user's instruments or effects may have changed.
        changed = []
        touched = set(id(r[1]) for r in record if r[0] == "clip" and r[1] is not None)
        for t in song.tracks:
            if id(t) in before and before[id(t)][1] != self._signature(t):
                changed.append(t.name)
        automation = []
        for t in song.tracks:
            if id(t) not in touched or id(t) not in before:
                continue
            names = []
            for d in _safe(lambda: list(t.devices), []):
                for p in _safe(lambda: list(d.parameters), []):
                    if int(_safe(lambda: int(p.automation_state), 0) or 0) != 0:
                        names.append("%s / %s" % (_safe(lambda: d.name, ""), _safe(lambda: p.name, "")))
            names += self._mixer_automation(t)
            if names:
                automation.append("%s : %s" % (t.name, ", ".join(names[:6])))
        report["fidelity"] = {"devicesUnchanged": not changed, "changed": changed, "automation": automation,
                              "newTracks": report["tracks"], "freshClips": len(report["fresh"])}
        for a in automation:
            report["manual"].append("Automation d'Arrangement non prolongee (l'API de Live ne permet pas de la copier) - " + a
                                    + " : copie-la sur les nouvelles mesures (selectionne la lane, Cmd/Ctrl+C puis Cmd/Ctrl+V).")
        aid = req.get("id", "")
        self._history[aid] = record
        self._last_apply = aid
        msg = "%d clips ecrits (%d copies de tes clips, %d variations)" % (report["written"], report["copied"], report["varied"])
        if report["skipped"]:
            msg += ", %d ignores (place occupee)" % len(report["skipped"])
        if report["tracks"]:
            msg += ", %d piste(s) ajoutee(s)" % len(report["tracks"])
        self._log(msg)
        ok = report["written"] > 0 or report["cues"] > 0
        return {"ok": ok, "message": msg if ok else "Rien n'a pu etre ecrit", "report": report,
                "undoable": True, "canDelete": hasattr(song.tracks[0], "delete_clip") if len(song.tracks) else False}

    def _play(self, req):
        song = self.song()
        if req.get("stop"):
            _safe(song.stop_playing)
            return {"ok": True, "message": "lecture arretee"}
        beat = max(0.0, float(req.get("beat", 0.0)))
        _safe(song.stop_playing)
        song.current_song_time = beat
        _safe(song.start_playing)
        return {"ok": True, "message": "lecture depuis la mesure %d" % (int(beat // 4) + 1)}

    def _undo(self, req):
        song = self.song()
        target = req.get("target", self._last_apply)
        record = self._history.get(target)
        if not record:
            return {"ok": False, "error": "Rien a annuler depuis FINISH (utilise Cmd/Ctrl+Z dans Live)."}
        removed, failed = 0, 0
        for kind, track, start, name in reversed(record):
            try:
                if kind == "clip":
                    if not hasattr(track, "delete_clip"):
                        failed += 1
                        continue
                    for c in list(track.arrangement_clips):
                        if abs(float(c.start_time) - start) < EPS:
                            track.delete_clip(c)
                            removed += 1
                            break
                elif kind == "track":
                    tracks = list(song.tracks)
                    if track in tracks:
                        song.delete_track(tracks.index(track))
                        removed += 1
                elif kind == "cue":
                    for c in song.cue_points:
                        if abs(float(c.time) - start) < EPS:
                            old = _safe(lambda: float(song.current_song_time), 0.0)
                            song.current_song_time = start
                            song.set_or_delete_cue()
                            song.current_song_time = old
                            removed += 1
                            break
            except Exception:
                failed += 1
        del self._history[target]
        if self._last_apply == target:
            self._last_apply = ""
        if failed:
            return {"ok": removed > 0, "message": "%d elements retires, %d a retirer avec Cmd/Ctrl+Z" % (removed, failed)}
        return {"ok": True, "message": "Envoi annule : %d elements retires" % removed}

    # ------------------------------------------------------------------ legacy v2: new tracks with generated parts
    def _find_browser_item(self, root, names, depth=2, budget=None):
        if budget is None:
            budget = [600]
        try:
            children = list(root.children)
        except Exception:
            return None
        for wanted in names:
            for item in children:
                budget[0] -= 1
                if budget[0] <= 0:
                    return None
                if item.name.split(".")[0].lower() == wanted.lower() and getattr(item, "is_loadable", False):
                    return item
        if depth > 0:
            for item in children:
                if getattr(item, "is_folder", False) or not getattr(item, "is_loadable", False):
                    found = self._find_browser_item(item, names, depth - 1, budget)
                    if found is not None:
                        return found
        return None

    def _find_kit(self, root, depth=3, budget=None):
        if budget is None:
            budget = [800]
        try:
            children = list(root.children)
        except Exception:
            return None
        for hint in DRUM_KIT_HINTS:
            for item in children:
                budget[0] -= 1
                if budget[0] <= 0:
                    return None
                if getattr(item, "is_loadable", False) and hint.lower() in item.name.lower() and item.name.lower() != "drum rack":
                    return item
        if depth > 0:
            for item in children:
                if getattr(item, "is_folder", False):
                    found = self._find_kit(item, depth - 1, budget)
                    if found is not None:
                        return found
        return None

    def _load_instrument(self, track, lane):
        if lane != "DRUMS" and hasattr(track, "insert_device"):
            for name in INSTRUMENTS.get(lane, ["Drift", "Operator"]):
                try:
                    track.insert_device(name)
                    return name
                except Exception:
                    continue
        try:
            app = Live.Application.get_application()
            browser = app.browser
            song = self.song()
            song.view.selected_track = track
            if lane == "DRUMS":
                item = self._find_kit(browser.drums)
            else:
                item = self._find_browser_item(browser.instruments, INSTRUMENTS.get(lane, ["Drift", "Operator"]))
            if item is None:
                return None
            browser.load_item(item)
            return item.name
        except Exception as e:
            self._log("instrument load failed: %s" % e)
            return None

    def _build(self, req):
        song = self.song()
        warnings = []
        start_beat = float(req.get("startBar", 0)) * 4.0
        bpm = float(req.get("bpm", 0) or 0)
        if bpm > 0 and abs(song.tempo - bpm) > 0.01:
            warnings.append("Tempo du projet %.2f BPM, FINISH a analyse %.2f BPM (tempo non modifie)." % (song.tempo, bpm))

        tracks_spec = req.get("tracks", [])
        if not tracks_spec:
            return {"ok": False, "error": "Rien a envoyer."}

        probe_mode = req.get("mode", "auto")
        created = []
        mode = None
        scene_index = {}
        clips_made = 0
        instruments = {}

        for spec in tracks_spec:
            kind = spec.get("kind", "midi")
            index = len(song.tracks)
            if kind == "audio":
                song.create_audio_track(index)
            else:
                song.create_midi_track(index)
            track = song.tracks[index]
            track.name = spec.get("name", "FINISH")
            try:
                if "color" in spec:
                    track.color = int(spec["color"])
            except Exception:
                pass
            created.append(track.name)

            if mode is None:
                can_arrange = hasattr(track, "create_midi_clip") if kind != "audio" else hasattr(track, "create_audio_clip")
                mode = "arrangement" if (probe_mode != "session" and can_arrange) else "session"
                if mode == "session":
                    for sec in req.get("sections", []):
                        song.create_scene(-1)
                        si = len(song.scenes) - 1
                        try:
                            song.scenes[si].name = "FINISH " + sec.get("name", "")
                        except Exception:
                            pass
                        scene_index[sec.get("name", "")] = si

            for clip_spec in spec.get("clips", []):
                start = float(clip_spec.get("start", 0.0))
                length = max(0.25, float(clip_spec.get("length", 4.0)))
                name = clip_spec.get("name", "")
                try:
                    if kind == "audio":
                        path = clip_spec.get("file", "")
                        if not os.path.exists(path):
                            continue
                        if mode == "arrangement" and hasattr(track, "create_audio_clip"):
                            clip = track.create_audio_clip(path, start_beat + start)
                        else:
                            slot = track.clip_slots[scene_index.get(clip_spec.get("section", name), 0)]
                            if not hasattr(slot, "create_audio_clip"):
                                warnings.append("Clips audio : glisse les WAV depuis le dossier FINISH (version de Live trop ancienne).")
                                break
                            clip = slot.create_audio_clip(path)
                        if clip is not None:
                            clip.name = name
                            clips_made += 1
                    else:
                        if mode == "arrangement":
                            clip = track.create_midi_clip(start_beat + start, length)
                        else:
                            slot = track.clip_slots[scene_index.get(clip_spec.get("section", name), 0)]
                            if slot.has_clip:
                                continue
                            slot.create_clip(length)
                            clip = slot.clip
                        clip.name = name
                        self._add_notes(clip, clip_spec.get("notes", []))
                        clips_made += 1
                except Exception as e:
                    warnings.append("%s / %s : %s" % (track.name, name, e))

            if kind != "audio":
                # FINISH never loads a substitute instrument: the track stays empty until you choose its sound.
                instruments[track.name] = ""
                warnings.append("%s : aucun instrument charge (FINISH ne remplace jamais tes sons) - mets-y le tien." % track.name)

        where = ("dans l'Arrangement a partir de la mesure %d" % (int(req.get("startBar", 0)) + 1)) if mode == "arrangement" \
            else "dans de nouvelles scenes de la vue Session"
        message = "%d pistes, %d clips crees %s." % (len(created), clips_made, where)
        self._log(message)
        return {"ok": True, "mode": mode, "tracks": created, "clips": clips_made,
                "instruments": instruments, "warnings": warnings, "message": message, "live": self._live_version()}
