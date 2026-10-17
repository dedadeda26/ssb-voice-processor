#include "EqNode.h"
EqNode::EqNode() : DspNode("EQ") {}
void EqNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };
    limitFrequency(lowFreq, 80.0f, 500.0f);
    limitFrequency(midFreq, 500.0f, 5000.0f);
    limitFrequency(highFreq, 3000.0f, 10000.0f);
    reset();
    updateCoefficients();
    lowFilter.prepare(spec); midFilter.prepare(spec); highFilter.prepare(spec);
}
void EqNode::reset()
{
    lowFilter.reset(); midFilter.reset(); highFilter.reset();
    lastLowFreq = lastMidFreq = lastHighFreq = -1.0;
    lastLowGain = lastMidGain = lastHighGain = -999.0;
    lastMidQ = -1.0;
}
void EqNode::updateCoefficients()
{
    // Smooth every control before it reaches the biquads: dragging a frequency or
    // gain knob would otherwise step all three filters once per block (zipper).
    auto follow = [](float& state, float target, float deadband)
    {
        if (! std::isfinite(state)) state = target;
        state += (target - state) * 0.25f;
        if (std::abs(state - target) < deadband) state = target;
        return state;
    };
    const float maxHz = (float) (sampleRate * 0.45);
    float lf = follow(sLowF, juce::jlimit(1.0f, maxHz, lowFreq.get()), 0.05f);
    float lg = follow(sLowG, juce::jlimit(-12.0f, 12.0f, lowGain.get()), 0.005f);
    float mf = follow(sMidF, juce::jlimit(1.0f, maxHz, midFreq.get()), 0.05f);
    float mg = follow(sMidG, juce::jlimit(-12.0f, 12.0f, midGain.get()), 0.005f);
    float mq = follow(sMidQ, juce::jlimit(0.3f, 3.0f, midQ.get()), 0.001f);
    float hf = follow(sHighF, juce::jlimit(1.0f, maxHz, highFreq.get()), 0.05f);
    float hg = follow(sHighG, juce::jlimit(-12.0f, 12.0f, highGain.get()), 0.005f);
    if (std::abs(lf - lastLowFreq) > 0.5 || std::abs(lg - lastLowGain) > 0.01)
    {
        lastLowFreq = lf; lastLowGain = lg;
        auto c = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf(
            sampleRate, lf, 0.707f, juce::Decibels::decibelsToGain(lg));
        *lowFilter.coefficients = c;
    }
    if (std::abs(mf - lastMidFreq) > 0.5 || std::abs(mg - lastMidGain) > 0.01 || std::abs(mq - lastMidQ) > 0.01)
    {
        lastMidFreq = mf; lastMidGain = mg; lastMidQ = mq;
        auto c = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(
            sampleRate, mf, mq, juce::Decibels::decibelsToGain(mg));
        *midFilter.coefficients = c;
    }
    if (std::abs(hf - lastHighFreq) > 0.5 || std::abs(hg - lastHighGain) > 0.01)
    {
        lastHighFreq = hf; lastHighGain = hg;
        auto c = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf(
            sampleRate, hf, 0.707f, juce::Decibels::decibelsToGain(hg));
        *highFilter.coefficients = c;
    }
}
void EqNode::process(float* buffer, int numSamples)
{
    updateCoefficients();
    juce::dsp::AudioBlock<float> block(&buffer, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    lowFilter.process(ctx); midFilter.process(ctx); highFilter.process(ctx);
}
std::vector<DspNode::ParamDescriptor> EqNode::getParameters()
{
    return {
        { "lowGain",  "param.lowGain",  "dB", -12.0f, 12.0f,    0.0f,    1, true,  &lowGain },
        frequencyParameter("lowFreq", "param.lowFreq", lowFreq, false),
        { "midGain",  "param.midGain",  "dB", -12.0f, 12.0f,    0.0f,    1, true,  &midGain },
        frequencyParameter("midFreq", "param.midFreq", midFreq, false),
        { "midQ",     "param.midQ",     "",    0.3f, 3.0f,      0.7f,    2, false, &midQ },
        { "highGain", "param.highGain", "dB", -12.0f, 12.0f,    0.0f,    1, true,  &highGain },
        frequencyParameter("highFreq", "param.highFreq", highFreq, false),
    };
}
