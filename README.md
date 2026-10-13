# WHALE — Mastering

Mastering plug-in (VST3 / AU / Standalone, macOS + Windows) built with JUCE 8.

- **AUTO**: listens 30 s, measures the loudest 60 % of it exactly like the reference profiles (`Resources/Profiles`):
  1/3-octave balance vs the style's median curve (interquartile tolerance), fitted onto the EQ tools; dynamics split
  between compression, clipper and limiter; width / mono bass; then measures the processed output, corrects what is
  still off and reports the remaining distance. Every move has a reason line in the AUTO report; problems that belong to the mix are reported, not "fixed".
- **Styles** (from the user's reference tracks): Tech House / House, Afro / Latin House, Hard Techno / Rave, Drum & Bass,
  UK Garage / Bass, Lo-fi / Chill, Afro Tech / Riddim, Afro / Dancehall — or Auto detection (tempo plausibility + distance of the 1/3-octave curve to each style).
- **Presets**: factory presets are a character layered on AUTO (colour built from the style's reference spread, punch / saturation / density / loudness signature); AUTO's correction is kept.
- **Chain**: LOW-CUT (36 dB/oct, set by AUTO) → SPECTRAL (4 dynamic bells, movable centres) → COMPRESSION (punch + 3-band leveller) → BASSES/AIR → LARGEUR (sides + mono bass)
  → x4 oversampled SATURATION + CLIPPER → true-peak look-ahead LIMITEUR. Linear-phase oversampling, ~2.9 ms latency at 44.1 kHz.
- **Main screen**: 8 macros (0 = AUTO's choice), style / target / dosage, presets (30 factory + user presets), SANS/AVEC level-matched on the loud passages.
- **Meters** (what is heard): 4x true peak, TONALITÉ (distance to the style's references, preset colour excluded), DYNAMIQUE (vs what the target leaves), stereo correlation, measured bass mono.
- **The whale**: drawn from the sound (each line = a band), rises to the target surface, breaks through when too loud,
  ghost double on phase problems, sparks when clipping, "le souffle" when the master is validated.

## Build

```
cmake -S . -B build -DWHALE_BUILD_TESTS=ON
cmake --build build --config Release --target WHALE_All WhaleTests
build/WhaleTests_artefacts/Release/WhaleTests            # offline tests
build/WhaleTests_artefacts/Release/WhaleTests --snap shots [track.wav]
build/WhaleTests_artefacts/Release/WhaleTests --render in.wav out.wav [style]
```

CI (`.github/workflows/build.yml`) runs the tests and pluginval (strictness 10) and publishes the macOS .pkg and
Windows Setup.exe on the `installers-whale` branch.

## Reference profiles

`Tools/profiles/classify.py` analyses reference masters (loudness, true peak, PLR, 1/3-octave balance, stereo per band,
tempo), sorts them by style and writes `Resources/Profiles/*.json`; `make_header.py` compiles them into
`Source/Auto/StyleProfilesData.h`. Audio files are not stored in the repository.
