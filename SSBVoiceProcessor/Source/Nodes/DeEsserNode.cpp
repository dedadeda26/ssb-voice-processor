#include "DeEsserNode.h"
DeEsserNode::DeEsserNode() : DspNode("De-Esser") {}
void DeEsserNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };
    limitFrequency(freqHz, 4000.0f, 10000.0f);
    lastFreq = freqHz.get();
    *highFilter.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(sampleRate, (float) lastFreq, 0.707f);
    highFilter.prepare(spec);
}
void DeEsserNode::reset() { highFilter.reset(); env = 0.0f; lastFreq = -1.0; sibLevel = 0.0f; }
void DeEsserNode::process(float* buffer, int numSamples)
{
    float f = juce::jlimit(1.0f, (float)(sampleRate * 0.45), freqHz.get());
    if (std::abs(f - lastFreq) > 0.5)
    {
        lastFreq = f;
        auto c = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(sampleRate, f, 0.707f);
        *highFilter.coefficients = c;
    }
    float thresh = threshDb.get();
    float amount = juce::jlimit(0.0f, 1.0f, amountPct.get() * 0.01f);
    float attCoef = std::exp(-1.0f / (2.0f * 0.001f * (float) sampleRate));
    float relCoef = std::exp(-1.0f / (80.0f * 0.001f * (float) sampleRate));

    float peakHi = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float x = buffer[i];
        float hi = highFilter.processSample(x);
        float lo = x - hi;
        float absHi = std::abs(hi);
        peakHi = juce::jmax(peakHi, absHi);
        if (absHi > env) env = attCoef * env + (1.0f - attCoef) * absHi;
        else             env = relCoef * env + (1.0f - relCoef) * absHi;
        float envDb = juce::Decibels::gainToDecibels(env, -100.0f);
        float over = envDb - thresh;
        float hiGain = 1.0f;
        if (over > 0.0f)
        {
            float reducDb = -over * amount;
            hiGain = juce::Decibels::decibelsToGain(reducDb);
        }
        buffer[i] = lo + hi * hiGain;
    }

    float norm = juce::jlimit(0.0f, 1.0f, peakHi);
    auto smooth = [](float o, float n) { return n > o ? n : o * 0.85f + n * 0.15f; };
    sibLevel.store(smooth(sibLevel.load(), norm));
}
std::vector<DspNode::ParamDescriptor> DeEsserNode::getParameters()
{
    return {
        frequencyParameter("deEssFreq", "param.deEssFreq", freqHz, false),
        { "deEssThresh", "param.deEssThresh", "dB", -48.0f, 0.0f, -25.0f, 1, false, &threshDb },
        { "deEssAmount", "param.deEssAmount", "%",  0.0f, 100.0f, 50.0f, 0, true,  &amountPct },
    };
}
