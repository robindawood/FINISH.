# FINISH V1 QA Matrix

| Area | Cases |
|---|---|
| DAWs | Ableton Live, Logic Pro, Cubase, FL Studio, Reaper |
| Formats | VST3, AU |
| Rates | 44.1, 48, 88.2, 96 kHz |
| Channels | mono, stereo |
| Playback | realtime, loop, stop/start, offline render |
| State | save project, reopen, preset recall |
| Automation | all exposed parameters |
| Audio | quiet, loud, clipped, silent, highly dynamic |
| Music | electronic, pop, hip-hop, rock, ambient |
| UX | analyze, finish, preview, bypass, reset |

Acceptance target: no crash, no clicks introduced by parameter changes, deterministic offline render, and no material level jump when A/B is engaged.

## Automated
- `Tests/TestMain.cpp`: 123 offline checks (incl. FINISH MY TRACK) (DSP safety, level match, sample rates 44.1-192 kHz, diagnosis, sections, intent FR/EN, block sizes, mono, state).
- pluginval strictness 10: passed (Linux VST3 build). CI runs pluginval strictness 5 on Windows and macOS.
- Still manual: listening tests in FL Studio and Ableton Live with real tracks, threshold tuning per genre.
- FINISH MY TRACK in real DAWs: verify host song position in FL Studio (Song mode vs Pattern mode) and Ableton (Arrangement vs Session), loops, tempo changes.
