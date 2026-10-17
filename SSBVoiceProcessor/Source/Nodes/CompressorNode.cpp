#include "CompressorNode.h"
CompressorNode::CompressorNode() : DspNode("Compressor") {}
void CompressorNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    makeup.prepare(sr);
    makeup.snapTo(juce::Decibels::decibelsToGain(makeupDb.get()));
}
void CompressorNode::reset()
{
    envelope = 0.0f;
    inLevel = 0.0f; outLevel = 0.0f; gainReduction = 0.0f;
    makeup.snapTo(juce::Decibels::decibelsToGain(makeupDb.get()));
}
void CompressorNode::process(float* buffer, int numSamples)
{
    float thresh = thresholdDb.get();
    float ratioVal = ratio.get();
    float attMs = attackMs.get();
    float relMs = releaseMs.get();
    float knee = kneeDb.get();
    makeup.setTarget(juce::Decibels::decibelsToGain(makeupDb.get()));
    float attCoef = std::exp(-1.0f / (attMs * 0.001f * (float) sampleRate));
    float relCoef = std::exp(-1.0f / (relMs * 0.001f * (float) sampleRate));

    float peakIn = 0.0f, peakOut = 0.0f, maxGr = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float x = buffer[i];
        peakIn = juce::jmax(peakIn, std::abs(x));
        float absX = std::abs(x);
        if (absX > envelope) envelope = attCoef * envelope + (1.0f - attCoef) * absX;
        else                 envelope = relCoef * envelope + (1.0f - relCoef) * absX;
        float envDb = juce::Decibels::gainToDecibels(envelope, -100.0f);
        float overDb = envDb - thresh;
        float gainDb = 0.0f;
        if (overDb > 0.0f)
        {
            float reduced;
            if (knee > 0.0f && overDb < knee)
            {
                float t = overDb / knee;
                reduced = overDb * (1.0f - 1.0f / ratioVal) * t * t * (3.0f - 2.0f * t);
            }
            else reduced = overDb * (1.0f - 1.0f / ratioVal);
            gainDb = -reduced;
            maxGr = juce::jmax(maxGr, -gainDb / 20.0f);
        }
        buffer[i] = x * juce::Decibels::decibelsToGain(gainDb) * makeup.next();
        peakOut = juce::jmax(peakOut, std::abs(buffer[i]));
    }

    auto smooth = [](float oldV, float newV) {
        return newV > oldV ? newV : oldV * 0.85f + newV * 0.15f;
    };
    inLevel.store(smooth(inLevel.load(), juce::jlimit(0.0f, 1.0f, peakIn)));
    outLevel.store(smooth(outLevel.load(), juce::jlimit(0.0f, 1.0f, peakOut)));
    gainReduction.store(smooth(gainReduction.load(), juce::jlimit(0.0f, 1.0f, maxGr)));
}
std::vector<DspNode::ParamDescriptor> CompressorNode::getParameters()
{
    return {
        { "threshold", "param.threshold", "dB", -48.0f, 0.0f,   -18.0f, 1, false, &thresholdDb },
        { "ratio",     "param.ratio",     "",    1.0f, 20.0f,    3.0f, 1, true,  &ratio },
        { "attack",    "param.attack",    "ms",  0.5f, 100.0f,  10.0f, 1, false, &attackMs },
        { "release",   "param.release",   "ms", 20.0f, 1000.0f, 120.0f, 0, false, &releaseMs },
        { "knee",      "param.knee",      "dB",  0.0f, 24.0f,    6.0f, 1, false, &kneeDb },
        { "makeup",    "param.makeup",    "dB",  0.0f, 24.0f,    0.0f, 1, false, &makeupDb },
    };
}
