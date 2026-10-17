#include "StylePresets.h"

namespace {
    void setParam(AudioEngine& engine, const juce::String& nodeName,
                  const juce::String& paramId, float value)
    {
        for (int i = 0; i < engine.getNumNodes(); ++i)
        {
            auto* node = engine.getNode(i);
            if (node == nullptr || node->getName() != nodeName) continue;
            for (auto& p : node->getParameters())
                if (p.id == paramId && p.param != nullptr) p.param->set(value);
        }
    }
}

void StylePresets::applyStyle(AudioEngine& engine, int styleId)
{
    switch (styleId)
    {
        case 1:
            setParam(engine, "EQ", "lowGain",   0.0f);
            setParam(engine, "EQ", "midGain",   0.0f);
            setParam(engine, "EQ", "highGain",  0.0f);
            setParam(engine, "Multi-Band", "lowAmount",  40.0f);
            setParam(engine, "Multi-Band", "midAmount",  40.0f);
            setParam(engine, "Multi-Band", "highAmount", 30.0f);
            setParam(engine, "Compressor", "ratio",     3.0f);
            setParam(engine, "Compressor", "threshold", -18.0f);
            setParam(engine, "Saturation", "drive",     4.0f);
            setParam(engine, "Saturation", "mix",       100.0f);
            setParam(engine, "De-Esser",  "deEssAmount", 40.0f);
            break;
        case 2:
            setParam(engine, "EQ", "lowGain",   4.0f);
            setParam(engine, "EQ", "midGain",   1.0f);
            setParam(engine, "EQ", "highGain",  0.5f);
            setParam(engine, "Multi-Band", "lowAmount",  60.0f);
            setParam(engine, "Multi-Band", "midAmount",  40.0f);
            setParam(engine, "Multi-Band", "highAmount", 25.0f);
            setParam(engine, "Compressor", "ratio",     3.5f);
            setParam(engine, "Compressor", "threshold", -20.0f);
            setParam(engine, "Saturation", "drive",     12.0f);
            setParam(engine, "Saturation", "mix",       70.0f);
            setParam(engine, "De-Esser",  "deEssAmount", 50.0f);
            break;
        case 3:
            setParam(engine, "EQ", "lowGain",  -3.0f);
            setParam(engine, "EQ", "midGain",   6.0f);
            setParam(engine, "EQ", "highGain",  3.0f);
            setParam(engine, "Multi-Band", "lowAmount",  50.0f);
            setParam(engine, "Multi-Band", "midAmount",  65.0f);
            setParam(engine, "Multi-Band", "highAmount", 40.0f);
            setParam(engine, "Compressor", "ratio",     5.0f);
            setParam(engine, "Compressor", "threshold", -22.0f);
            setParam(engine, "Saturation", "drive",     6.0f);
            setParam(engine, "Saturation", "mix",       80.0f);
            setParam(engine, "De-Esser",  "deEssAmount", 60.0f);
            break;
        case 4:
            setParam(engine, "EQ", "lowGain",   5.0f);
            setParam(engine, "EQ", "midGain",   2.0f);
            setParam(engine, "EQ", "highGain", -0.5f);
            setParam(engine, "Multi-Band", "lowAmount",  65.0f);
            setParam(engine, "Multi-Band", "midAmount",  50.0f);
            setParam(engine, "Multi-Band", "highAmount", 30.0f);
            setParam(engine, "Compressor", "ratio",     4.0f);
            setParam(engine, "Compressor", "threshold", -20.0f);
            setParam(engine, "Saturation", "drive",     14.0f);
            setParam(engine, "Saturation", "mix",       80.0f);
            setParam(engine, "De-Esser",  "deEssAmount", 55.0f);
            break;
        default: break;
    }
    engine.beginSoftTransition();
}
