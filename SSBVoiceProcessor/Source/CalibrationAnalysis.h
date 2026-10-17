#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace CalibrationAnalysis
{
struct Plan
{
    juce::String error;
    float inputGain = 0, gateThreshold = -50, gateAmount = 40;
    float rawNoiseDb = -100, speechDb = -100, gateNoiseDb = -100;
    bool valid() const { return error.isEmpty(); }
};

inline Plan analyze(const float* samples, int count, double sampleRate,
                    bool inputGainEnabled, bool highPassEnabled)
{
    Plan plan;
    auto fail = [&](const char* message) { plan.error = juce::String::fromUTF8(message); return plan; };
    if (samples == nullptr || !std::isfinite(sampleRate) || sampleRate <= 0 || count < sampleRate)
        return fail(u8"录音太短，请重新校准");
    const int frameSize = juce::jmax(1, (int) std::round(sampleRate * .02));
    const int noiseEnd = count / 4;
    auto percentile = [](std::vector<float> values, float fraction) {
        if (values.empty()) return 0.0f;
        std::sort(values.begin(), values.end());
        return values[juce::jmin(values.size()-1, (size_t)(fraction * (float)values.size()))];
    };
    std::vector<float> noise, speech;
    int clipped = 0;
    float peak = 0;
    for (int i = 0; i < count; ++i)
    {
        if (!std::isfinite(samples[i])) return fail(u8"录音包含无效数值，请检查设备");
        peak = juce::jmax(peak, std::abs(samples[i]));
        if (std::abs(samples[i]) >= .999f) ++clipped;
    }
    if (peak > 1.001f || clipped > count * .005) return fail(u8"输入明显削波，请降低麦克风增益后重试");
    for (int pos = 0; pos + frameSize <= count; pos += frameSize)
    {
        if (pos < noiseEnd && pos + frameSize > noiseEnd) continue;
        double sum = 0;
        for (int i = pos; i < pos + frameSize; ++i) sum += (double)samples[i] * samples[i];
        (pos < noiseEnd ? noise : speech).push_back((float)std::sqrt(sum / frameSize));
    }
    const float noiseRms = percentile(noise, .9f);
    const float speechRms = percentile(speech, .7f);
    plan.rawNoiseDb = juce::Decibels::gainToDecibels(noiseRms, -100.0f);
    plan.speechDb = juce::Decibels::gainToDecibels(speechRms, -100.0f);
    const float activityFloor = juce::jmax(juce::Decibels::decibelsToGain(-50.0f), noiseRms * 3.162278f);
    int activeFrames = 0;
    for (float rms : speech) if (rms > activityFloor) ++activeFrames;
    const double activitySeconds = (double)activeFrames * frameSize / sampleRate;
    const double requiredSeconds = juce::jmin(1.0, (double)(count-noiseEnd) / sampleRate * .5);
    if (plan.speechDb < -50 || plan.speechDb - plan.rawNoiseDb < 10 || activitySeconds < requiredSeconds)
        return fail(u8"未采集到足够清晰的语音：前段保持安静，后段正常说话后重试");

    plan.inputGain = juce::jlimit(-12.0f, 6.0f, -16.0f - plan.speechDb);
    if (peak > 0)
        plan.inputGain = juce::jmin(plan.inputGain, -1.0f - juce::Decibels::gainToDecibels(peak));

    // Simulate the exact proposed gate input without touching live DSP state.
    juce::dsp::IIR::Filter<float> filter;
    *filter.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(sampleRate, 100.0f, .707f);
    filter.prepare({ sampleRate, (juce::uint32)frameSize, 1 });
    const float gain = inputGainEnabled ? juce::Decibels::decibelsToGain(plan.inputGain) : 1.0f;
    std::vector<float> gateNoise;
    double sum = 0;
    for (int i = 0; i < noiseEnd; ++i)
    {
        const float sample = (highPassEnabled ? filter.processSample(samples[i]) : samples[i]) * gain;
        sum += (double)sample * sample;
        if ((i + 1) % frameSize == 0) { gateNoise.push_back((float)std::sqrt(sum / frameSize)); sum = 0; }
    }
    plan.gateNoiseDb = juce::Decibels::gainToDecibels(percentile(gateNoise, .9f), -100.0f);
    plan.gateThreshold = juce::jlimit(-80.0f, -20.0f, plan.gateNoiseDb + 8.0f);
    plan.gateAmount = plan.gateNoiseDb > -55 ? 70.0f : 40.0f;
    return plan;
}
}
