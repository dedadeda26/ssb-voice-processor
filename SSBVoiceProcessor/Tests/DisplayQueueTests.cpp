#include "VisualizationData.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

static void require(bool condition)
{
    if (!condition) { std::cerr << "Display queue assertion failed\n"; std::abort(); }
}

int main()
{
    DisplayQueue<int, 4> small;
    require(small.push(1) && small.push(2) && small.push(3));
    require(!small.push(4));
    int value = 0;
    for (int expected = 1; expected <= 3; ++expected)
        require(small.pop(value) && value == expected);
    require(!small.pop(value));
    require(small.push(5) && small.pop(value) && value == 5);

    // Saturate the actual producer queues while the UI is paused, then recover.
    auto display = std::make_unique<VisualizationData>();
    std::array<float, 256> wave;
    wave.fill(0.25f);
    VisualizationData::Spectrum spectrum;
    spectrum.fill(0.5f);
    for (int i = 0; i < 10000; ++i) { display->pushWave(wave.data(), 256); display->pushSpectrum(spectrum); }
    display->consume();
    require(display->getWaveWritePos() == 511 * 256);
    require(display->getWaveSample(0) == 0.25f);
    require(display->getSpectrumBand(0) == 0.5f);
    wave.fill(0.75f);
    display->pushWave(wave.data(), 256);
    display->consume();
    require(display->getWaveWritePos() == 0);
    require(display->getWaveSample(VisualizationData::waveSize - 1) == 0.75f);

    // Verify the ownership hand-off under contention, including wraparound.
    DisplayQueue<VisualizationData::Spectrum, 8> queue;
    constexpr int frames = 200000;
    std::thread producer([&] {
        VisualizationData::Spectrum frame;
        for (int i = 1; i <= frames; ++i)
        {
            frame.fill(static_cast<float>(i));
            while (!queue.push(frame)) std::this_thread::yield();
        }
    });
    VisualizationData::Spectrum frame;
    for (int expected = 1; expected <= frames; ++expected)
    {
        while (!queue.pop(frame)) std::this_thread::yield();
        for (float item : frame) require(item == static_cast<float>(expected));
    }
    producer.join();

    // Exercise the actual transport and UI-owned histories concurrently.
    std::atomic<bool> done { false };
    std::thread audio([&] {
        for (int i = 0; i < 50000; ++i)
        {
            display->pushWave(wave.data(), 256);
            display->pushSpectrum(spectrum);
        }
        done.store(true);
    });
    do
    {
        display->consume();
        for (int i = 0; i < VisualizationData::bands; ++i)
            require(display->getSpectrumBand(i) == 0.5f);
        require(std::isfinite(display->getSmoothedWaveSample(display->getWaveWritePos())));
    } while (!done.load());
    audio.join();
    display->consume();
    std::cout << "PASS: queue full/recovery, 200000 ordered frames, concurrent visualization histories\n";
}
