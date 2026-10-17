#pragma once
#include "AudioEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class TestDevice : public juce::AudioIODevice
{
public:
    explicit TestDevice(juce::String type = "Test") : AudioIODevice("Offline test device", type) {}
    double rate = 48000.0;
    int block = 512;
    bool inputEnabled = true, outputEnabled = true;
    juce::StringArray getOutputChannelNames() override { return { "out" }; }
    juce::StringArray getInputChannelNames() override { return { "in" }; }
    juce::Array<double> getAvailableSampleRates() override { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { block }; }
    int getDefaultBufferSize() override { return block; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return juce::BigInteger(outputEnabled ? 1 : 0); }
    juce::BigInteger getActiveInputChannels() const override { return juce::BigInteger(inputEnabled ? 1 : 0); }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

static void process(AudioEngine& engine, const std::vector<float>& input, std::vector<float>& output)
{
    output.resize(input.size());
    const float* in[] = { input.data() };
    float* out[] = { output.data() };
    engine.audioDeviceIOCallbackWithContext(in, 1, out, 1, static_cast<int>(input.size()), {});
}

static void pumpTimer()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
}

