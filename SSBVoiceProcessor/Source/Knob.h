#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class Knob : public juce::Slider
{
public:
    Knob(const juce::String& name = {});
    ~Knob() override = default;

    void setKnobName(const juce::String& n) { juce::Component::setName(n); repaint(); }
    juce::String getKnobName() const { return juce::Component::getName(); }

    void setAccentColour(juce::Colour c) { accentColour = c; repaint(); }

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    bool hovered = false;
    bool dragging = false;
    juce::Colour accentColour;
};
