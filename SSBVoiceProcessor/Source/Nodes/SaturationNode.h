#pragma once
#include "../DspNode.h"
#include "../GainSmoother.h"
#include <memory>
#include <vector>

// 4x oversampled saturation.
//
// tanh evaluated at the device rate folds its high-order harmonics back into the
// transmitted band; with drive at the top of its range the in-band alias spur
// measured about -35 dBc. Running the nonlinearity at 4x with JUCE's polyphase
// half-band filters, then decimating, moves most of that energy out of band. The
// dry path is delayed by the oversampler latency so the mix control stays
// phase-correct.
class SaturationNode : public DspNode
{
public:
    SaturationNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    int getLatencySamples() const { return latencySamples; }

    Param driveDb  { 6.0f, 0.0f, 24.0f };
    Param mixPct   { 100.0f, 0.0f, 100.0f };
    Param outputDb { 0.0f, -12.0f, 12.0f };

private:
    void processChunk(float* buffer, int numSamples);

    GainSmoother drive, mix, out;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<float> dryScratch, dryDelay;
    int latencySamples = 0;
    int dryWrite = 0;
    int maxChunkSize = 0;
};
