#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class CalibrationOverlay : public juce::Component,
                           private juce::Timer
{
public:
    CalibrationOverlay();
    ~CalibrationOverlay() override = default;

    void setProgress(float p) { progress = juce::jlimit(0.0f, 1.0f, p); }
    void setStage(int idx, const juce::String& text)
    {
        stageIndex = idx;
        stageText = text;
        repaint();
    }

    // 实时音频电平回调（0-1）
    std::function<float()> levelGetter;
    std::function<void()> onCancel;
    void setCancelEnabled(bool enabled) { cancelButton.setEnabled(enabled); }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void timerCallback() override;
    juce::TextButton cancelButton;

    float progress = 0.0f;
    int stageIndex = 0;
    juce::String stageText;

    float t1 = 0.0f;
    float t2 = 0.0f;
    float smoothLevel = 0.0f;

    struct Aurora
    {
        float baseY;
        float amp;
        float freq;
        float phase;
        float speed;
        float baseAlpha;
    };
    std::vector<Aurora> auroras;
};
