#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

// One audio producer and one message-thread consumer. A full queue drops new
// display data instead of overwriting a slot the consumer may still be reading.
template <typename T, std::size_t Capacity>
class DisplayQueue
{
public:
    bool push(const T& value) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = (write + 1) % Capacity;
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        slots[write] = value;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        if (read == writeIndex.load(std::memory_order_acquire)) return false;
        value = slots[read];
        readIndex.store((read + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    static_assert(Capacity > 1, "A queue needs at least two slots");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "Display queue indices must be lock-free on the audio thread");
    std::array<T, Capacity> slots {};
    alignas(64) std::atomic<std::size_t> writeIndex { 0 };
    alignas(64) std::atomic<std::size_t> readIndex { 0 };
};

class VisualizationData
{
public:
    static constexpr int waveSize = 131072;
    static constexpr int bands = 96;
    static constexpr int historySize = 480;
    using Spectrum = std::array<float, bands>;

    // Audio-thread methods: bounded copies, no allocation, no waiting.
    void pushWave(const float* samples, int count) noexcept
    {
        WavePacket packet;
        while (count > 0)
        {
            packet.count = std::min(count, waveChunkSize);
            std::copy_n(samples, packet.count, packet.samples.begin());
            if (!waveQueue.push(packet)) return;
            samples += packet.count;
            count -= packet.count;
        }
    }

    void pushSpectrum(const Spectrum& frame) noexcept { spectrumQueue.push(frame); }

    // Call only on the message thread before painting. All getters below also
    // belong to that thread, so no UI history array is shared with the producer.
    void consume() noexcept
    {
        WavePacket packet;
        for (int n = 0; n < waveQueueSize && waveQueue.pop(packet); ++n)
            for (int i = 0; i < packet.count; ++i)
            {
                const auto slot = static_cast<std::size_t>(waveWritePos);
                const float sample = packet.samples[static_cast<std::size_t>(i)];
                wave[slot] = sample;
                // One-pole envelope along time. The previous attempt mixed with
                // the same ring slot, i.e. ~2.7 s of audio, which smoothed across
                // unrelated material instead of between neighbouring samples.
                const auto previousSlot = (slot + waveSize - 1) % waveSize;
                const float previous = smoothedWave[previousSlot];
                const float coefficient = std::abs(sample) > std::abs(previous) ? 0.55f : 0.08f;
                smoothedWave[slot] = previous + (sample - previous) * coefficient;
                waveWritePos = (waveWritePos + 1) % waveSize;
            }

        Spectrum frame;
        for (int n = 0; n < spectrumQueueSize && spectrumQueue.pop(frame); ++n)
        {
            spectrum = frame;
            history[static_cast<std::size_t>(spectrumWritePos)] = frame;
            spectrumWritePos = (spectrumWritePos + 1) % historySize;
        }
    }

    float getSpectrumBand(int index) const noexcept
    {
        return index >= 0 && index < bands ? spectrum[static_cast<std::size_t>(index)] : 0.0f;
    }
    float getSpectrumHistory(int time, int band) const noexcept
    {
        if (band < 0 || band >= bands) return 0.0f;
        const auto row = static_cast<std::size_t>(std::clamp(time, 0, historySize - 1));
        return history[row][static_cast<std::size_t>(band)];
    }
    // Direct row access for the 3D view: avoids one bounds-checked call per point.
    const Spectrum& getHistoryRow(int time) const noexcept
    {
        return history[static_cast<std::size_t>(std::clamp(time, 0, historySize - 1))];
    }
    float getWaveSample(int index) const noexcept
    {
        return index >= 0 && index < waveSize ? wave[static_cast<std::size_t>(index)] : 0.0f;
    }
    float getSmoothedWaveSample(int index) const noexcept
    {
        return index >= 0 && index < waveSize ? smoothedWave[static_cast<std::size_t>(index)] : 0.0f;
    }
    int getWaveWritePos() const noexcept { return waveWritePos; }
    int getSpectrumWritePos() const noexcept { return spectrumWritePos; }

private:
    static constexpr int waveChunkSize = 256;
    static constexpr int waveQueueSize = 512;
    static constexpr int spectrumQueueSize = 128;
    struct WavePacket
    {
        std::array<float, waveChunkSize> samples {};
        int count = 0;
    };
    DisplayQueue<WavePacket, waveQueueSize> waveQueue;
    DisplayQueue<Spectrum, spectrumQueueSize> spectrumQueue;

    std::array<float, waveSize> wave {};
    std::array<float, waveSize> smoothedWave {};
    Spectrum spectrum {};
    std::array<Spectrum, historySize> history {};
    int waveWritePos = 0;
    int spectrumWritePos = 0;
};
