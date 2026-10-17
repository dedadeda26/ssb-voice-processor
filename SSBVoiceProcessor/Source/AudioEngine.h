#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>
#include <vector>
#include <array>
#include "DspNode.h"
#include "VisualizationData.h"

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int kWaveSize = VisualizationData::waveSize;
    static constexpr int kFFTOrder = 10;
    static constexpr int kFFTSize  = 1 << kFFTOrder;
    static constexpr int kSpecBands = VisualizationData::bands;
    static constexpr int kSpecHistory = VisualizationData::historySize;

    explicit AudioEngine(bool openDevices = true, juce::PropertiesFile* settings = nullptr);
    juce::String applyDeviceSettings(const juce::String& type, const juce::AudioDeviceManager::AudioDeviceSetup& setup);
    juce::String restoreDeviceSettings();
    const juce::String& getDeviceStatus() const { return deviceStatus; }
    unsigned int getConfigurationVersion() const { return configurationVersion.load(); }
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    void setBypass(bool b) { bypass.store(b); }
    bool isBypass() const { return bypass.load(); }
    // Output mute (click-free 8 ms fade).
    void setMuted(bool m) { muted.store(m); }
    bool isMuted() const { return muted.load(); }
    void addNode(std::unique_ptr<DspNode> node);
    DspNode* getNode(int index);
    int getNumNodes() const { return (int) nodes.size(); }

    float getInputLevel()  const { return inLevel.load(); }
    float getInputRms()    const { return inRms.load(); }
    float getOutputLevel() const { return outLevel.load(); }
    float getOutputRms()   const { return outRms.load(); }

    // Display access belongs exclusively to the message thread.
    void consumeVisualization() { visualization.consume(); }
    float getSpectrumBand(int i) const { return visualization.getSpectrumBand(i); }
    int getSpectrumBands() const { return kSpecBands; }
    int getSpectrumHistorySize() const { return kSpecHistory; }
    int getSpectrumWritePos() const { return visualization.getSpectrumWritePos(); }
    float getSpectrumHistory(int time, int band) const { return visualization.getSpectrumHistory(time, band); }
    const std::array<float, kSpecBands>& getSpectrumHistoryRow(int time) const { return visualization.getHistoryRow(time); }
    float getWaveSample(int i) const { return visualization.getWaveSample(i); }
    int getWaveWritePos() const { return visualization.getWaveWritePos(); }
    float getSmoothedWaveSample(int i) const { return visualization.getSmoothedWaveSample(i); }

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels, int numSamples,
        const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String&) override;

    bool startCapture(int targetSamples);
    bool isCaptureReady() const { return captureReady.load(); }
    void stopCapture();
    bool isCapturing() const { return capturing.load(); }
    int getCapturedSamples() const { return captureWritePos.load(); }
    const juce::AudioBuffer<float>& getCaptureBuffer() const { return captureBuffer; }
    std::uint64_t getFFTFrameCount() const { return fftFrames.load(); }
    double getCurrentSampleRate() const { return currentSampleRate.load(); }

    // Call on the message thread right after a large, simultaneous parameter
    // change (preset load, style change, auto-tune, reset). The output ducks to
    // silence and ramps back, so the step cannot be heard as a click.
    void beginSoftTransition(double milliseconds = 25.0);
    bool isTransitioning() const { return fadeRemaining.load(std::memory_order_relaxed) > 0; }

private:
    juce::AudioDeviceManager deviceManager;
    juce::PropertiesFile* deviceSettings = nullptr;
    juce::String deviceStatus;
    std::atomic<unsigned int> configurationVersion { 0 };
    std::vector<std::unique_ptr<DspNode>> nodes;
    std::atomic<bool> bypass { false };
    std::atomic<bool> muted { false };
    float muteGain = 1.0f;
    juce::AudioBuffer<float> monoBuffer;
    juce::AudioBuffer<float> bypassDryBuffer;
    float bypassMix = 1.0f;
    float bypassFadeStep = 1.0f / 384.0f;
    bool bypassFadeInitialised = false;
    std::atomic<int> fadeRemaining { 0 };
    std::atomic<int> fadeTotal { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<bool> captureReady { false };
    int currentBlockSize = 512;

    std::atomic<float> inLevel { 0.0f };
    std::atomic<float> inRms   { 0.0f };
    std::atomic<float> outLevel { 0.0f };
    std::atomic<float> outRms   { 0.0f };

    VisualizationData visualization;

    juce::dsp::FFT fft { kFFTOrder };
    std::array<float, kFFTSize * 2> fftData {};
    std::array<float, kFFTSize / 2> fftMags {};
    std::array<float, kSpecBands> spectrumSmooth {};
    int fftHop = 0;
    std::atomic<std::uint64_t> fftFrames { 0 };
    std::array<float, kFFTSize> fftWindow {};

    juce::AudioBuffer<float> captureBuffer;
    std::atomic<bool> capturing { false };
    std::atomic<int> captureWritePos { 0 };
    int captureTarget = 0;

    void processBlock(const float* input, float* const* outputs, int channels, int outputOffset, int numSamples);
    void pushFFT(const float* data, int numSamples);
};
