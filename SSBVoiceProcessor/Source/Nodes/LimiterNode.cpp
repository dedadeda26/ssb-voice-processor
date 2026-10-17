#include "LimiterNode.h"
#include <algorithm>

LimiterNode::LimiterNode() : DspNode("Limiter") {}

void LimiterNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    lookahead = juce::jmax(1, (int) std::lround(sr * 0.001)); // 1 ms
    delayLine.assign((size_t) lookahead, 0.0f);
    magnitudeWindow.assign((size_t) lookahead, 0.0f);
    writePos = 0;
    gain = 1.0f;
    clampedSamples = 0;
}

void LimiterNode::reset()
{
    std::fill(delayLine.begin(), delayLine.end(), 0.0f);
    std::fill(magnitudeWindow.begin(), magnitudeWindow.end(), 0.0f);
    writePos = 0;
    gain = 1.0f;
    h1 = h2 = h3 = 0.0f;
    haveHistory = 0;
    reduction.store(0.0f);
    clampedSamples = 0;
}

void LimiterNode::process(float* buffer, int numSamples)
{
    if (lookahead <= 0 || (int) delayLine.size() != lookahead)
        prepare(sampleRate, juce::jmax(1, blockSize)); // defensive, never on the hot path

    const float ceiling = juce::Decibels::decibelsToGain(thresholdDb.get());
    const float relMs = releaseMs.get();
    const float relCoef = std::exp(-1.0f / (relMs * 0.001f * (float) sampleRate));

    float maxReduc = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = buffer[i];
        const float delayed = delayLine[(size_t) writePos];

        delayLine[(size_t) writePos] = x;

        // True-peak estimate: a cubic through the last four samples catches the
        // inter-sample peak that a sample-peak detector misses (the DAC / SDR
        // rebuilds a higher peak between two samples).
        float peak = std::abs(x);
        if (haveHistory >= 3)
        {
            const float p0 = h3, p1 = h2, p2 = h1, p3 = x;
            for (int k = 1; k <= 3; ++k)
            {
                const float t = (float) k * 0.25f;
                const float y = 0.5f * (2.0f * p1
                                        + (-p0 + p2) * t
                                        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                                        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
                peak = juce::jmax(peak, std::abs(y));
            }
        }
        h3 = h2; h2 = h1; h1 = x;
        if (haveHistory < 3) ++haveHistory;

        magnitudeWindow[(size_t) writePos] = peak;
        if (++writePos >= lookahead) writePos = 0;

        // Worst case already visible inside the look-ahead window: this is the
        // gain the output will need by the time that peak arrives.
        float windowMax = 0.0f;
        for (int k = 0; k < lookahead; ++k)
            windowMax = juce::jmax(windowMax, magnitudeWindow[(size_t) k]);

        const float targetGain = juce::jmin(1.0f, ceiling / juce::jmax(windowMax, 1e-9f));
        if (targetGain < gain) gain = targetGain;
        else                   gain = relCoef * gain + (1.0f - relCoef) * targetGain;

        const float y = delayed * gain;
        if (std::abs(y) > ceiling + 1e-6f) ++clampedSamples;
        buffer[i] = juce::jlimit(-ceiling, ceiling, y);

        maxReduc = juce::jmax(maxReduc, 1.0f - gain);
    }

    auto smooth = [](float o, float n) { return n > o ? n : o * 0.85f + n * 0.15f; };
    reduction.store(smooth(reduction.load(), juce::jlimit(0.0f, 1.0f, maxReduc)));
}

std::vector<DspNode::ParamDescriptor> LimiterNode::getParameters()
{
    return {
        { "limitThreshold", "param.limitThresh",  "dB", -12.0f, 0.0f, -1.0f, 1, true,  &thresholdDb },
        { "limitRelease",   "param.limitRelease", "ms",   5.0f, 500.0f, 50.0f, 0, false, &releaseMs },
    };
}
