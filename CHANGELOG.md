# Changelog

## 3.1.0 - sound fidelity first
- Every new part plays through the project's own instruments, presets, plug-ins and effects; FINISH never loads or
  substitutes an instrument (the legacy path that loaded Live instruments on new tracks is gone).
- Variations are built on a copy of the user's clip (duplicate, then replace the notes): clip envelopes, groove,
  transposition and settings are kept. Parts that do not fit a whole copy are reported ("clip neuf").
- Sound inventory read from the set: instruments / presets / racks, third-party plug-ins, effect chains, Simpler and
  Drum Rack samples, parameter values, automated parameters (SONS DU PROJET).
- Fidelity check after each send: device and parameter signatures of every track compared before / after; arrangement
  automation that the Live API cannot copy is reported with what to do.
- ÉCOUTER DANS LIVE: writes the plan and plays Live from 2 bars before the new parts (real sounds), keep or undo.
  The in-plugin preview is labelled APERÇU (FINISH's simplified sounds).
- FINISH's own sounds (FX track: risers, impacts) are off by default (SONS FINISH toggle, explicitly labelled).
- ESQUISSE -> Live: parts routed onto the user's tracks by role, drums remapped to their own pads, melodic parts
  transposed into the project's key; a part with no matching track or pad is reported, never replaced.

## 3.0.0
- PROJECT mode: FINISH MY TRACK continues the user's own Ableton Live project instead of generating a new track.
  - Remote Script v3 reads the set (tempo, signature, scale, locators, tracks, devices and drum pads, Arrangement and
    Session clips with every MIDI note) and writes only into empty places on the user's own tracks: exact copies of
    their clips (duplicate_clip_to_arrangement) or variations of their own notes; one Live undo step; undo from FINISH.
  - Analysis of the real content: track roles (name, devices, note statistics), key from the notes, drum map from pad
    names, existing structure from the per-bar activity, the strongest section's material per track, what is missing.
  - Plan before writing: four intervention levels (complete / enrich / improve / finalise), three versions, per-track
    locks, per-cell on/off, transition fills layered on a copy of the user's drum track, FX track, locators.
  - Free commands with constraints ("sans toucher à ma mélodie"), ranges ("16 prochaines mesures"), iterations
    ("plus subtil", "garde la version précédente", "uniquement la basse"), questions, send / undo with confirmation.
  - Connection diagnosis (installed / selected / alive / version), Link vs Control Surface explained, journal.
- Mastering from the mix's problems: 7-band balance vs genre landmarks or a reference, mud and presence bells,
  mono-safe width and mono bass, punch-preserving leveller, profiles, intensity, "same level" target (never louder by
  default), before/after measures and an explanation of every move.
- Fixed: the 3-band leveller blended its band sum with the dry signal, which comb-filtered around 4 kHz and 120 Hz
  (up to -3 dB in the 2-5 kHz presence region in 2.0). It is now level-based and phase-coherent.
- Tests: protocol end to end (plugin <-> real Remote Script on a mock Live: read, plan, write 38 clips, read back,
  undo, set identical), mastering before/after through the plugin, 11,800+ checks.

## 2.0.0
- FINISH COMPOSE: FINISH MY TRACK now writes the missing parts of the song from what you played (a drop, a loop or a lone melody):
  - musical analysis of the played audio: tempo grid (host), key with confidence and relative major/minor resolution,
    groove templates (kick / snare-clap / hats, swing), bass line, chord loop, melody motif, energy, style guess;
    every uncertain estimate is flagged and correctable (key, style, tempo x2 / /2);
  - arrangement generator: INTRO, BUILD, DROP (yours, untouched), BREAK, BRIDGE, BUILD 2, DROP 2, OUTRO + transition FX
    (risers, sweeps, reverse cymbals, impacts, downlifters), MIDI for drums / bass / chords / lead / pad with filter (CC74) automation;
  - three proposals (FIDELE / ATMO / INTENSE), creativity control, per-part regenerate, lock, mood moves, undo;
  - built-in instruments for preview inside the plugin (solo or over the DAW);
  - free commands (FR/EN): "finis mon morceau", "crée un bridge mélodique sombre", "ajoute un build-up avant le drop",
    "rends le drop plus percutant" (punch + accent layer on your drop), "masterise pour le club"...;
  - delivery: SEND TO LIVE (Remote Script, new tracks and clips only), EXPORT (multitrack MIDI, clips per section, WAV FX,
    guide), drag & drop of any clip into the DAW.
- Mastering: loudness targets reached by a closed loop (±0.5 LU), tonal balance shelves, 3-band leveller (LR4),
  soft clipper, band-limited 4x true-peak look-ahead limiter (true peak within 0.15 dB of the ceiling, FFT-verified).
- New pages COMPOSE / SECTIONS / MASTER, command strip in the header, MASTER chain view, terser French UI.
- FINISH MY TRACK uses what was just played (no need to arm listening first).
- Tests: 11,700+ checks incl. end-to-end (play a drop, one click, arrangement + master + preview + export), Live bridge mock test;
  pluginval strictness 10.


## 1.2.0
- New hardware faceplate ("synthwave night machine", model FN-1): brushed anodised chassis with screws and neon trim,
  neon-tube logo, round illuminated arcade FINISH key, CRT track-map screen (scanlines, synthwave grid and sun),
  LCD section console with LED-ladder Section DNA, backlit A/B/C pads, rubber keys with LEDs, 7-segment score,
  LED-ring knobs with knurled caps, backlit needle VU meters (IN / OUT + GR). Window 1200x800, scalable 60-160 %.
- No change to the audio engine or FINISH MY TRACK logic (123 checks, pluginval strictness 10).

## 1.1.0
- FINISH MY TRACK redesigned around the whole song:
  - Track map on the DAW timeline (host song position), sections detected from loudness and texture novelty,
    confidence per section, "not sure" readings with confirmation, relabel and draggable markers.
  - Section DNA (energy, density, punch, low, air, width, slope, musical role).
  - Transition Intelligence for every boundary, able to conclude a transition already works.
  - Context-aware section diagnosis that can point to a different section/transition.
  - Adaptive A/B/C variants per section and transition (role-specific), Regenerate, ranking with intent and risk refusals.
  - Preview (live, with amount), Apply, Keep Original, Undo (edits and structure), Coherence parameter + warnings.
  - Edits automated in time on the audio thread (lock-free plan hand-over, 120 ms edge fades, trims after level match).
  - Track map, corrections and edits saved with the project.
- New "Nocturne" visual identity: night palette, aurora gradient, crescent-moon mark, starfield, embedded
  Unbounded / Manrope / JetBrains Mono (OFL), new knobs, cards, track map, resizable window (66-160 %).
- Tests: 123 checks; pluginval strictness 10 passes.

## 1.0.0
- Rewritten as a working VST3/AU (JUCE 8, CMake). Compiles with zero warnings on the project code.
- Real analysis: gated BS.1770 loudness, PLR, loudness range, low/high balance, width, correlation, stereo bass, transient strength.
- Real section detection (INTRO/VERSE/BUILD/DROP/BREAK/OUTRO) from the loudness contour, replaces the sine placeholder.
- Diagnosis engine: 13 explained diagnoses with severity/confidence, FINISH SCORE, reference-relative thresholds.
- Intent parser (English + French) with negation handling and refusals when a request would degrade the mix.
- Decision engine ranks PUNCH / WIDE / MASSIVE and declines strategies that would hurt the track.
- Reference track loading (WAV/AIFF/FLAC/MP3/OGG) analysed on a background thread.
- New DSP chain: transient shaper, glue compressor, warmth tilt (previously unused EQ), M/S width with mono bass,
  density, -1 dBFS limiter, automatic A/B level match, click-free bypass. Neutral = bit-transparent.
  Removed the always-on tanh distortion of rc1.
- Thread safety: audio thread no longer allocates or shares std::vector/String with the UI (lock-free FIFOs + atomics).
- Full UI: energy timeline, live spectrum, score, opportunities, intent, strategy cards, 6 knobs, meters.
- State saves parameters, intent, reference path and the last analysis.
- Offline test runner (79 checks), pluginval strictness 10 passed, GitHub Actions for Windows + macOS with pluginval validation.

## 1.0.0-rc1
- Consolidated V1 source release (prototype skeleton).
