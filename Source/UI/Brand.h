#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

// Rawdio Labs logo (symbol + wordmark), drawn from the brand kit's SVG sources.
namespace Brand
{
    inline juce::Drawable* load (const char* data, int size)
    {
        if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size)))
            return juce::Drawable::createFromSVG (*xml).release();
        return nullptr;
    }
    inline juce::Drawable* symbol()   { static std::unique_ptr<juce::Drawable> d (load (BinaryData::rawdiosymbolcyan_svg, BinaryData::rawdiosymbolcyan_svgSize)); return d.get(); }
    inline juce::Drawable* wordmark() { static std::unique_ptr<juce::Drawable> d (load (BinaryData::rawdiowordmarkwhite_svg, BinaryData::rawdiowordmarkwhite_svgSize)); return d.get(); }

    // symbol + wordmark in one line, height h, left at x (returns the width used)
    inline float drawLockup (juce::Graphics& g, float x, float cy, float h, float alpha)
    {
        float used = 0.0f;
        if (auto* s = symbol())  { s->drawWithin (g, { x, cy - h * 0.5f, h, h }, juce::RectanglePlacement::centred, alpha); used += h + h * 0.18f; }
        if (auto* w = wordmark()) { const float wh = h * 0.95f, ww = wh * 664.59f / 184.0f; w->drawWithin (g, { x + used, cy - wh * 0.5f, ww, wh }, juce::RectanglePlacement::centred, alpha); used += ww; }
        return used;
    }
    inline float lockupWidth (float h) { return h + h * 0.18f + h * 0.95f * 664.59f / 184.0f; }
}
