#pragma once
#include "NightTheme.h"
#include "../PluginProcessor.h"

// TRACK MAP: energy curve, sections (with confidence), transition nodes, applied edits, playhead.
// Click a section or a transition node to inspect it. Drag a boundary to move it. Right-click a section to relabel.
class TrackMapView : public juce::Component
{
public:
    using Selection = TrackEngine::Selection;

    explicit TrackMapView (FINISHAudioProcessor&);

    std::function<void (Selection)> onSelect;
    void setSelection (Selection s) { selection = s; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> glassArea() const;
    juce::Rectangle<float> plot() const;
    juce::Rectangle<float> labelRow() const;
    juce::Rectangle<float> curveArea() const;
    juce::Rectangle<float> rail() const;
    std::pair<double, double> range() const;
    float xFor (double t) const;
    double timeFor (float x) const;
    int boundaryNear (juce::Point<float>) const;
    int nodeAt (juce::Point<float>) const;
    int sectionAt (juce::Point<float>) const;
    void showSectionMenu (int section);

    void paintIdle (juce::Graphics&);
    void paintListening (juce::Graphics&);
    void paintMap (juce::Graphics&);
    void paintCurve (juce::Graphics&, const std::vector<float>& curve, double t0, double step, bool live);
    void paintAxis (juce::Graphics&);
    void paintPlayhead (juce::Graphics&);

    FINISHAudioProcessor& proc;
    Selection selection;
    juce::TextButton wholeTrack { "WHOLE TRACK" };
    std::vector<Night::Star> stars;
    int hoverBoundary = -1, hoverNode = -1, hoverSection = -1;
    int dragBoundary = -1;
    double dragTime = 0;
};
