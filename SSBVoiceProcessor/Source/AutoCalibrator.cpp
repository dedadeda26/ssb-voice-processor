#include "AutoCalibrator.h"
#include "CalibrationAnalysis.h"
#include "Nodes/SpectralNrNode.h"
#include <algorithm>
#include <vector>
#include <limits>

AutoCalibrator::AutoCalibrator(AudioEngine& e, std::function<double()> clock)
    : engine(e), now(clock ? std::move(clock) : [] { return juce::Time::getMillisecondCounterHiRes(); }) {}
AutoCalibrator::~AutoCalibrator() { stopTimer(); engine.stopCapture(); }

void AutoCalibrator::start(double seconds)
{
    if (running) return;
    double sr = engine.getCurrentSampleRate();
    if (!std::isfinite(seconds) || seconds <= 0.0 || !std::isfinite(sr) || sr <= 0.0
        || sr * seconds > static_cast<double>(std::numeric_limits<int>::max())
        || !engine.isCaptureReady() || engine.isBypass())
    {
        finish(Result::failed, juce::String::fromUTF8(u8"校准失败：请启用音频输入/输出设备并关闭总旁路"));
        return;
    }
    targetSamples = (int)(sr * seconds);
    if (!engine.startCapture(targetSamples))
    {
        finish(Result::failed, juce::String::fromUTF8(u8"校准失败：无法开始音频采集"));
        return;
    }
    running = true;
    captureSampleRate = sr;
    startedAt = lastSamplesAt = now();
    deadlineMs = seconds * 1000.0 + 3000.0;
    lastSampleCount = 0;
    lastStage = -1;
    startTimerHz(30);
    if (onProgress) onProgress(0.0f);
    if (onStage) onStage(0, juce::String::fromUTF8(u8"采集环境噪声：请保持安静"));
    lastStage = 0;
}

void AutoCalibrator::cancel()
{
    if (!running) return;
    finish(Result::cancelled, juce::String::fromUTF8(u8"已取消校准，保留当前参数"));
}

void AutoCalibrator::finish(Result result, const juce::String& message)
{
    stopTimer();
    engine.stopCapture();
    running = false;
    if (onStage) onStage(result == Result::succeeded ? 3 : 4, message);
    if (onFinished) onFinished(result, message);
}

void AutoCalibrator::timerCallback()
{
    if (!running) return;
    int cur = engine.getCapturedSamples();
    const double time = now();
    if (!engine.isCaptureReady() || engine.getCurrentSampleRate() != captureSampleRate)
    {
        finish(Result::failed, juce::String::fromUTF8(u8"校准失败：音频设备已停止或断开"));
        return;
    }
    if (cur != lastSampleCount)
    {
        lastSampleCount = cur;
        lastSamplesAt = time;
    }
    if (cur < targetSamples && (time - lastSamplesAt >= 2000.0 || time - startedAt >= deadlineMs))
    {
        finish(Result::failed, juce::String::fromUTF8(u8"校准失败：音频采集超时，请检查设备"));
        return;
    }
    float progress = targetSamples > 0 ? (float)cur / (float)targetSamples : 1.0f;
    progress = juce::jlimit(0.0f, 1.0f, progress);
    if (onProgress) onProgress(progress);

    int stage = (progress < 0.25f) ? 0 : 1;
    if (stage != lastStage)
    {
        lastStage = stage;
        if (onStage)
            onStage(stage, stage == 0
                ? juce::String::fromUTF8(u8"采集环境噪声：请保持安静")
                : juce::String::fromUTF8(u8"采集语音：请正常说话"));
    }

    if (cur >= targetSamples)
    {
        stopTimer();
        engine.stopCapture();
        if (onStage) onStage(2, juce::String::fromUTF8(u8"正在应用参数…"));
        const auto error = analyzeAndApply();
        if (error.isEmpty())
            finish(Result::succeeded, juce::String::fromUTF8(u8"自适应调参完成：") + lastSummary);
        else
            finish(Result::failed, juce::String::fromUTF8(u8"校准失败：") + error);
    }
    else if (!engine.isCapturing())
    {
        finish(Result::failed, juce::String::fromUTF8(u8"校准失败：音频采集提前结束"));
    }
}

juce::String AutoCalibrator::analyzeAndApply()
{
    auto enabled = [&](const juce::String& name) {
        for (int i = 0; i < engine.getNumNodes(); ++i)
            if (engine.getNode(i)->getName() == name) return !engine.getNode(i)->isBypassed();
        return false;
    };
    const auto& buffer = engine.getCaptureBuffer();
    const int captured = engine.getCapturedSamples();
    const auto* samples = buffer.getReadPointer(0);
    const auto plan = CalibrationAnalysis::analyze(samples, captured, captureSampleRate,
                                                  enabled("Input Gain"), enabled("High-Pass"));
    if (!plan.valid()) return plan.error;

    // Measure the recording, then derive a value for every module. The rule
    // engine is the intelligent part; `model` may only apply bounded residuals
    // (it is a no-op until weights are trained), so this stays predictable.
    const auto features = SmartTune::extract(samples, captured, captureSampleRate);
    auto settings = SmartTune::propose(features, samples, captured, captureSampleRate, plan, model.get(),
                                       enabled("Input Gain"), enabled("High-Pass"));

    auto setParam = [&](const juce::String& nodeName, const juce::String& paramId, float value) {
        for (int i = 0; i < engine.getNumNodes(); ++i)
        {
            auto* node = engine.getNode(i);
            if (node == nullptr) continue;
            if (node->getName() != nodeName) continue;
            for (auto& p : node->getParameters())
                if (p.id == paramId && p.param != nullptr)
                    p.param->set(value);
        }
    };

    juce::String summary;
    {
        // Apply, then render the captured speech through the real chain so the
        // output gain is trimmed against measured behaviour, not a guess.
        const juce::ScopedLock applyLock(engine.getDeviceManager().getAudioCallbackLock());
        SmartTune::applySettings(engine, settings);

        // The first quarter of the capture is the quiet part: that is exactly the
        // noise profile the spectral stage needs.
        for (int i = 0; i < engine.getNumNodes(); ++i)
            if (auto* nr = dynamic_cast<SpectralNrNode*>(engine.getNode(i)))
                nr->setNoiseProfile(samples, captured / 4, captureSampleRate);

        // Aim the loudness at a fixed distance below the ceiling: asking for an
        // absolute RMS would just push the limiter harder and squash the signal.
        float ceilingDb = settings.lookup("Limiter", "limitThreshold", -1.5f);
        const float targetRmsDb = juce::jlimit(-16.0f, -10.0f, ceilingDb - 11.5f);

        float outGain = 0.0f;
        auto measurement = SmartTune::measureChain(engine, samples, captured);
        for (int pass = 0; pass < 4 && measurement.rmsDb > -99.0f; ++pass)
        {
            // Two constraints: reach the loudness target, but never push the peak
            // past the limiter ceiling now that the output trim sits after it.
            const float delta = juce::jlimit(-24.0f, 12.0f,
                                             juce::jmin(targetRmsDb - measurement.rmsDb,
                                                        ceilingDb - measurement.peakDb));
            if (std::abs(delta) < 0.5f) break;
            const float previousRms = measurement.rmsDb;
            outGain = juce::jlimit(-24.0f, 12.0f, outGain + delta);
            setParam("Output Gain", "outGain", outGain);
            measurement = SmartTune::measureChain(engine, samples, captured);
            // Once the limiter eats more than half of the requested gain, more
            // input only squashes the signal: stop at the level we reached.
            if (delta > 0.0f && measurement.rmsDb - previousRms < delta * 0.5f) break;
        }
        settings.set("Output Gain", "outGain", outGain);

        summary = settings.summary
                + juce::String::fromUTF8(u8" · 输出 ") + juce::String(outGain, 1) + "dB";
        if (measurement.rmsDb > -99.0f)
            summary += juce::String::fromUTF8(u8"（预计 ") + juce::String(measurement.rmsDb, 1) + " dBFS RMS";
        if (measurement.limiterGrDb > 0.2f)
            summary += juce::String::fromUTF8(u8"，限幅 ") + juce::String(measurement.limiterGrDb, 1) + "dB";
        if (measurement.rmsDb > -99.0f) summary += juce::String::fromUTF8(u8"）");

        engine.stopCapture();
    }

    lastSummary = summary;
    engine.beginSoftTransition();
    return {};
}
