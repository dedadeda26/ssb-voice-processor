#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Block-rate smoothed scalar gain with per-sample linear interpolation.
//
// Audio-thread safe: no allocation, no locks. A target change arms a ramp of
// `stepsPerRamp` samples; while the ramp is running every sample gets its own
// gain value, which removes the staircase (zipper) that a per-block constant
// produces when a knob is dragged.
class GainSmoother
{
public:
    void prepare(double newSampleRate, double smoothingSeconds = 0.012) noexcept
    {
        const double samples = juce::jlimit(1.0, 48000.0, newSampleRate * smoothingSeconds);
        stepsPerRamp = (int) std::lround(samples);
    }

    void reset(float value = 1.0f) noexcept
    {
        current = target = value;
        step = 0.0f;
        remaining = 0;
    }

    void setTarget(float newTarget) noexcept
    {
        if (! std::isfinite(newTarget) || newTarget == target) return;
        target = newTarget;
        if (stepsPerRamp <= 0)
        {
            current = target;
            remaining = 0;
            return;
        }
        remaining = stepsPerRamp;
        step = (target - current) / (float) remaining;
    }

    // Jumps immediately (used on prepare/reset, never for a user-visible change).
    void snapTo(float value) noexcept { reset(value); }

    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }
    bool isRamping() const noexcept { return remaining > 0; }

    float next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining <= 0)
            {
                current = target;
                step = 0.0f;
            }
        }
        return current;
    }

    // Multiplies the block in place, interpolating between samples while a ramp
    // is active and falling back to a vector multiply otherwise.
    void applyGain(float* buffer, int numSamples) noexcept
    {
        if (numSamples <= 0) return;
        if (remaining <= 0)
        {
            if (current != 1.0f)
                juce::FloatVectorOperations::multiply(buffer, current, numSamples);
            return;
        }
        for (int i = 0; i < numSamples; ++i)
            buffer[i] *= next();
    }

private:
    float current = 1.0f, target = 1.0f, step = 0.0f;
    int remaining = 0, stepsPerRamp = 576;
};
