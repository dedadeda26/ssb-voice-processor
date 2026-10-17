#include "AutoLevelNode.h"
#include <cmath>

AutoLevelNode::AutoLevelNode() : DspNode("Auto Level") {}

void AutoLevelNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    gain.prepare(sr, 0.05);   // 50 ms ramp between block updates: no zipper
    smoothedGainDb = 0.0;
    measuredRmsDb = -100.0f;
    gain.snapTo(1.0f);
}

void AutoLevelNode::reset()
{
    smoothedGainDb = 0.0;
    measuredRmsDb = -100.0f;
    gain.snapTo(1.0f);
}

void AutoLevelNode::process(float* buffer, int numSamples)
{
    if (numSamples <= 0) return;

    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i) sum += (double) buffer[i] * buffer[i];
    const float rms = (float) std::sqrt(sum / (double) numSamples);
    measuredRmsDb = juce::Decibels::gainToDecibels(rms, -100.0f);

    // Do not "boost" silence: hold the gain when there is nothing to measure.
    if (measuredRmsDb > -55.0f)
    {
        const float limit = juce::jlimit(6.0f, 24.0f, maxGainDb.get());
        const float desired = juce::jlimit(-limit, limit, targetDb.get() - measuredRmsDb);
        const double blockSeconds = (double) numSamples / juce::jmax(1.0, sampleRate);
        const double speedSeconds = (double) juce::jlimit(300.0f, 5000.0f, speedMs.get()) / 1000.0;
        const double coefficient = 1.0 - std::exp(-blockSeconds / juce::jmax(0.001, speedSeconds));
        smoothedGainDb += ((double) desired - smoothedGainDb) * coefficient;
    }

    gain.setTarget(juce::Decibels::decibelsToGain((float) smoothedGainDb));
    gain.applyGain(buffer, numSamples);
}

std::vector<DspNode::ParamDescriptor> AutoLevelNode::getParameters()
{
    return {
        { "agcTarget", "param.agcTarget", "dB", -30.0f, -6.0f, -12.0f, 1, true,  &targetDb },
        { "agcSpeed",  "param.agcSpeed",  "ms", 300.0f, 5000.0f, 1500.0f, 0, false, &speedMs },
        { "agcMax",    "param.agcMax",    "dB", 6.0f, 24.0f, 18.0f, 0, false, &maxGainDb },
    };
}
