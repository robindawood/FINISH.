# FINISH V1 — Product Specification

Analyze → Understand → Propose → Preview → Apply.

FINISH answers: “What is missing from this track for it to feel finished?”

V1 modes: Finish My Track, Intent, Reference.

Strategies: PUNCH (impact/transients/contrast), WIDE (width/depth/mono protection), MASSIVE (density/controlled low-end/loudness).

Trust rules: reversible processing, volume-matched A/B, confidence on recommendations, and refusal when a requested change would likely degrade the mix.

V1 master-plugin limitation: a VST3 on the master cannot inspect individual DAW tracks. V1 therefore reasons from the stereo/master signal and optional reference input.

## FINISH MY TRACK (1.1)
Whole Track Intelligence, Section DNA, Transition Intelligence, Adaptive Variants, Coherence & Preview.

| Spec module | Implementation |
|---|---|
| SectionDetector | `Source/Music/SectionDetector.*` |
| SectionFeatureAnalyzer | `Source/Music/SectionFeatureAnalyzer.*` |
| SectionDNA | `SectionDNA` in `Source/Music/SectionTypes.h` |
| TransitionAnalyzer | `Source/Music/TransitionAnalyzer.*` |
| SectionDiagnosisEngine | `Source/Music/SectionDiagnosisEngine.*` |
| VariantGenerator / VariantRanker | `Source/Music/VariantGenerator.*` |
| SectionProcessor | `SectionPlan` (`Source/Music/SectionPlan.h`) evaluated in `processBlock` |
| PreviewManager / UndoManager | `TrackEngine` (`Source/Music/TrackEngine.*`) |
