#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <juce_core/juce_core.h>

class Param
{
public:
    Param(float defaultValue, float minValue, float maxValue)
        : value(defaultValue), defaultValue(defaultValue), limits(pack(minValue, maxValue)) {}

    struct Range { float minimum, maximum; };
    void set(float v)
    {
        if (!std::isfinite(v)) return;
        const auto r = getRange();
        value.store(juce::jlimit(r.minimum, r.maximum, v));
    }
    float get() const { return value.load(); }
    float getDefault() const { const auto r = getRange(); return juce::jlimit(r.minimum, r.maximum, defaultValue); }
    Range getRange() const
    {
        const auto bits = limits.load();
        const std::uint32_t low = static_cast<std::uint32_t>(bits), high = static_cast<std::uint32_t>(bits >> 32);
        Range r;
        std::memcpy(&r.minimum, &low, sizeof(float));
        std::memcpy(&r.maximum, &high, sizeof(float));
        return r;
    }
    float getMin() const { return getRange().minimum; }
    float getMax() const { return getRange().maximum; }
    void setLimits(float minimum, float maximum)
    {
        jassert(std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum);
        limits.store(pack(minimum, maximum));
        set(get());
    }

private:
    std::atomic<float> value;
    float defaultValue;
    std::atomic<std::uint64_t> limits;
    static std::uint64_t pack(float minimum, float maximum)
    {
        std::uint32_t low, high;
        std::memcpy(&low, &minimum, sizeof(float));
        std::memcpy(&high, &maximum, sizeof(float));
        return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
    }
};
