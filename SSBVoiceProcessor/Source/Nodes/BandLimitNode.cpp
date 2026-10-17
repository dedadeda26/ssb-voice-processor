#include "BandLimitNode.h"

BandLimitNode::BandLimitNode() : DspNode("Band-Limit") {}

void BandLimitNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };
    limitFrequency(lowHz, 60.0f, 500.0f);
    limitFrequency(highHz, 2000.0f, 4000.0f);

    // 强制重算一次
    lastLow = -1.0;
    lastHigh = -1.0;
    smoothedLow = lowHz.get();
    smoothedHigh = highHz.get();
    updateCoefficientsIfNeeded();
    hp1.prepare(spec); hp2.prepare(spec);
    lp1.prepare(spec); lp2.prepare(spec);
}

void BandLimitNode::reset()
{
    hp1.reset(); hp2.reset();
    lp1.reset(); lp2.reset();
}

void BandLimitNode::updateCoefficientsIfNeeded()
{
    double hiLimit = sampleRate * 0.45;
    // Smooth the cutoffs so dragging the band edges does not step the biquads.
    const float targetLow  = juce::jlimit(60.0f, (float) hiLimit, lowHz.get());
    const float targetHigh = juce::jlimit(60.0f, (float) hiLimit, highHz.get());
    if (! std::isfinite(smoothedLow))  smoothedLow = targetLow;
    if (! std::isfinite(smoothedHigh)) smoothedHigh = targetHigh;
    smoothedLow  += (targetLow - smoothedLow) * 0.25f;
    smoothedHigh += (targetHigh - smoothedHigh) * 0.25f;
    if (std::abs(smoothedLow - targetLow) < 0.05f) smoothedLow = targetLow;
    if (std::abs(smoothedHigh - targetHigh) < 0.05f) smoothedHigh = targetHigh;

    double lo = (double) juce::jlimit(60.0f, (float) hiLimit, smoothedLow);
    double hi = (double) juce::jlimit(1.0f, (float) hiLimit, smoothedHigh);

    // 只有参数变了才重新计算系数（避免音频线程反复分配）
    if (std::abs(lo - lastLow) > 0.5)
    {
        lastLow = lo;
        const auto c1 = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(
            sampleRate, (float) lo, qLow);
        const auto c2 = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(
            sampleRate, (float) lo, qHigh);
        *hp1.coefficients = c1;
        *hp2.coefficients = c2;
    }
    if (std::abs(hi - lastHigh) > 0.5)
    {
        lastHigh = hi;
        const auto c1 = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(
            sampleRate, (float) hi, qLow);
        const auto c2 = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(
            sampleRate, (float) hi, qHigh);
        *lp1.coefficients = c1;
        *lp2.coefficients = c2;
    }
}

void BandLimitNode::process(float* buffer, int numSamples)
{
    updateCoefficientsIfNeeded();

    juce::dsp::AudioBlock<float> block(&buffer, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    hp1.process(ctx);
    hp2.process(ctx);
    lp1.process(ctx);
    lp2.process(ctx);
}

std::vector<DspNode::ParamDescriptor> BandLimitNode::getParameters()
{
    return {
        frequencyParameter("bandLow", "param.bandLow", lowHz, true),
        frequencyParameter("bandHigh", "param.bandHigh", highHz, true),
    };
}
