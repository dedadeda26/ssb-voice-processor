#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include "Param.h"

class DspNode
{
public:
    struct ParamDescriptor
    {
        juce::String id;
        juce::String nameKey;
        juce::String unit;
        float minValue;
        float maxValue;
        float defaultValue;
        int numDecimals;
        bool showInSimple;
        Param* param;
    };

    virtual ~DspNode() = default;

    virtual void prepare(double newSampleRate, int newBlockSize)
    {
        sampleRate = newSampleRate;
        blockSize = newBlockSize;
        dryBuffer.setSize(1, juce::jmax(1, newBlockSize), false, true, true);
        fadeStep = 1.0f / (float) juce::jmax(1.0, newSampleRate * 0.008);
        bypassFadeInitialised = false;
    }

    virtual void reset() = 0;

    // Always processes. Bypass handling moved to render(), so that a bypass
    // transition can still run the node while the crossfade is in progress.
    virtual void process(float* buffer, int numSamples) = 0;

    // Audio-thread entry point used by AudioEngine.
    //
    // Steady state: bypassed nodes are skipped entirely, active nodes run
    // unchanged. While a bypass toggle is fading, both are mixed per sample so
    // the change is inaudible instead of a hard waveform step.
    void render(float* buffer, int numSamples) noexcept
    {
        if (numSamples <= 0) return;

        const bool wantActive = ! bypassed.load();
        if (! bypassFadeInitialised)
        {
            bypassFade = wantActive ? 1.0f : 0.0f;
            bypassFadeInitialised = true;
        }

        if (wantActive && bypassFade >= 1.0f)
        {
            process(buffer, numSamples);
            return;
        }
        if (! wantActive && bypassFade <= 0.0f)
            return;

        if (dryBuffer.getNumSamples() < numSamples)
            dryBuffer.setSize(1, numSamples, false, false, true); // guard, not the normal path
        dryBuffer.copyFrom(0, 0, buffer, numSamples);

        process(buffer, numSamples);

        const float* dry = dryBuffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            bypassFade = juce::jlimit(0.0f, 1.0f, bypassFade + (wantActive ? fadeStep : -fadeStep));
            buffer[i] = dry[i] + (buffer[i] - dry[i]) * bypassFade;
        }
    }

    void setBypassed(bool b) { bypassed.store(b); }
    bool isBypassed() const { return bypassed.load(); }

    const juce::String& getName() const { return name; }
    virtual std::vector<ParamDescriptor> getParameters() { return {}; }

    // 可视化指标，0-1 归一化
    virtual int getNumMeters() const { return 0; }
    virtual float getMeterValue(int) const { return 0.0f; }
    virtual juce::String getMeterLabel(int) const { return {}; }

protected:
    void limitFrequency(Param& param, float nominalMin, float nominalMax)
    {
        const float maximum = juce::jmin(nominalMax, static_cast<float>(sampleRate * 0.45));
        param.setLimits(juce::jmin(nominalMin, maximum * 0.5f), maximum);
    }
    static ParamDescriptor frequencyParameter(const juce::String& id, const juce::String& key,
                                               Param& param, bool simple)
    {
        const auto r = param.getRange();
        return { id, key, "Hz", r.minimum, r.maximum,
                 juce::jlimit(r.minimum, r.maximum, param.getDefault()), 0, simple, &param };
    }
    explicit DspNode(const juce::String& n) : name(n) {}
    juce::String name;
    std::atomic<bool> bypassed { false };
    double sampleRate = 48000.0;
    int blockSize = 512;

private:
    juce::AudioBuffer<float> dryBuffer;
    float bypassFade = 1.0f;
    float fadeStep = 1.0f / 384.0f;
    bool bypassFadeInitialised = false;
};
