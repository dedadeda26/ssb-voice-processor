#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    LevelMeter(const juce::String& label,
               std::function<float()> levelGetter,
               bool vertical = true);
    ~LevelMeter() override = default;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::String labelText;
    std::function<float()> getter;
    bool vertical;
    float smoothValue = 0.0f;
};
