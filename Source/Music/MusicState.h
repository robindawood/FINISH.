#pragma once
#include <JuceHeader.h>
#include <vector>

enum class SectionType { Excerpt, Intro, Verse, Chorus, Build, Drop, Break, Bridge, Outro };

// The musical job of a section (Section DNA).
enum class SectionRole { Whole, SetMood, Carry, Anticipate, FirstPeak, NewPeak, Lift, Breathe, Conclude };

inline juce::String sectionTypeName (SectionType t)
{
    switch (t)
    {
        case SectionType::Intro:   return "INTRO";
        case SectionType::Verse:   return "VERSE";
        case SectionType::Chorus:  return "CHORUS";
        case SectionType::Build:   return "BUILD";
        case SectionType::Drop:    return "DROP";
        case SectionType::Break:   return "BREAK";
        case SectionType::Bridge:  return "BRIDGE";
        case SectionType::Outro:   return "OUTRO";
        case SectionType::Excerpt: break;
    }
    return "EXCERPT";
}

inline juce::String roleName (SectionRole r)
{
    switch (r)
    {
        case SectionRole::SetMood:    return "Set the mood";
        case SectionRole::Carry:      return "Carry the groove";
        case SectionRole::Anticipate: return "Build anticipation";
        case SectionRole::FirstPeak:  return "First peak";
        case SectionRole::NewPeak:    return "New peak";
        case SectionRole::Lift:       return "Lift";
        case SectionRole::Breathe:    return "Let it breathe";
        case SectionRole::Conclude:   return "Conclude";
        case SectionRole::Whole:      break;
    }
    return "Whole excerpt";
}

inline bool isPeakType (SectionType t)  { return t == SectionType::Drop || t == SectionType::Chorus; }
inline bool isBreathType (SectionType t) { return t == SectionType::Break || t == SectionType::Bridge; }

struct Section
{
    double startTime = 0, endTime = 0;    // host song position, seconds
    SectionType type = SectionType::Excerpt;
    SectionType alternative = SectionType::Excerpt;   // second-best reading when unsure
    juce::String label;                   // "DROP 2"
    float loudness = 0;                   // mean short-term LUFS
    float confidence = 1.0f;              // 0..1, how sure FINISH is about this section
    bool confirmed = false;               // the user confirmed or edited it

    double length() const noexcept { return endTime - startTime; }
    bool isUncertain() const noexcept { return ! confirmed && confidence < 0.6f; }
};

struct MusicState
{
    double startTime = 0;                 // song position of energyCurve[0]
    double durationSec = 0;
    static constexpr double frameSec = 0.1;
    std::vector<float> energyCurve;       // smoothed loudness (LUFS) every 100 ms
    std::vector<Section> sections;
    float contrastDb = 0;                 // loud parts vs quiet parts

    double endTime() const noexcept { return startTime + durationSec; }
    int sectionAt (double t) const noexcept
    {
        for (size_t i = 0; i < sections.size(); ++i)
            if (t >= sections[i].startTime && t < sections[i].endTime) return (int) i;
        return -1;
    }
};
