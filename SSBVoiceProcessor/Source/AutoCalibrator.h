#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "AudioEngine.h"
#include "SmartTune.h"

class AutoCalibrator : private juce::Timer
{
public:
    enum class Result { succeeded, cancelled, failed };
    explicit AutoCalibrator(AudioEngine& engine, std::function<double()> clock = {});
    ~AutoCalibrator() override;

    void start(double seconds = 8.0);
    void cancel();
    bool isRunning() const { return running; }
    const juce::String& getLastSummary() const { return lastSummary; }

    std::function<void(float)> onProgress;
    std::function<void(int, const juce::String&)> onStage;
    std::function<void(Result, const juce::String&)> onFinished;

private:
    void timerCallback() override;
    juce::String analyzeAndApply();
    void finish(Result result, const juce::String& message);

    AudioEngine& engine;
    bool running = false;
    int targetSamples = 0;
    int lastStage = -1;
    std::function<double()> now;
    double startedAt = 0.0;
    double lastSamplesAt = 0.0;
    double deadlineMs = 0.0;
    int lastSampleCount = 0;
    double captureSampleRate = 48000;
    juce::String lastSummary;
    std::unique_ptr<SmartTune::ParamModel> model = SmartTune::defaultModel();
};
