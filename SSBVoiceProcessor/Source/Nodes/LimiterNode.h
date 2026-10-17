#pragma once
#include "../DspNode.h"
#include <vector>

// Peak limiter with a 1 ms look-ahead.
//
// The gain is derived from the maximum magnitude inside the look-ahead window,
// so the reduction starts before the peak reaches the output. Without it the
// gain had to collapse on the very sample that needed it, which put a corner in
// the waveform (the "hard" limiter sound). The final clamp stays as a last-resort
// safety net, but a correct look-ahead means it should never have to act.
class LimiterNode : public DspNode
{
public:
    LimiterNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    int getNumMeters() const override { return 1; }
    float getMeterValue(int) const override { return reduction.load(); }

    // Number of samples where the safety clamp had to act (test instrumentation).
    int getClampCount() const { return clampedSamples; }

    Param thresholdDb { -1.0f, -12.0f, 0.0f };
    Param releaseMs   { 50.0f, 5.0f, 500.0f };

private:
    float gain = 1.0f;
    std::vector<float> delayLine, magnitudeWindow;
    int writePos = 0, lookahead = 48;
    int clampedSamples = 0;
    float h1 = 0.0f, h2 = 0.0f, h3 = 0.0f;
    int haveHistory = 0;
    mutable std::atomic<float> reduction {0.0f};
};
