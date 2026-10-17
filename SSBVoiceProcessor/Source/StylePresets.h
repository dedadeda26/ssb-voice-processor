#pragma once
#include <juce_core/juce_core.h>
#include "AudioEngine.h"

class StylePresets
{
public:
    static void applyStyle(AudioEngine& engine, int styleId);
};
