#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ChainCanvas : public juce::Component
{
public:
    ChainCanvas() = default;
    void paint(juce::Graphics&) override;
};
