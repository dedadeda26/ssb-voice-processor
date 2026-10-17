#pragma once
#include "AudioEngine.h"
#include "CalibrationAnalysis.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

// One-click "smart tune".
//
// The calibration capture contains 25 % pure background noise followed by
// speech. CalibrationAnalysis already validates it (silence / clipping / SNR /
// NaN) and derives the input gain and gate threshold. SmartTune goes further:
// it measures the long-term speech spectrum, crest factor, sibilance and level
// distribution, proposes a value for **every** processing module, applies the
// proposal, renders the captured speech through the real node chain offline and
// trims the output gain so the result actually lands on a sane TX level.
//
// A model hook (ParamModel) is provided for a future lightweight network: it may
// only apply bounded residuals to the rule-based proposal, so a missing or
// untrained model is a no-op by construction rather than a regression risk.
namespace SmartTune
{
inline constexpr int kNumBands = 16;

struct Features
{
    double sampleRate = 48000.0;
    int sampleCount = 0;

    float noiseRmsDb = -100.0f;   // background noise, before any processing
    float speechRmsDb = -100.0f;  // speech section, RMS
    float speechP95Db = -100.0f;  // 95th percentile of 20 ms frame levels
    float speechPeakDb = -100.0f;
    float crestDb = 0.0f;         // peak - rms
    float snrDb = 0.0f;
    float clipRatio = 0.0f;
    float activitySeconds = 0.0f;

    float lowBandDb = -100.0f;    // 60-300 Hz share relative to the whole band
    float presenceDb = -100.0f;   // absolute 2-3 kHz band level
    float sibilanceDb = -100.0f;  // 5-9 kHz relative to 1-3 kHz
    float sibBandDb = -100.0f;    // absolute 5-9 kHz band level
    float tiltDbPerOct = 0.0f;    // least-squares slope, 300 Hz - 3 kHz

    std::array<float, kNumBands> speechBandDb {};
    std::array<float, kNumBands> noiseBandDb {};
};

struct Setting
{
    juce::String node;
    juce::String param;
    float value = 0.0f;
};

struct TargetSettings
{
    std::vector<Setting> values;
    juce::String summary;
    bool valid() const { return ! values.empty(); }

    float lookup(const juce::String& node, const juce::String& param, float fallback) const
    {
        for (const auto& s : values)
            if (s.node == node && s.param == param) return s.value;
        return fallback;
    }
    void set(const juce::String& node, const juce::String& param, float value)
    {
        for (auto& s : values)
            if (s.node == node && s.param == param) { s.value = value; return; }
        values.push_back({ node, param, value });
    }
};

// A future lightweight model may only shift the rule-based proposal by a bounded
// amount, so it can refine but never destabilise the result.
class ParamModel
{
public:
    virtual ~ParamModel() = default;
    virtual void refine(TargetSettings&, const Features&) const = 0;
};

// Feature vector for the model hook: 12 normalised inputs.
inline constexpr int kModelInputs = 12;

// Three-layer residual MLP (12 -> 16 -> 6). Weights are shipped empty on
// purpose: until they are trained from real operator preferences the model is a
// no-op and the rule engine drives the result. See docs/一键智能校准.md.
class ResidualMlp final : public ParamModel
{
public:
    static constexpr int kHidden = 16;
    static constexpr int kOutputs = 6;
    static constexpr int kWeightCount = kModelInputs * kHidden + kHidden
                                      + kHidden * kOutputs + kOutputs;

    ResidualMlp() = default;
    explicit ResidualMlp(std::vector<float> weights) : w(std::move(weights)) {}

    bool hasWeights() const { return (int) w.size() == kWeightCount; }
    const std::vector<float>& getWeights() const { return w; }

    void refine(TargetSettings& settings, const Features& f) const override;

private:
    std::vector<float> w;
};

// `model` may be nullptr; the rule proposal is then used unchanged.
std::unique_ptr<ParamModel> defaultModel();

Features extract(const float* samples, int count, double sampleRate);

TargetSettings propose(const Features&, const float* samples, int count, double sampleRate,
                       const CalibrationAnalysis::Plan& base,
                       const ParamModel* model = nullptr,
                       bool inputGainEnabled = true, bool highPassEnabled = true);

// --- engine helpers -------------------------------------------------------

struct NodeState
{
    struct Entry
    {
        juce::String node;
        bool bypassed = false;
        std::vector<std::pair<juce::String, float>> params;
    };
    std::vector<Entry> entries;
    explicit operator bool() const { return ! entries.empty(); }
};

NodeState captureState(AudioEngine&);
void restoreState(AudioEngine&, const NodeState&);

void applySettings(AudioEngine&, const TargetSettings&);

struct ChainMeasurement
{
    float rmsDb = -100.0f;
    float peakDb = -100.0f;
    float preLimiterPeakDb = -100.0f;
    float limiterGrDb = 0.0f;
};

// Renders `samples` through the live node chain (honouring per-module bypass),
// returns level statistics and resets every node so live audio is unaffected.
// Must be called with the audio callback lock held.
ChainMeasurement measureChain(AudioEngine&, const float* samples, int count);

} // namespace SmartTune
