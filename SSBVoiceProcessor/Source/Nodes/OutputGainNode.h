#pragma once
#include "../DspNode.h"
#include "../GainSmoother.h"
class OutputGainNode : public DspNode
{
public:
    OutputGainNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;
    Param gainDb { 0.0f, -24.0f, 12.0f };
private:
    GainSmoother gain;
};
