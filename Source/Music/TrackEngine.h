#pragma once
#include <JuceHeader.h>
#include "SectionTypes.h"
#include "SectionPlan.h"
#include "VariantGenerator.h"
#include "../Analysis/AudioAnalyzer.h"
#include "../Diagnosis/Diagnosis.h"
#include <optional>

// FINISH MY TRACK: Whole Track Intelligence, Section DNA, Transition Intelligence, Adaptive Variants,
// Coherence & Preview. Message thread only. Produces a SectionPlan that the audio thread follows in time.
class TrackEngine
{
public:
    struct Selection
    {
        enum class Kind { Track, Section, Transition };
        Kind kind = Kind::Track;
        int index = -1;
        bool operator== (const Selection& o) const noexcept { return kind == o.kind && index == o.index; }
        bool operator!= (const Selection& o) const noexcept { return ! (*this == o); }
        static Selection track() { return {}; }
        static Selection section (int i) { return { Kind::Section, i }; }
        static Selection transition (int i) { return { Kind::Transition, i }; }
    };

    struct Edit
    {
        int key = 0;                  // section i -> i, transition i -> 1000 + i
        Variant variant;
        float amount = 1.0f;
        bool keepOriginal = false;
        juce::String source;          // "DROP 2 · B Evolved"
    };

    // ---- Analysis ---------------------------------------------------------------------
    void build (const std::vector<AnalysisFrame>& sessionFrames);
    void setContext (const std::vector<Diagnosis>& globalDiagnoses, const IntentVector& intent);
    void clear();
    bool hasMap() const noexcept { return ! music.sections.empty(); }

    const MusicState& getMusic() const noexcept { return music; }
    const std::vector<SectionDNA>& getDNA() const noexcept { return dna; }
    const std::vector<SectionVerdict>& getVerdicts() const noexcept { return verdicts; }
    const std::vector<Transition>& getTransitions() const noexcept { return transitions; }
    const std::vector<Variant>* variantsFor (Selection) const;

    // ---- Structure corrections (the producer has the last word) -------------------------
    void setSectionType (int section, SectionType);
    void confirmSection (int section);
    bool moveBoundary (int boundary, double newTime);   // boundary i = between section i and i + 1

    // ---- Variants, preview, apply, undo --------------------------------------------------
    void preview (Selection, int variantIndex, float amount = 1.0f);   // -1 = hear the section untouched
    void clearPreview();
    std::optional<std::pair<Selection, int>> getPreview() const noexcept { return previewState; }
    float getPreviewAmount() const noexcept { return previewAmount; }
    void apply (Selection, int variantIndex, float amount);
    void keepOriginal (Selection);
    void setAmount (Selection, float amount);
    void regenerate (Selection);
    bool undo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    void removeAllEdits();

    const Edit* editFor (Selection) const;
    const std::vector<Edit>& getEdits() const noexcept { return edits; }
    std::vector<Edit> effectiveEdits() const;
    SectionPlan compilePlan() const;
    juce::StringArray coherenceWarnings (float coherence) const;
    std::pair<double, double> rangeOf (const Edit&) const;
    std::pair<double, double> rangeOf (Selection) const;

    int getVersion() const noexcept { return version; }

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

    static int keyFor (Selection s) { return s.kind == Selection::Kind::Transition ? 1000 + s.index : s.index; }
    // Exposed for tests: 100 ms grid on song time from frames stamped with host positions.
    static std::vector<AnalysisFrame> buildGrid (const std::vector<AnalysisFrame>& frames, double& startTime);

private:
    void analyseStructure();
    void rankAll();
    void pushUndo();
    void changed() { ++version; }
    juce::String sourceName (Selection, const Variant&) const;

    std::vector<AnalysisFrame> grid;
    MusicState music;
    std::vector<SectionDNA> dna;
    std::vector<SectionVerdict> verdicts;
    std::vector<Transition> transitions;
    std::vector<std::vector<Variant>> sectionVariants;
    std::map<int, int> generations;

    std::vector<Edit> edits;
    struct UndoEntry { std::vector<Section> sections; std::vector<Edit> edits; };
    std::vector<UndoEntry> undoStack;
    int lastAmountKey = -1;

    std::optional<std::pair<Selection, int>> previewState;
    float previewAmount = 1.0f;
    IntentVector intent;
    VariantRanker::Risks risks;
    int version = 0;
};
