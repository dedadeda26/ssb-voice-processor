#pragma once
#include "../DspNode.h"
#include "../GainSmoother.h"

// Slow RMS leveler ("AGC") for the TX level.
//
// The compressor and the limiter only handle fast material; the level that
// actually reaches the radio still drifts with mic distance and voice effort.
// This node measures the block RMS and trims the gain with a 1-3 s time constant
// so the average level stays on target without audibly pumping.
class AutoLevelNode : public DspNode
{
public:
    AutoLevelNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    float getCurrentGainDb() const { return (float) smoothedGainDb; }
    float getMeasuredRmsDb() const { return measuredRmsDb; }

    Param targetDb  { -12.0f, -30.0f, -6.0f };
    Param speedMs   { 1500.0f, 300.0f, 5000.0f };
    Param maxGainDb { 18.0f, 6.0f, 24.0f };

private:
    double smoothedGainDb = 0.0;
    float measuredRmsDb = -100.0f;
    GainSmoother gain;
};
