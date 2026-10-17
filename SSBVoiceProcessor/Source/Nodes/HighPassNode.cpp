#include "HighPassNode.h"
HighPassNode::HighPassNode() : DspNode("High-Pass") {}
void HighPassNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };
    limitFrequency(cutoffHz, 20.0f, 500.0f);
    lastCutoff = -1.0;
    smoothedCutoff = cutoffHz.get();
    updateCoefficients();
    filter.prepare(spec);
}
void HighPassNode::reset() { filter.reset(); lastCutoff = -1.0; }
void HighPassNode::updateCoefficients()
{
    // Sweep the cutoff smoothly instead of stepping the biquad once per block:
    // a fast knob drag would otherwise zipper.
    const double target = juce::jlimit(20.0, sampleRate * 0.45, (double) cutoffHz.get());
    if (! std::isfinite(smoothedCutoff)) smoothedCutoff = (float) target;
    smoothedCutoff += ((float) target - smoothedCutoff) * 0.25f;
    if (std::abs((double) smoothedCutoff - target) < 0.05) smoothedCutoff = (float) target;

    double c = juce::jlimit(20.0, sampleRate * 0.45, (double) smoothedCutoff);
    if (std::abs(c - lastCutoff) < 0.01) return;
    lastCutoff = c;
    auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(sampleRate, (float) c, 0.707f);
    *filter.coefficients = coeffs;
}
void HighPassNode::process(float* buffer, int numSamples)
{
    updateCoefficients();
    juce::dsp::AudioBlock<float> block(&buffer, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    filter.process(ctx);
}
std::vector<DspNode::ParamDescriptor> HighPassNode::getParameters()
{
    return { frequencyParameter("cutoff", "param.cutoff", cutoffHz, true) };
}
