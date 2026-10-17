#pragma once
#include "DspNode.h"
#include <vector>

// Message-thread control. Capture the actual advanced settings as the anchor,
// so touching a macro never projects them onto an unrelated average value.
class SimpleMacro
{
public:
    explicit SimpleMacro(DspNode& node)
    {
        const auto name = node.getName();
        auto parameters = node.getParameters();
        juce::String primary;
        float primaryMin = 0, primaryMax = 100;
        if (name == "EQ") { primary = "highGain"; primaryMin = -6; primaryMax = 6; }
        if (name == "Multi-Band") primary = "midAmount";
        if (name == "Compressor") { primary = "ratio"; primaryMin = 1; primaryMax = 8; }
        if (name == "Saturation") { primary = "drive"; primaryMax = 12; }
        if (name == "De-Esser") primary = "deEssAmount";
        if (name == "Noise Gate") primary = "gateAmount";
        for (const auto& p : parameters)
            if (p.id == primary && p.param)
                anchor = juce::jlimit(0.0f, 1.0f, (p.param->get() - primaryMin) / (primaryMax - primaryMin));

        auto bind = [&](const char* id, float start, float end) {
            for (auto& p : parameters)
                if (p.id == id && p.param)
                {
                    const float value = p.param->get();
                    // Preserve monotonic direction even for custom endpoints.
                    if (start <= end) { start = juce::jmin(start, value); end = juce::jmax(end, value); }
                    else { start = juce::jmax(start, value); end = juce::jmin(end, value); }
                    bindings.push_back({ p.param, value, start, end });
                }
        };
        if (name == "EQ") { bind("lowGain", 6, -6); bind("midGain", -2, 3); bind("highGain", -6, 6); }
        if (name == "Multi-Band") { bind("lowAmount", 0, 85); bind("midAmount", 0, 85); bind("highAmount", 0, 70); }
        if (name == "Compressor") { bind("threshold", -6, -36); bind("ratio", 1, 8); bind("makeup", 0, 3); }
        if (name == "Saturation") { bind("drive", 0, 12); bind("mix", 0, 100); }
        if (name == "De-Esser") { bind("deEssAmount", 0, 100); bind("deEssThresh", -12, -42); }
        if (name == "Noise Gate") { bind("gateAmount", 0, 100); bind("gateThreshold", -80, -30); }
    }

    float initialValue() const { return anchor * 100.0f; }
    void apply(float percent)
    {
        const float t = juce::jlimit(0.0f, 1.0f, percent * 0.01f);
        for (const auto& b : bindings)
        {
            float value = b.original;
            if (t < anchor && anchor > 0)
                value = b.start + (b.original - b.start) * (t / anchor);
            else if (t > anchor && anchor < 1)
                value = b.original + (b.end - b.original) * ((t - anchor) / (1 - anchor));
            b.param->set(value);
        }
    }

private:
    struct Binding { Param* param; float original, start, end; };
    std::vector<Binding> bindings;
    float anchor = 0.5f;
};
