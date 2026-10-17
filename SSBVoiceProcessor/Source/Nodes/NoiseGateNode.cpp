#include "NoiseGateNode.h"
NoiseGateNode::NoiseGateNode() : DspNode("Noise Gate") {}
void NoiseGateNode::reset() { envelope = 0.0f; gain = 1.0f; holdRemaining = 0; openState = true; gateOpen = true; }
void NoiseGateNode::process(float* buffer, int numSamples)
{
    float thresh = thresholdDb.get();
    float amount = juce::jlimit(0.0f, 1.0f, amountPct.get() * 0.01f);
    float attMs = attackMs.get();
    float relMs = releaseMs.get();
    float attCoef = std::exp(-1.0f / (attMs * 0.001f * (float) sampleRate));
    float relCoef = std::exp(-1.0f / (relMs * 0.001f * (float) sampleRate));

    // Hold + hysteresis: without them the gate chatters around the threshold,
    // which on speech sounds like a pump on every syllable tail.
    constexpr float hysteresisDb = 4.0f;
    const int holdSamples = (int) ((float) sampleRate * (float) juce::jlimit(0.0f, 500.0f, holdMs.get()) * 0.001f);

    bool lastOpen = openState;
    for (int i = 0; i < numSamples; ++i)
    {
        float x = buffer[i];
        float absX = std::abs(x);
        if (absX > envelope) envelope = attCoef * envelope + (1.0f - attCoef) * absX;
        else                 envelope = relCoef * envelope + (1.0f - relCoef) * absX;
        float envDb = juce::Decibels::gainToDecibels(envelope, -100.0f);

        if (envDb > thresh)
        {
            openState = true;
            holdRemaining = holdSamples;
        }
        else if (envDb < thresh - hysteresisDb)
        {
            if (holdRemaining > 0) --holdRemaining;
            else                   openState = false;
        }
        // Inside the hysteresis band the state is kept as-is.

        float targetGain = 1.0f;
        if (! openState)
        {
            float below = thresh - envDb;
            float reducDb = -below * amount;
            targetGain = juce::Decibels::decibelsToGain(reducDb);
        }
        // Opening the gate increases gain: use attack. Close it with release.
        if (targetGain > gain) gain = attCoef * gain + (1.0f - attCoef) * targetGain;
        else                   gain = relCoef * gain + (1.0f - relCoef) * targetGain;
        buffer[i] = x * gain;
        lastOpen = openState;
    }
    gateOpen = lastOpen;
}
std::vector<DspNode::ParamDescriptor> NoiseGateNode::getParameters()
{
    return {
        { "gateThreshold", "param.gateThresh",  "dB", -80.0f, -20.0f, -50.0f, 0, false, &thresholdDb },
        { "gateAmount",    "param.gateAmount",  "%",   0.0f, 100.0f,  50.0f, 0, true,  &amountPct },
        { "gateAttack",    "param.gateAttack",  "ms",  0.5f, 100.0f,   5.0f, 1, false, &attackMs },
        { "gateRelease",   "param.gateRelease", "ms",  20.0f, 1000.0f, 150.0f, 0, false, &releaseMs },
        { "gateHold",      "param.gateHold",    "ms",  0.0f, 500.0f,  120.0f, 0, false, &holdMs },
    };
}
