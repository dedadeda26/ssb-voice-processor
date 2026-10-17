#pragma once
#include "../DspNode.h"

// 4th-order (24 dB/oct) Butterworth band limit built from two biquads per side.
// The cutoff defaults and adjustable ranges are unchanged from the tuned 2nd
// order version; only the stopband slope is steeper, which is what keeps
// saturation products out of the next channel.
class BandLimitNode : public DspNode
{
public:
    BandLimitNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    Param lowHz  { 90.0f,  60.0f, 500.0f };
    Param highHz { 3100.0f, 2000.0f, 4000.0f };

private:
    // Butterworth 4th order = two 2nd-order sections.
    static constexpr float qLow = 0.541196100146197f;
    static constexpr float qHigh = 1.306562964876377f;
    juce::dsp::IIR::Filter<float> hp1, hp2, lp1, lp2;
    double lastLow = -1.0, lastHigh = -1.0;
    float smoothedLow = 90.0f, smoothedHigh = 3100.0f;
    void updateCoefficientsIfNeeded();
};
