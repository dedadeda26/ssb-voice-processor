#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

#include "Nodes/BandLimitNode.h"
#include "Nodes/LimiterNode.h"
#include "Nodes/SaturationNode.h"
#include "Nodes/AutoLevelNode.h"

// Offline self test: runs the shipped nodes and reports the numbers this project
// is judged by (band-limit slope, aliasing, limiter ceiling, AGC convergence).
// Everything runs headless on a scratch signal, so it can be triggered while the
// app is idle; it never touches the live chain.
namespace SelfTest
{
struct Result
{
    juce::String name;
    bool passed = false;
    juce::String detail;
};

inline juce::String report(const std::vector<Result>& results)
{
    juce::String text;
    bool allPassed = true;
    for (const auto& r : results)
    {
        if (! r.passed) allPassed = false;
        text << (r.passed ? "PASS  " : "FAIL  ") << r.name << "  " << r.detail << "\n";
    }
    text << (allPassed ? "全部通过" : "存在未通过项");
    return text;
}

inline bool allPassed(const std::vector<Result>& results)
{
    for (const auto& r : results)
        if (! r.passed) return false;
    return ! results.empty();
}

// RMS level of a sine of `amplitude` after the node has settled.
inline float sineLevelThrough(double sampleRate, float amplitude, float frequency,
                              std::function<void(float*, int)> process, int total = 48000)
{
    std::vector<float> buffer((size_t) total);
    for (int i = 0; i < total; ++i)
        buffer[(size_t) i] = amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi
                                                          * (double) frequency * (double) i / sampleRate);
    for (int pos = 0; pos < total; pos += 512)
        process(buffer.data() + pos, juce::jmin(512, total - pos));

    const int from = total / 2; // settled half
    double sum = 0.0;
    for (int i = from; i < total; ++i) sum += (double) buffer[(size_t) i] * buffer[(size_t) i];
    return juce::Decibels::gainToDecibels((float) std::sqrt(sum / (double) (total - from)), -100.0f);
}

inline std::vector<Result> run(double sampleRate)
{
    std::vector<Result> results;
    if (! std::isfinite(sampleRate) || sampleRate < 8000.0)
    {
        results.push_back({ "采样率", false, "采样率无效" });
        return results;
    }

    // 1) Band limit: a 24 dB/oct 4th-order filter must be well down one octave up.
    {
        BandLimitNode node;
        node.prepare(sampleRate, 512);
        const float reference = juce::Decibels::gainToDecibels(0.25f * 0.70710678f, -100.0f);
        const float pass = sineLevelThrough(sampleRate, 0.25f, 1000.0f,
                                            [&](float* b, int n) { node.process(b, n); }) - reference;
        node.reset();
        const float stop = sineLevelThrough(sampleRate, 0.25f, juce::jmin(6200.0f, (float) sampleRate * 0.2f),
                                            [&](float* b, int n) { node.process(b, n); }) - reference;
        results.push_back({ "带限斜率", pass > -1.5f && pass < 1.5f && stop <= -20.0f,
                            "通带 " + juce::String(pass, 1) + " dB，2 倍截止处 " + juce::String(stop, 1) + " dB" });
    }

    // 2) Limiter: nothing may exceed the ceiling and the safety clamp must stay idle.
    {
        LimiterNode node;
        node.prepare(sampleRate, 512);
        node.thresholdDb.set(-6.0f);
        const float ceiling = juce::Decibels::decibelsToGain(-6.0f);
        std::vector<float> buffer(24000);
        for (size_t i = 0; i < buffer.size(); ++i)
            buffer[i] = 1.0f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 700.0 * (double) i / sampleRate);
        node.process(buffer.data(), (int) buffer.size());
        float peak = 0.0f;
        for (float v : buffer) peak = juce::jmax(peak, std::abs(v));
        results.push_back({ "限幅上限", peak <= ceiling + 1e-4f && node.getClampCount() == 0,
                            "峰值 " + juce::String(juce::Decibels::gainToDecibels(peak, -100.0f), 2) + " dBFS，兜底截断 "
                                + juce::String(node.getClampCount()) + " 次" });
    }

    // 3) Saturation: alias image inside the transmitted band.
    {
        SaturationNode node;
        node.prepare(sampleRate, 512);
        node.driveDb.set(24.0f);
        node.mixPct.set(100.0f);
        constexpr int fftOrder = 15, fftSize = 1 << fftOrder;
        const int total = fftSize + 4096;
        std::vector<float> buffer((size_t) total);
        for (int i = 0; i < total; ++i)
            buffer[(size_t) i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 2900.0
                                                          * (double) i / sampleRate);
        for (int pos = 0; pos < total; pos += 512)
            node.process(buffer.data() + pos, juce::jmin(512, total - pos));

        juce::dsp::FFT fft(fftOrder);
        std::vector<float> spectrum((size_t) fftSize * 2, 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f - 0.5f * (float) std::cos(2.0 * juce::MathConstants<double>::pi
                                                            * (double) i / (double) (fftSize - 1));
            spectrum[(size_t) i] = buffer[(size_t) (i + 4096)] * w;
        }
        fft.performFrequencyOnlyForwardTransform(spectrum.data());
        const double binHz = sampleRate / (double) fftSize;
        auto bin = [&](double hz) { return juce::jmax(1, (int) std::lround(hz / binHz)); };
        const float fundamental = spectrum[(size_t) bin(2900.0)];
        float spur = 0.0f;
        for (int k = bin(1000.0); k <= bin(1600.0); ++k) spur = juce::jmax(spur, spectrum[(size_t) k]);
        const float spurDb = juce::Decibels::gainToDecibels(spur / juce::jmax(1e-9f, fundamental), -120.0f);
        results.push_back({ "饱和混叠", spurDb < -55.0f,
                            "带内杂散 " + juce::String(spurDb, 1) + " dBc" });
    }

    // 4) Auto level: two very different inputs must land on the same target.
    {
        AutoLevelNode node;
        node.prepare(sampleRate, 512);
        node.speedMs.set(500.0f);
        auto settle = [&](float amplitude)
        {
            std::vector<float> block(512);
            for (int b = 0; b < 400; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    block[(size_t) i] = amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi
                                                                     * 500.0 * (double) (b * 512 + i) / sampleRate);
                node.process(block.data(), 512);
            }
            double sum = 0.0;
            for (float v : block) sum += (double) v * v;
            return juce::Decibels::gainToDecibels((float) std::sqrt(sum / 512.0), -100.0f);
        };
        const float quiet = settle(0.0447f);
        node.reset();
        const float loud = settle(0.5f);
        results.push_back({ "自动电平", std::abs(quiet + 12.0f) < 2.5f && std::abs(loud + 12.0f) < 2.5f,
                            juce::String(quiet, 1) + " / " + juce::String(loud, 1) + " dBFS（目标 -12）" });
    }

    return results;
}
} // namespace SelfTest
