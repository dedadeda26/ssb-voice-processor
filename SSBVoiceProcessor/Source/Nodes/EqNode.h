#pragma once
#include "../DspNode.h"
class EqNode : public DspNode
{
public:
    EqNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;
    Param lowFreq  { 200.0f, 80.0f, 500.0f };
    Param lowGain  { 0.0f, -12.0f, 12.0f };
    Param midFreq  { 1500.0f, 500.0f, 5000.0f };
    Param midGain  { 0.0f, -12.0f, 12.0f };
    Param midQ     { 0.7f, 0.3f, 3.0f };
    Param highFreq { 5000.0f, 3000.0f, 10000.0f };
    Param highGain { 0.0f, -12.0f, 12.0f };
private:
    juce::dsp::IIR::Filter<float> lowFilter, midFilter, highFilter;
    double lastLowFreq = -1.0, lastLowGain = -999.0;
    double lastMidFreq = -1.0, lastMidGain = -999.0, lastMidQ = -1.0;
    double lastHighFreq = -1.0, lastHighGain = -999.0;
    float sLowF = 200.0f, sLowG = 0.0f, sMidF = 1500.0f, sMidG = 0.0f;
    float sMidQ = 0.7f, sHighF = 5000.0f, sHighG = 0.0f;
    void updateCoefficients();
};
