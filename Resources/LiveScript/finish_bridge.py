# FINISH bridge for Ableton Live (Remote Script).
#
# The FINISH plugin writes an arrangement request (JSON) into  ~/Music/FINISH/inbox/request.json.
# This script picks it up and builds it in the open Live set:
#   - NEW tracks only ("FINISH DRUMS", "FINISH BASS", ...): nothing existing is modified or deleted.
#   - MIDI clips per section (INTRO, BUILD, BREAK, DROP 2, OUTRO...), placed in the Arrangement when this
#     version of Live allows it (Live 12), otherwise in new Session scenes named after the sections.
#   - FX audio clips (risers, impacts...) when Live can create audio clips from files.
#   - Tries to load a Live instrument on each new track; reports what it could not load.
# The result is written to ~/Music/FINISH/inbox/response.json and shown in Live's status bar.

import json
import os
import time

try:
    import Live  # noqa: F401  (only available inside Live)
except Exception:  # pragma: no cover - tests outside Live
    Live = None

POLL_EVERY = 4  # update_display ticks (~100 ms each)
INSTRUMENTS = {
    "BASS": ["Drift", "Analog", "Operator", "Wavetable"],
    "CHORDS": ["Drift", "Wavetable", "Analog", "Operator"],
    "LEAD": ["Wavetable", "Drift", "Analog", "Operator"],
    "PAD": ["Wavetable", "Drift", "Analog", "Operator"],
}
DRUM_KIT_HINTS = ["909", "808", "Core", "Kit"]


def inbox_dir():
    return os.path.join(os.path.expanduser("~"), "Music", "FINISH", "inbox")


class FinishBridge(object):
    def __init__(self, c_instance):
        self._c = c_instance
        self._tick = 0
        self._busy = False
        self._log("FINISH bridge ready (inbox: %s)" % inbox_dir())
        try:
            os.makedirs(inbox_dir(), exist_ok=True)
            # Let the plugin know the bridge is alive.
            with open(os.path.join(inbox_dir(), "bridge.json"), "w") as f:
                json.dump({"alive": True, "time": time.time(), "live": self._live_version()}, f)
        except Exception as e:
            self._log("cannot create inbox: %s" % e)

    # ------------------------------------------------------------------ Live control-surface API
    def disconnect(self):
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
        if self._tick % POLL_EVERY != 0 or self._busy:
            return
        path = os.path.join(inbox_dir(), "request.json")
        if not os.path.exists(path):
            return
        self._busy = True
        try:
            work = path + ".working"
            try:
                os.replace(path, work)
            except Exception:
                return
            with open(work, "r", encoding="utf-8") as f:
                request = json.load(f)
            response = self._build(request)
            try:
                os.remove(work)
            except Exception:
                pass
        except Exception as e:
            response = {"ok": False, "error": "FINISH bridge error: %s" % e}
        finally:
            self._busy = False
        self._respond(response)

    # ------------------------------------------------------------------ helpers
    def _log(self, msg):
        try:
            self._c.log_message("FINISH: " + msg)
        except Exception:
            pass

    def _status(self, msg):
        try:
            self._c.show_message("FINISH: " + msg)
        except Exception:
            pass

    def _live_version(self):
        try:
            app = Live.Application.get_application()
            return "%d.%d.%d" % (app.get_major_version(), app.get_minor_version(), app.get_bugfix_version())
        except Exception:
            return "?"

    def _respond(self, response):
        try:
            response["time"] = time.time()
            with open(os.path.join(inbox_dir(), "response.json.tmp"), "w", encoding="utf-8") as f:
                json.dump(response, f)
            os.replace(os.path.join(inbox_dir(), "response.json.tmp"), os.path.join(inbox_dir(), "response.json"))
        except Exception as e:
            self._log("cannot write response: %s" % e)
        self._status(response.get("message", response.get("error", "")))

    def _add_notes(self, clip, notes):
        if not notes:
            return
        if hasattr(clip, "add_new_notes") and Live is not None and hasattr(Live.Clip, "MidiNoteSpecification"):
            specs = []
            for p, s, d, v in notes:
                specs.append(Live.Clip.MidiNoteSpecification(pitch=int(p), start_time=float(s), duration=float(d),
                                                             velocity=float(v), mute=False))
            clip.add_new_notes(tuple(specs))
        else:
            clip.set_notes(tuple((int(p), float(s), float(d), float(v), False) for p, s, d, v in notes))

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
        # Live 12.3+: Track.insert_device loads a native instrument directly (no browser search).
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

    # ------------------------------------------------------------------ build
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

        # Arrangement placement needs Track.create_midi_clip (Live 12); otherwise new Session scenes.
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
                    # One new scene per section, named after it, appended after the user's scenes.
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

            if kind != "audio" and req.get("loadInstruments", True):
                loaded = self._load_instrument(track, spec.get("lane", ""))
                instruments[track.name] = loaded or ""
                if not loaded:
                    warnings.append("%s : aucun instrument charge, mets %s." % (track.name, spec.get("hint", "un synthe")))

        where = ("dans l'Arrangement a partir de la mesure %d" % (int(req.get("startBar", 0)) + 1)) if mode == "arrangement" \
            else "dans de nouvelles scenes de la vue Session"
        message = "%d pistes, %d clips crees %s." % (len(created), clips_made, where)
        self._log(message)
        return {"ok": True, "id": req.get("id", ""), "mode": mode, "tracks": created, "clips": clips_made,
                "instruments": instruments, "warnings": warnings, "message": message, "live": self._live_version()}
