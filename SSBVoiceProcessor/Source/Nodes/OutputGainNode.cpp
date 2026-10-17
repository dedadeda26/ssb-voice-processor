#include "OutputGainNode.h"
OutputGainNode::OutputGainNode() : DspNode("Output Gain") {}
void OutputGainNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    gain.prepare(sr);
    gain.snapTo(juce::Decibels::decibelsToGain(gainDb.get()));
}
void OutputGainNode::reset() { gain.snapTo(juce::Decibels::decibelsToGain(gainDb.get())); }
void OutputGainNode::process(float* buffer, int numSamples)
{
    gain.setTarget(juce::Decibels::decibelsToGain(gainDb.get()));
    gain.applyGain(buffer, numSamples);
}
std::vector<DspNode::ParamDescriptor> OutputGainNode::getParameters()
{
    return { { "outGain", "param.outGain", "dB", -24.0f, 12.0f, 0.0f, 1, true, &gainDb } };
}
