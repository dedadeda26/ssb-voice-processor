#include "AudioEngine.h"
#include "Nodes/InputGainNode.h"
#include "Nodes/HighPassNode.h"
#include "Nodes/NoiseGateNode.h"
#include "Nodes/SpectralNrNode.h"
#include "Nodes/EqNode.h"
#include "Nodes/MultiBandCompNode.h"
#include "Nodes/CompressorNode.h"
#include "Nodes/SaturationNode.h"
#include "Nodes/DeEsserNode.h"
#include "Nodes/LimiterNode.h"
#include "Nodes/AutoLevelNode.h"
#include "Nodes/BandLimitNode.h"
#include "Nodes/OutputGainNode.h"
#include <cmath>
#include "AppSettings.h"

AudioEngine::AudioEngine(bool openDevices, juce::PropertiesFile* settings)
    : deviceSettings(settings ? settings : (openDevices ? AppSettings::get() : nullptr))
{
    addNode(std::make_unique<InputGainNode>());
    addNode(std::make_unique<HighPassNode>());
    addNode(std::make_unique<NoiseGateNode>());
    // Denoise right after the gate so everything downstream sees a quieter signal.
    addNode(std::make_unique<SpectralNrNode>());
    addNode(std::make_unique<EqNode>());
    addNode(std::make_unique<MultiBandCompNode>());
    addNode(std::make_unique<CompressorNode>());
    addNode(std::make_unique<SaturationNode>());
    addNode(std::make_unique<DeEsserNode>());
    addNode(std::make_unique<BandLimitNode>());
    // Slow leveler keeps the TX level steady and feeds the limiter.
    addNode(std::make_unique<AutoLevelNode>());
    addNode(std::make_unique<LimiterNode>());
    // Output gain is the load-matching trim for the radio, so it must be last.
    addNode(std::make_unique<OutputGainNode>());

    for (int i = 0; i < kFFTSize; ++i)
        fftWindow[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float)i / (float)(kFFTSize - 1));
    fftData.fill(0.0f);
    fftMags.fill(0.0f);
    spectrumSmooth.fill(0.0f);
    captureBuffer.setSize(1, 48000 * 10, false, true, true);

    if (!openDevices) return; // Offline processing and deterministic tests.

    restoreDeviceSettings();
    deviceManager.addAudioCallback(this);
}
juce::String AudioEngine::restoreDeviceSettings()
{
    auto saved = deviceSettings ? deviceSettings->getXmlValue("audioDeviceState") : nullptr;
    auto error = saved ? deviceManager.initialise(1, 2, saved.get(), false)
                       : deviceManager.initialiseWithDefaultDevices(1, 2);
    if (error.isNotEmpty() && saved)
    {
        const auto fallback = deviceManager.initialiseWithDefaultDevices(1, 2);
        deviceStatus = juce::String::fromUTF8(u8"无法恢复保存的设备，已尝试默认设备：") + error;
        if (fallback.isNotEmpty()) deviceStatus += " / " + fallback;
        return deviceStatus;
    }
    deviceStatus = error;
    return error;
}

juce::String AudioEngine::applyDeviceSettings(const juce::String& typeName,
                                             const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    if (setup.inputDeviceName.isEmpty() || setup.outputDeviceName.isEmpty()
        || !std::isfinite(setup.sampleRate) || setup.sampleRate < 0 || setup.bufferSize < 0)
        return deviceStatus = "Select valid input and output devices and sample rate/buffer size";
    bool knownType = false;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type->getTypeName() == typeName) knownType = true;
    if (!knownType) return deviceStatus = "Unknown audio device type";
    auto previous = deviceManager.createStateXml();
    const auto oldType = deviceManager.getCurrentAudioDeviceType();
    const auto oldSetup = deviceManager.getAudioDeviceSetup();
    const auto stored = deviceSettings ? deviceSettings->getValue("audioDeviceState") : juce::String();
    if (oldType != typeName) deviceManager.setCurrentAudioDeviceType(typeName, true);
    auto error = deviceManager.getCurrentAudioDeviceType() == typeName
                     ? deviceManager.setAudioDeviceSetup(setup, true) : juce::String("Could not switch audio device type");
    auto* device = deviceManager.getCurrentAudioDevice();
    if (error.isEmpty() && (!device || !device->isOpen())) error = "Audio device did not open";
    if (error.isEmpty() && deviceSettings)
    {
        // createStateXml() returns null when the device manager has no *explicit*
        // settings yet (the setup matched the already-open default device). That
        // is not a failure: there is simply nothing new to persist. Treating it as
        // one used to roll the (perfectly good) device back.
        if (const auto state = deviceManager.createStateXml())
        {
            deviceSettings->setValue("audioDeviceState", state.get());
            if (!deviceSettings->save())
            {
                deviceSettings->setValue("audioDeviceState", stored);
                error = "Could not save audio device settings";
            }
        }
    }
    if (error.isNotEmpty())
    {
        juce::String rollback;
        if (previous) rollback = deviceManager.initialise(1, 2, previous.get(), false);
        else if (oldSetup.inputDeviceName.isNotEmpty() || oldSetup.outputDeviceName.isNotEmpty())
        {
            deviceManager.setCurrentAudioDeviceType(oldType, false);
            rollback = deviceManager.setAudioDeviceSetup(oldSetup, false);
        }
        else deviceManager.closeAudioDevice();
        if (rollback.isNotEmpty()) error += " / Restore failed: " + rollback;
    }
    deviceStatus = error;
    return error;
}

AudioEngine::~AudioEngine() { deviceManager.removeAudioCallback(this); }
void AudioEngine::addNode(std::unique_ptr<DspNode> node) { nodes.push_back(std::move(node)); }
DspNode* AudioEngine::getNode(int index)
{
    if (index < 0 || index >= (int) nodes.size()) return nullptr;
    return nodes[(size_t) index].get();
}
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    currentSampleRate = device->getCurrentSampleRate();
    currentBlockSize  = juce::jmax(1, device->getCurrentBufferSizeSamples());
    if (!std::isfinite(currentSampleRate.load()) || currentSampleRate.load() <= 0.0) { audioDeviceError("Invalid sample rate"); return; }
    monoBuffer.setSize(1, currentBlockSize, false, true, true);
    bypassDryBuffer.setSize(1, currentBlockSize, false, true, true);
    bypassFadeStep = 1.0f / (float) juce::jmax(1.0, currentSampleRate.load() * 0.008);
    bypassFadeInitialised = false;
    bypassMix = bypass.load() ? 0.0f : 1.0f;
    fadeRemaining.store(0);
    fftHop = 0;
    fftFrames.store(0);
    fftData.fill(0.0f);
    fft.performFrequencyOnlyForwardTransform(fftData.data());
    fftData.fill(0.0f);
    for (auto& n : nodes) { n->reset(); n->prepare(currentSampleRate.load(), currentBlockSize); }
    configurationVersion.fetch_add(1);
    captureReady.store(device->getActiveInputChannels().countNumberOfSetBits() > 0
                       && device->getActiveOutputChannels().countNumberOfSetBits() > 0
                       && currentSampleRate.load() > 0.0);
}
void AudioEngine::beginSoftTransition(double milliseconds)
{
    const int total = juce::jmax(1, (int) std::lround(currentSampleRate.load() * milliseconds * 0.001));
    fadeTotal.store(total, std::memory_order_relaxed);
    fadeRemaining.store(total, std::memory_order_release);
}

void AudioEngine::audioDeviceStopped()
{
    captureReady.store(false);
    capturing.store(false);
    // Do not leave the meters frozen on the last peak: it looks like live audio.
    inLevel.store(0.0f); inRms.store(0.0f);
    outLevel.store(0.0f); outRms.store(0.0f);
    for (auto& n : nodes) n->reset();
}

void AudioEngine::audioDeviceError(const juce::String&)
{
    captureReady.store(false);
    capturing.store(false);
    inLevel.store(0.0f); inRms.store(0.0f);
    outLevel.store(0.0f); outRms.store(0.0f);
}

bool AudioEngine::startCapture(int targetSamples)
{
    // The manager holds this lock during callbacks. Only the control thread
    // acquires it here, so resize/clear cannot race an in-flight capture copy.
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    if (targetSamples <= 0 || !captureReady.load() || capturing.load()) return false;
    if (captureBuffer.getNumSamples() < targetSamples)
        captureBuffer.setSize(1, targetSamples, false, true, true);
    captureBuffer.clear();
    captureTarget = targetSamples;
    captureWritePos.store(0);
    capturing.store(true);
    return true;
}

void AudioEngine::stopCapture()
{
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    capturing.store(false);
}

void AudioEngine::pushFFT(const float* data, int numSamples)
{
    while (numSamples > 0)
    {
        const int count = juce::jmin(numSamples, kFFTSize - fftHop);
        std::copy_n(data, count, fftData.begin() + fftHop);
        data += count;
        numSamples -= count;
        fftHop += count;
        if (fftHop < kFFTSize) continue;

        for (int i = 0; i < kFFTSize; ++i)
            fftData[(size_t) i] *= fftWindow[(size_t) i];
        for (int i = kFFTSize; i < kFFTSize * 2; ++i) fftData[(size_t) i] = 0.0f;

        fft.performFrequencyOnlyForwardTransform(fftData.data());

        int half = kFFTSize / 2;
        for (int i = 0; i < half; ++i)
        {
            float mag = fftData[(size_t) i] / (float) kFFTSize;
            float db = juce::Decibels::gainToDecibels(mag, -90.0f);
            fftMags[(size_t) i] = juce::jlimit(0.0f, 1.0f, (db + 90.0f) / 90.0f);
        }

        for (int b = 0; b < kSpecBands; ++b)
        {
            float f1 = 40.0f * std::pow(20000.0f / 40.0f, (float) b / kSpecBands);
            float f2 = 40.0f * std::pow(20000.0f / 40.0f, (float)(b + 1) / kSpecBands);
            if (f1 >= currentSampleRate.load() * 0.5)
            {
                spectrumSmooth[(size_t)b] = 0.0f;
                continue;
            }
            int i1 = juce::jlimit(0, half - 1, (int)(f1 * kFFTSize / currentSampleRate));
            int i2 = juce::jlimit(i1 + 1, half, (int)(f2 * kFFTSize / currentSampleRate));
            float sum = 0.0f;
            for (int i = i1; i < i2; ++i) sum += fftMags[(size_t) i];
            float avg = sum / (float)(i2 - i1);
            float prev = spectrumSmooth[(size_t) b];
            spectrumSmooth[(size_t) b] = avg > prev ? avg : prev * 0.85f + avg * 0.15f;
        }

        visualization.pushSpectrum(spectrumSmooth);
        fftFrames.fetch_add(1, std::memory_order_relaxed);

        fftHop = 0;
        std::fill(fftData.begin(), fftData.end(), 0.0f);
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);

    if (numSamples <= 0 || numInputChannels == 0 || numOutputChannels == 0) return;
    if (inputChannelData == nullptr || inputChannelData[0] == nullptr) return;

    const int capacity = monoBuffer.getNumSamples();
    if (capacity <= 0) return;
    for (int offset = 0; offset < numSamples; offset += capacity)
        processBlock(inputChannelData[0] + offset, outputChannelData, numOutputChannels,
                     offset, juce::jmin(capacity, numSamples - offset));
}

void AudioEngine::processBlock(const float* input, float* const* outputs, int channels,
                               int outputOffset, int numSamples)
{
    {
        float peak = 0.0f, sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float v = input[i];
            peak = juce::jmax(peak, std::abs(v));
            sum += v * v;
        }
        float rms = std::sqrt(sum / (float) numSamples);
        float prev = inLevel.load();
        inLevel.store(juce::jmax(peak, prev * 0.88f));
        prev = inRms.load();
        inRms.store(juce::jmax(rms, prev * 0.92f));
    }

    if (capturing.load())
    {
        int pos = captureWritePos.load();
        int remaining = captureTarget - pos;
        if (remaining > 0)
        {
            int toCopy = juce::jmin(numSamples, remaining);
            captureBuffer.copyFrom(0, pos, input, toCopy);
            int newPos = pos + toCopy;
            captureWritePos.store(newPos);
            if (newPos >= captureTarget) capturing.store(false);
        }
        else capturing.store(false);
    }

    float* work = monoBuffer.getWritePointer(0);
    juce::FloatVectorOperations::copy(work, input, numSamples);

    const bool wantProcess = ! bypass.load();
    if (! bypassFadeInitialised)
    {
        bypassMix = wantProcess ? 1.0f : 0.0f;
        bypassFadeInitialised = true;
    }

    if (wantProcess && bypassMix >= 1.0f)
    {
        for (auto& n : nodes) n->render(work, numSamples);
    }
    else if (! wantProcess && bypassMix <= 0.0f)
    {
        // Fully bypassed: `work` already holds the dry input.
    }
    else
    {
        // Crossfade between processed and dry so toggling bypass cannot click.
        if (bypassDryBuffer.getNumSamples() < numSamples)
            bypassDryBuffer.setSize(1, numSamples, false, false, true);
        juce::FloatVectorOperations::copy(bypassDryBuffer.getWritePointer(0), work, numSamples);
        for (auto& n : nodes) n->render(work, numSamples);
        const float* dry = bypassDryBuffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            bypassMix = juce::jlimit(0.0f, 1.0f, bypassMix + (wantProcess ? bypassFadeStep : -bypassFadeStep));
            work[i] = dry[i] + (work[i] - dry[i]) * bypassMix;
        }
    }

    // Mute: fade over ~8 ms so toggling it never clicks.
    {
        const float target = muted.load() ? 0.0f : 1.0f;
        if (target != muteGain)
        {
            const float step = (target > muteGain ? 1.0f : -1.0f)
                             / juce::jmax(1.0f, (float) (currentSampleRate.load() * 0.008));
            for (int i = 0; i < numSamples; ++i)
            {
                muteGain = juce::jlimit(0.0f, 1.0f, muteGain + step);
                work[i] *= muteGain;
            }
        }
        else if (muteGain <= 0.0f)
        {
            juce::FloatVectorOperations::clear(work, numSamples);
        }
    }

    // Ramp back up after a preset/style/auto-tune change.
    int fadeLeft = fadeRemaining.load(std::memory_order_acquire);
    const int fadeSize = fadeTotal.load(std::memory_order_relaxed);
    if (fadeLeft > 0 && fadeSize > 0)
    {
        const float total = (float) fadeSize;
        for (int i = 0; i < numSamples && fadeLeft > 0; ++i, --fadeLeft)
            work[i] *= 1.0f - (float) fadeLeft / total;
        fadeRemaining.store(fadeLeft, std::memory_order_release);
    }

    // Final safety boundary: -1 dBFS instead of full scale, so the signal handed
    // to the radio always keeps a little headroom (0 dBFS was audibly clipping on
    // the receiving end as soon as any stage overshot).
    constexpr float finalCeiling = 0.8912509f; // -1.0 dBFS
    for (int i = 0; i < numSamples; ++i)
        work[i] = std::isfinite(work[i]) ? juce::jlimit(-finalCeiling, finalCeiling, work[i]) : 0.0f;

    {
        float peak = 0.0f, sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float v = work[i];
            peak = juce::jmax(peak, std::abs(v));
            sum += v * v;
        }
        float rms = std::sqrt(sum / (float) numSamples);
        float prev = outLevel.load();
        outLevel.store(juce::jmax(peak, prev * 0.88f));
        prev = outRms.load();
        outRms.store(juce::jmax(rms, prev * 0.92f));
    }

    visualization.pushWave(work, numSamples);
    pushFFT(work, numSamples);

    for (int ch = 0; ch < channels; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::copy(outputs[ch] + outputOffset, work, numSamples);
}
