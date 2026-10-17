#pragma once
#include "../DspNode.h"
class NoiseGateNode : public DspNode
{
public:
    NoiseGateNode();
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    int getNumMeters() const override { return 1; }
    float getMeterValue(int) const override { return gateOpen ? 1.0f : 0.0f; }

    Param thresholdDb { -50.0f, -80.0f, -20.0f };
    Param amountPct   { 50.0f, 0.0f, 100.0f };
    Param attackMs    { 5.0f, 0.5f, 100.0f };
    Param releaseMs   { 150.0f, 20.0f, 1000.0f };
    Param holdMs      { 120.0f, 0.0f, 500.0f };

private:
    float envelope = 0.0f;
    float gain = 1.0f;
    int holdRemaining = 0;
    bool openState = true;
    std::atomic<bool> gateOpen { true };
};
