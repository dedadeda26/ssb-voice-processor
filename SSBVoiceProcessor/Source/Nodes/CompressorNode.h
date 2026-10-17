#pragma once
#include "../DspNode.h"
#include "../GainSmoother.h"
class CompressorNode : public DspNode
{
public:
    CompressorNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    int getNumMeters() const override { return 3; }
    float getMeterValue(int i) const override
    {
        if (i == 0) return inLevel.load();
        if (i == 1) return outLevel.load();
        return gainReduction.load();
    }

    Param thresholdDb { -18.0f, -48.0f, 0.0f };
    Param ratio      { 3.0f,  1.0f, 20.0f };
    Param attackMs   { 10.0f, 0.5f, 100.0f };
    Param releaseMs  { 120.0f, 20.0f, 1000.0f };
    Param kneeDb     { 6.0f,  0.0f, 24.0f };
    Param makeupDb   { 0.0f,  0.0f, 24.0f };

private:
    float envelope = 0.0f;
    GainSmoother makeup;
    mutable std::atomic<float> inLevel {0.0f};
    mutable std::atomic<float> outLevel {0.0f};
    mutable std::atomic<float> gainReduction {0.0f}; // 0-1（0=无，1=最大）
};
