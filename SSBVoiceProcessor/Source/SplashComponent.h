#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class SplashComponent : public juce::Component,
                        private juce::Timer
{
public:
    SplashComponent();
    ~SplashComponent() override;

    std::function<void()> onFinished;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    double startTimeMs = 0.0;
    double elapsedSec  = 0.0;

    float ringRotation = 0.0f;
    float pulsePhase   = 0.0f;

    juce::Image iconImage;

    // 时间轴（秒）
    static constexpr double kTotalDuration = 3.6;
    static constexpr double kFadeOutStart  = 3.1;

    // 外发光厚度（组件四周预留的内边距）
    static constexpr int    kGlowPad   = 30;
    // 卡片本体尺寸
    static constexpr int    kCardW     = 580;
    static constexpr int    kCardH     = 380;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashComponent)
};
