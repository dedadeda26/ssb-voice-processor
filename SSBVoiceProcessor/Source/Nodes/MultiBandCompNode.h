#pragma once
#include "../DspNode.h"
#include "../GainSmoother.h"
#include <array>

class MultiBandCompNode : public DspNode
{
public:
    MultiBandCompNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    int getNumMeters() const override { return 9; }
    float getMeterValue(int i) const override
    {
        if (i < 0 || i >= 9) return 0.0f;
        return bandMeters[(size_t) i].load();
    }

    Param crossLowHz  { 300.0f,  100.0f, 800.0f };
    Param crossHighHz { 3000.0f, 1500.0f, 6000.0f };
    Param lowAmount   { 40.0f, 0.0f, 100.0f };
    Param midAmount   { 40.0f, 0.0f, 100.0f };
    Param highAmount  { 30.0f, 0.0f, 100.0f };
    Param attackMs    { 15.0f, 0.5f, 100.0f };
    Param releaseMs   { 150.0f, 20.0f, 1000.0f };
    Param threshDb    { -22.0f, -45.0f, -6.0f };
    Param makeupDb    { 0.0f, 0.0f, 12.0f };

    // Level the three bands actually removed in the last block (dB, <= 0).
    float getBandGrDb(int band) const
    {
        return band >= 0 && band < 3 ? bandGrDb[(size_t) band] : 0.0f;
    }

private:
    // 低频分频：两个独立实例，各自固定类型，不再在 process 里 setType
    juce::dsp::LinkwitzRileyFilter<float> lowBandFilter;   // 固定 lowpass
    juce::dsp::LinkwitzRileyFilter<float> restBandFilter;  // 固定 highpass

    // Use matching LR4 splits. The low branch needs the same all-pass phase
    // response as the sum of the second split to preserve a flat recombination.
    juce::dsp::LinkwitzRileyFilter<float> midLowpass;
    juce::dsp::LinkwitzRileyFilter<float> highHighpass;
    juce::dsp::LinkwitzRileyFilter<float> lowPhaseCompensation;

    juce::AudioBuffer<float> lowBuf, midBuf, highBuf, tmpBuf;
    float lowEnv = 0.0f, midEnv = 0.0f, highEnv = 0.0f;
    double lastCrossLow = -1.0, lastCrossHigh = -1.0;
    GainSmoother makeup;
    std::array<float, 3> bandGrDb { 0.0f, 0.0f, 0.0f };
    std::array<double, 3> bandInPower { 0.0, 0.0, 0.0 };
    std::array<double, 3> bandOutPower { 0.0, 0.0, 0.0 };
    float autoMakeupDb = 0.0f;

    std::array<std::atomic<float>, 9> bandMeters {};

    void updateSplitters();
    void processBand(float* buf, int n, float amount, float threshold, float& env,
                     int meterBase, float& grDb);
    void updateBandMeters(int meterBase, float peakIn, float peakOut, float gr);
};
