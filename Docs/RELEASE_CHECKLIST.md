# FINISH V1 Release Checklist

[x] = covered by `Tests/TestMain.cpp` or by the code design. Items left open need real DAWs, real music or vendor accounts.

## Engineering
- [ ] Build Windows VST3
- [ ] Build macOS VST3
- [ ] Build macOS AU
- [x] Validate 44.1 / 48 / 88.2 / 96 kHz
- [x] Validate mono and stereo input
- [x] Validate offline rendering
- [x] Confirm no realtime-thread allocations
- [x] Confirm parameter automation
- [x] Confirm state save/restore

## Audio
- [ ] Loudness/true-peak validation
- [ ] FFT and spectral validation
- [ ] Transient validation
- [x] Stereo correlation / mono compatibility
- [ ] Section detection on representative genres
- [x] A/B volume matching
- [ ] Oversampling/limiter safety

## Product
- [x] Intent parser validation
- [x] Reference workflow
- [x] PUNCH/WIDE/MASSIVE ranking
- [x] Failure/refusal messages
- [ ] Factory intent presets

## Distribution
- [ ] Code signing Windows
- [ ] Apple signing + notarization
- [ ] Installer
- [ ] License activation backend
- [ ] Privacy policy / EULA
- [ ] Crash logging and update channel
