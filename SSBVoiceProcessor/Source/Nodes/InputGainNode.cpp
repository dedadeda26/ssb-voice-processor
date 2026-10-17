#include "InputGainNode.h"
InputGainNode::InputGainNode() : DspNode("Input Gain") {}
void InputGainNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    gain.prepare(sr);
    gain.snapTo(juce::Decibels::decibelsToGain(gainDb.get()));
}
void InputGainNode::reset() { gain.snapTo(juce::Decibels::decibelsToGain(gainDb.get())); }
void InputGainNode::process(float* buffer, int numSamples)
{
    gain.setTarget(juce::Decibels::decibelsToGain(gainDb.get()));
    gain.applyGain(buffer, numSamples);
}
std::vector<DspNode::ParamDescriptor> InputGainNode::getParameters()
{
    return { { "gain", "param.gain", "dB", -24.0f, 24.0f, 0.0f, 1, true, &gainDb } };
}
