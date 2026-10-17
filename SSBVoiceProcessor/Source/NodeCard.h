#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "DspNode.h"
#include "Knob.h"
#include "AudioEngine.h"

// Shared 3D-view colour gain: one value for the Input and Output cards, and it is
// written to the app settings on exit so it survives a restart.
float getSharedWaterfallGain();
void setSharedWaterfallGain(float value);

class NodeCard : public juce::Component,
                 private juce::Timer
{
public:
    NodeCard(DspNode* node, AudioEngine& engine, bool simpleMode);
    ~NodeCard() override = default;

    std::function<void()> onParametersChanged;
    void setLargePanel(bool b) { largePanel = b; resized(); repaint(); }
    // Re-read every knob from the DSP parameters (the card is cached while other
    // modules are shown, so its widgets can be out of date when it comes back).
    void refreshFromParams();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void buildControls();
    void buildSimpleMacros();
    void drawHelpIcon(juce::Graphics&, juce::Rectangle<float>);
    void drawVisualization(juce::Graphics&, juce::Rectangle<float>);

    DspNode* node = nullptr;
    AudioEngine& engine;
    bool simple = true;
    bool largePanel = false;
    bool fullCardWaterfall = false;

    juce::String displayTitle;
    juce::String nodeKey;
    bool onState = true;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Knob> macroKnobs;
    std::vector<std::function<void()>> refreshers;

    unsigned int configurationVersion = 0;
    int numKnobs = 0;
    int rows = 1;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> helpArea;
    juce::Rectangle<int> iconArea;
    juce::Rectangle<int> visArea;

    bool helpHovered = false;
    int helpHoverFrames = 0;
    bool showHelp = false;

    // 瀑布图增益滑块（仅 Input/Output Gain 使用）
    std::unique_ptr<juce::Slider> waterfallGainSlider;
    std::unique_ptr<juce::Label> waterfallGainLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeCard)
};
