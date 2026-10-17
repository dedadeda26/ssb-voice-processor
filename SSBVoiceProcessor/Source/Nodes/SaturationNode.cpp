#include "SaturationNode.h"
#include <algorithm>

SaturationNode::SaturationNode() : DspNode("Saturation") {}

void SaturationNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    drive.prepare(sr); mix.prepare(sr); out.prepare(sr);

    maxChunkSize = juce::jmax(1, bs);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing((size_t) maxChunkSize);

    latencySamples = (int) std::lround(oversampler->getLatencyInSamples());
    dryScratch.assign((size_t) maxChunkSize, 0.0f);
    dryDelay.assign((size_t) juce::jmax(1, latencySamples), 0.0f);
    dryWrite = 0;

    drive.snapTo(juce::Decibels::decibelsToGain(driveDb.get()));
    mix.snapTo(juce::jlimit(0.0f, 1.0f, mixPct.get() * 0.01f));
    out.snapTo(juce::Decibels::decibelsToGain(outputDb.get()));
}

void SaturationNode::reset()
{
    if (oversampler != nullptr) oversampler->reset();
    std::fill(dryScratch.begin(), dryScratch.end(), 0.0f);
    std::fill(dryDelay.begin(), dryDelay.end(), 0.0f);
    dryWrite = 0;
    drive.snapTo(juce::Decibels::decibelsToGain(driveDb.get()));
    mix.snapTo(juce::jlimit(0.0f, 1.0f, mixPct.get() * 0.01f));
    out.snapTo(juce::Decibels::decibelsToGain(outputDb.get()));
}

void SaturationNode::process(float* buffer, int numSamples)
{
    if (oversampler == nullptr || maxChunkSize <= 0)
        prepare(sampleRate, juce::jmax(1, blockSize));

    // The oversampler is sized for the prepared block; measurement and test
    // paths hand us arbitrary lengths, so chunk here.
    for (int pos = 0; pos < numSamples; pos += maxChunkSize)
        processChunk(buffer + pos, juce::jmin(maxChunkSize, numSamples - pos));
}

void SaturationNode::processChunk(float* buffer, int numSamples)
{
    if (numSamples <= 0) return;

    drive.setTarget(juce::Decibels::decibelsToGain(driveDb.get()));
    mix.setTarget(juce::jlimit(0.0f, 1.0f, mixPct.get() * 0.01f));
    out.setTarget(juce::Decibels::decibelsToGain(outputDb.get()));

    // Keep the dry signal before the block is replaced by the decimated wet one.
    std::copy_n(buffer, numSamples, dryScratch.begin());

    juce::dsp::AudioBlock<float> block(&buffer, 1, (size_t) numSamples);
    auto upsampled = oversampler->processSamplesUp(block);
    float* up = upsampled.getChannelPointer(0);
    const int factor = (int) upsampled.getNumSamples() / juce::jmax(1, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = drive.next();
        const float compensation = 1.0f / std::sqrt(juce::jmax(1.0f, d));
        for (int k = 0; k < factor; ++k)
        {
            const int index = i * factor + k;
            up[index] = std::tanh(up[index] * d) * compensation;
        }
    }
    oversampler->processSamplesDown(block);

    const int delaySize = (int) dryDelay.size();
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = dryScratch[(size_t) i];
        const float delayed = dryDelay[(size_t) dryWrite];
        dryDelay[(size_t) dryWrite] = dry;
        if (++dryWrite >= delaySize) dryWrite = 0;

        const float m = mix.next();
        buffer[i] = (delayed + (buffer[i] - delayed) * m) * out.next();
    }
}

std::vector<DspNode::ParamDescriptor> SaturationNode::getParameters()
{
    return {
        { "drive",  "param.drive",  "dB", 0.0f, 24.0f, 6.0f, 1, true,  &driveDb },
        { "mix",    "param.mix",    "%",  0.0f, 100.0f, 100.0f, 0, true, &mixPct },
        { "output", "param.output", "dB", -12.0f, 12.0f, 0.0f, 1, false, &outputDb },
    };
}
