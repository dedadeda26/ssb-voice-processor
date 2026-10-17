#pragma once
#include "../DspNode.h"
class DeEsserNode : public DspNode
{
public:
    DeEsserNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    int getNumMeters() const override { return 1; }
    float getMeterValue(int) const override { return sibLevel.load(); }

    Param freqHz    { 6000.0f, 4000.0f, 10000.0f };
    Param threshDb  { -25.0f, -48.0f, 0.0f };
    Param amountPct { 50.0f, 0.0f, 100.0f };

private:
    juce::dsp::IIR::Filter<float> highFilter;
    float env = 0.0f;
    double lastFreq = -1.0;
    mutable std::atomic<float> sibLevel {0.0f};
};
