#pragma once
#include "../DspNode.h"
class HighPassNode : public DspNode
{
public:
    HighPassNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;
    Param cutoffHz { 90.0f, 20.0f, 500.0f };
private:
    void updateCoefficients();
    juce::dsp::IIR::Filter<float> filter;
    double lastCutoff = -1.0;
    float smoothedCutoff = 90.0f;
};
