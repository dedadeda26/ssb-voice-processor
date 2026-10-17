#include "SmartTune.h"
#include <algorithm>
#include <limits>

namespace SmartTune
{
namespace
{
constexpr int kFftOrder = 10;
constexpr int kFftSize = 1 << kFftOrder;

float bandEdgeHz(int index)
{
    constexpr float low = 60.0f, high = 12000.0f;
    return low * std::pow(high / low, (float) index / (float) kNumBands);
}

float toDb(float gain) { return juce::Decibels::gainToDecibels(gain, -100.0f); }
float clampDb(float value, float lo, float hi) { return juce::jlimit(lo, hi, value); }

float bandLevelForRange(const std::array<float, kNumBands>& bands, float loHz, float hiHz)
{
    double sum = 0.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        const float centre = std::sqrt(bandEdgeHz(b) * bandEdgeHz(b + 1));
        if (centre < loHz || centre > hiHz) continue;
        sum += std::pow(10.0, (double) bands[(size_t) b] / 10.0);
    }
    return sum > 0.0 ? (float) (10.0 * std::log10(sum)) : -100.0f;
}

juce::String formatSigned(float value, int decimals)
{
    return (value >= 0.0f ? "+" : "") + juce::String(value, decimals);
}
} // namespace

// ---------------------------------------------------------------- extraction

Features extract(const float* samples, int count, double sampleRate)
{
    Features f;
    f.sampleRate = sampleRate;
    f.sampleCount = count;
    f.speechBandDb.fill(-100.0f);
    f.noiseBandDb.fill(-100.0f);
    if (samples == nullptr || count < kFftSize || ! std::isfinite(sampleRate) || sampleRate <= 0.0)
        return f;

    const int noiseEnd = count / 4;

    // ---- time-domain statistics -------------------------------------------
    float peak = 0.0f;
    int clipped = 0;
    for (int i = 0; i < count; ++i)
    {
        const float v = samples[i];
        if (! std::isfinite(v)) return f;
        peak = juce::jmax(peak, std::abs(v));
        if (std::abs(v) >= 0.999f) ++clipped;
    }
    f.clipRatio = (float) clipped / (float) count;

    const int frameSize = juce::jmax(1, (int) std::lround(sampleRate * 0.02));
    std::vector<float> noiseFrames, speechFrames;
    double totalSpeechSquare = 0.0;
    int totalSpeechSamples = 0;
    for (int pos = 0; pos + frameSize <= count; pos += frameSize)
    {
        double sum = 0.0;
        for (int i = pos; i < pos + frameSize; ++i) sum += (double) samples[i] * samples[i];
        const float rms = (float) std::sqrt(sum / (double) frameSize);
        if (pos < noiseEnd && pos + frameSize > noiseEnd) continue;
        if (pos < noiseEnd) noiseFrames.push_back(rms);
        else
        {
            speechFrames.push_back(rms);
            totalSpeechSquare += sum;
            totalSpeechSamples += frameSize;
        }
    }
    f.noiseRmsDb = noiseFrames.empty() ? -100.0f
                                       : toDb(std::sqrt(juce::jmax(0.0f, noiseFrames[noiseFrames.size() / 2] * noiseFrames[noiseFrames.size() / 2])));
    if (! noiseFrames.empty())
    {
        std::vector<float> sortedNoise = noiseFrames;
        std::sort(sortedNoise.begin(), sortedNoise.end());
        f.noiseRmsDb = toDb(sortedNoise[(size_t) (sortedNoise.size() * 0.9f)]);
    }
    if (totalSpeechSamples > 0)
        f.speechRmsDb = toDb((float) std::sqrt(totalSpeechSquare / (double) totalSpeechSamples));
    if (! speechFrames.empty())
    {
        std::vector<float> sortedSpeech = speechFrames;
        std::sort(sortedSpeech.begin(), sortedSpeech.end());
        const size_t p95 = juce::jmin(sortedSpeech.size() - 1,
                                      (size_t) (0.95f * (float) sortedSpeech.size()));
        f.speechP95Db = toDb(sortedSpeech[p95]);
    }
    f.speechPeakDb = toDb(peak);
    f.crestDb = f.speechPeakDb - f.speechRmsDb;
    f.snrDb = f.speechRmsDb - f.noiseRmsDb;

    const float activityFloor = juce::jmax(toDb(0.0031622777f), f.noiseRmsDb + 6.0f); // -50 dB or noise+6
    int activeFrames = 0;
    for (float rms : speechFrames) if (toDb(rms) > activityFloor) ++activeFrames;
    f.activitySeconds = (float) ((double) activeFrames * frameSize / sampleRate);

    // ---- spectral statistics ---------------------------------------------
    juce::dsp::FFT fft(kFftOrder);
    std::vector<float> fftData((size_t) kFftSize * 2, 0.0f);
    std::vector<float> window((size_t) kFftSize, 0.0f);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                                                    * (float) i / (float) (kFftSize - 1));
    double windowEnergy = 0.0;
    for (float w : window) windowEnergy += (double) w * w;
    const double norm = 2.0 / ((double) kFftSize * windowEnergy);

    std::vector<double> speechBins((size_t) kFftSize / 2, 0.0), noiseBins((size_t) kFftSize / 2, 0.0);
    int speechFramesUsed = 0, noiseFramesUsed = 0;
    for (int pos = 0; pos + kFftSize <= count; pos += kFftSize / 2)
    {
        const bool isNoise = pos + kFftSize <= noiseEnd;
        const bool isSpeech = pos >= noiseEnd;
        if (! isNoise && ! isSpeech) continue;

        for (int i = 0; i < kFftSize; ++i) fftData[(size_t) i] = samples[pos + i] * window[(size_t) i];
        std::fill(fftData.begin() + kFftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        auto& bins = isNoise ? noiseBins : speechBins;
        for (int k = 1; k < kFftSize / 2; ++k)
        {
            const double mag = fftData[(size_t) k];
            bins[(size_t) k] += mag * mag * norm;
        }
        if (isNoise) ++noiseFramesUsed; else ++speechFramesUsed;
    }

    auto fillBands = [&](const std::vector<double>& bins, int frames, std::array<float, kNumBands>& out)
    {
        if (frames <= 0) return;
        const double binHz = sampleRate / (double) kFftSize;
        for (int b = 0; b < kNumBands; ++b)
        {
            const double lo = bandEdgeHz(b), hi = bandEdgeHz(b + 1);
            if (lo >= sampleRate * 0.5) { out[(size_t) b] = -100.0f; continue; }
            int k0 = juce::jmax(1, (int) std::floor(lo / binHz));
            int k1 = juce::jmin(kFftSize / 2 - 1, (int) std::ceil(hi / binHz));
            double sum = 0.0;
            for (int k = k0; k <= k1; ++k) sum += bins[(size_t) k];
            out[(size_t) b] = sum > 0.0 ? (float) (10.0 * std::log10(sum / (double) frames)) : -100.0f;
        }
    };
    fillBands(speechBins, speechFramesUsed, f.speechBandDb);
    fillBands(noiseBins, noiseFramesUsed, f.noiseBandDb);

    const float lowLevel = bandLevelForRange(f.speechBandDb, 60.0f, 300.0f);
    const float allLevel = bandLevelForRange(f.speechBandDb, 60.0f, 12000.0f);
    f.lowBandDb = lowLevel - allLevel;
    f.presenceDb = bandLevelForRange(f.speechBandDb, 2000.0f, 3000.0f);
    const float midLevel = bandLevelForRange(f.speechBandDb, 1000.0f, 3000.0f);
    f.sibBandDb = bandLevelForRange(f.speechBandDb, 5000.0f, 9000.0f);
    f.sibilanceDb = f.sibBandDb - midLevel;

    // Least-squares slope of the speech spectrum between 300 Hz and 3 kHz.
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int n = 0;
    for (int b = 0; b < kNumBands; ++b)
    {
        const float centre = std::sqrt(bandEdgeHz(b) * bandEdgeHz(b + 1));
        if (centre < 300.0f || centre > 3000.0f) continue;
        if (f.speechBandDb[(size_t) b] <= -99.0f) continue;
        const double x = std::log2((double) centre);
        const double y = (double) f.speechBandDb[(size_t) b];
        sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
    }
    if (n >= 2)
    {
        const double denom = (double) n * sxx - sx * sx;
        if (std::abs(denom) > 1e-9)
            f.tiltDbPerOct = (float) (((double) n * sxy - sx * sy) / denom);
    }
    return f;
}

// ----------------------------------------------------------------- proposal

TargetSettings propose(const Features& f, const float* samples, int count, double sampleRate,
                       const CalibrationAnalysis::Plan& base,
                       const ParamModel* model, bool inputGainEnabled, bool highPassEnabled)
{
    juce::ignoreUnused(samples, count, sampleRate, inputGainEnabled, highPassEnabled);

    TargetSettings t;
    auto add = [&t](const char* node, const char* param, float value) { t.values.push_back({ node, param, value }); };

    const float inGain = base.inputGain;
    add("Input Gain", "gain", inGain);

    // High-pass: keep the tuned 90 Hz default unless the source really carries
    // low-frequency weight, in which case lift it a step (never more).
    const float hp = f.lowBandDb > -12.0f ? 130.0f : (f.lowBandDb > -16.0f ? 110.0f : 90.0f);
    add("High-Pass", "cutoff", hp);

    // Noise gate: level and depth come from the validated calibration plan; the
    // timing is tightened for speech.
    add("Noise Gate", "gateThreshold", base.gateThreshold);
    add("Noise Gate", "gateAmount", base.gateAmount);
    add("Noise Gate", "gateAttack", 4.0f);
    add("Noise Gate", "gateRelease", 220.0f);

    // Spectral reduction: only as much as the measured SNR asks for. It stays a
    // no-op until the noise profile of this capture is installed.
    const float nrAmount = f.snrDb < 12.0f ? 70.0f
                         : f.snrDb < 22.0f ? 50.0f
                         : f.snrDb < 35.0f ? 30.0f : 0.0f;
    add("Spectral NR", "nrAmount", nrAmount);
    add("Spectral NR", "nrMaxReduction", -18.0f);

    // EQ: gentle correction of what the microphone actually delivers.
    const float lowTrim  = juce::jlimit(-4.0f, 1.0f, (f.lowBandDb + 12.0f) * -0.6f);
    const float midTrim  = juce::jlimit(-2.0f, 3.0f, (-1.5f - f.tiltDbPerOct) * 0.6f);
    const float highTrim = juce::jlimit(-3.0f, 2.0f, (-15.0f - f.sibilanceDb) * 0.25f);
    add("EQ", "lowGain", lowTrim);
    add("EQ", "lowFreq", 190.0f);
    add("EQ", "midGain", midTrim);
    add("EQ", "midFreq", 1600.0f);
    add("EQ", "midQ", 0.8f);
    add("EQ", "highGain", highTrim);
    add("EQ", "highFreq", 4200.0f);

    // Multi-band: peakier sources get more work; dense ones get less. The
    // threshold is now a real knob, so it can follow the measured level.
    const float density = juce::jlimit(0.0f, 1.0f, (f.crestDb - 6.0f) / 12.0f);
    add("Multi-Band", "lowAmount", 30.0f + 25.0f * density);
    add("Multi-Band", "midAmount", 30.0f + 25.0f * density);
    add("Multi-Band", "highAmount", 20.0f + 20.0f * density);
    add("Multi-Band", "thresh", clampDb(f.speechRmsDb + inGain - 8.0f, -40.0f, -16.0f));
    add("Multi-Band", "attack", 15.0f);
    add("Multi-Band", "release", 180.0f);
    add("Multi-Band", "makeup", 0.0f); // the node's auto make-up handles the level

    // Compressor: threshold from the loud speech level, ratio from the crest.
    const float compThreshold = clampDb(f.speechP95Db + inGain - 5.0f, -36.0f, -14.0f);
    const float compRatio = f.crestDb > 15.0f ? 2.5f : (f.crestDb > 10.0f ? 3.5f : 5.0f);
    const float predictedGr = juce::jmax(0.0f, (f.speechRmsDb + inGain) - compThreshold)
                            * (1.0f - 1.0f / compRatio) * 0.6f;
    add("Compressor", "threshold", compThreshold);
    add("Compressor", "ratio", compRatio);
    add("Compressor", "knee", 8.0f);
    add("Compressor", "attack", 8.0f);
    add("Compressor", "release", 160.0f);
    add("Compressor", "makeup", juce::jlimit(0.0f, 10.0f, predictedGr));

    // Saturation: deliberately conservative, this is a voice chain not a guitar.
    add("Saturation", "drive", f.crestDb > 15.0f ? 5.0f : (f.crestDb > 10.0f ? 3.5f : 2.0f));
    add("Saturation", "mix", 100.0f);
    add("Saturation", "output", 0.0f);

    // De-esser: engage only as much as the measured sibilance asks for.
    const float essAmount = f.sibilanceDb > -8.0f ? 65.0f : (f.sibilanceDb > -14.0f ? 50.0f : 35.0f);
    add("De-Esser", "deEssFreq", f.sibilanceDb > -10.0f ? 5500.0f : 6000.0f);
    add("De-Esser", "deEssThresh", clampDb(f.sibBandDb + 3.0f, -45.0f, -15.0f));
    add("De-Esser", "deEssAmount", essAmount);

    // Limiter: leave a little more room on already hot sources.
    const float hotPeak = f.speechPeakDb + inGain;
    add("Limiter", "limitThreshold", hotPeak > -3.0f ? -2.5f : -1.5f);
    add("Limiter", "limitRelease", 60.0f);

    // Slow leveler: same target the closed-loop output trim aims for, so the two
    // agree instead of fighting each other.
    const float ceiling = t.lookup("Limiter", "limitThreshold", -1.5f);
    add("Auto Level", "agcTarget", juce::jlimit(-16.0f, -10.0f, ceiling - 11.5f));
    add("Auto Level", "agcSpeed", 1500.0f);

    // Output gain starts neutral; the caller trims it against a real chain render.
    add("Output Gain", "outGain", 0.0f);

    if (model != nullptr) model->refine(t, f);

    t.summary = juce::String::fromUTF8(u8"输入 ")
              + formatSigned(t.lookup("Input Gain", "gain", 0.0f), 1) + "dB"
              + juce::String::fromUTF8(u8" · 低切 ") + juce::String((int) t.lookup("High-Pass", "cutoff", 90.0f)) + "Hz"
              + juce::String::fromUTF8(u8" · 门限 ") + juce::String((int) t.lookup("Noise Gate", "gateThreshold", -50.0f)) + "dB"
              + juce::String::fromUTF8(u8" · 压缩 ") + juce::String(t.lookup("Compressor", "ratio", 3.0f), 1) + ":1"
              + juce::String::fromUTF8(u8" · 饱和 ") + juce::String(t.lookup("Saturation", "drive", 3.0f), 1) + "dB"
              + juce::String::fromUTF8(u8" · 齿音 ") + juce::String((int) t.lookup("De-Esser", "deEssAmount", 40.0f)) + "%"
              + juce::String::fromUTF8(u8" · 限幅 ") + juce::String(t.lookup("Limiter", "limitThreshold", -1.5f), 1) + "dB";
    return t;
}

// -------------------------------------------------------------- model hook

std::unique_ptr<ParamModel> defaultModel() { return std::make_unique<ResidualMlp>(); }

void ResidualMlp::refine(TargetSettings& settings, const Features& f) const
{
    // Without trained weights the model must not change anything.
    if (! hasWeights()) return;

    float in[kModelInputs] = {
        juce::jlimit(-1.0f, 1.0f, f.crestDb / 24.0f),
        juce::jlimit(-1.0f, 1.0f, f.snrDb / 40.0f),
        juce::jlimit(-1.0f, 1.0f, (f.speechRmsDb + 60.0f) / 60.0f),
        juce::jlimit(-1.0f, 1.0f, (f.speechP95Db + 60.0f) / 60.0f),
        juce::jlimit(-1.0f, 1.0f, f.tiltDbPerOct / 8.0f),
        juce::jlimit(-1.0f, 1.0f, f.sibilanceDb / 30.0f),
        juce::jlimit(-1.0f, 1.0f, f.lowBandDb / 30.0f),
        juce::jlimit(-1.0f, 1.0f, (f.presenceDb + 60.0f) / 60.0f),
        juce::jlimit(-1.0f, 1.0f, f.activitySeconds / 6.0f),
        juce::jlimit(-1.0f, 1.0f, f.clipRatio * 100.0f),
        juce::jlimit(-1.0f, 1.0f, (f.noiseRmsDb + 100.0f) / 100.0f),
        juce::jlimit(-1.0f, 1.0f, settings.lookup("Input Gain", "gain", 0.0f) / 24.0f),
    };

    float hidden[kHidden];
    for (int h = 0; h < kHidden; ++h)
    {
        float sum = w[(size_t) (kModelInputs * kHidden + h)];
        for (int i = 0; i < kModelInputs; ++i) sum += in[i] * w[(size_t) (h * kModelInputs + i)];
        hidden[h] = std::tanh(sum);
    }

    float out[kOutputs];
    const int outBase = kModelInputs * kHidden + kHidden;
    for (int o = 0; o < kOutputs; ++o)
    {
        float sum = w[(size_t) (outBase + kHidden * kOutputs + o)];
        for (int h = 0; h < kHidden; ++h) sum += hidden[h] * w[(size_t) (outBase + o * kHidden + h)];
        out[o] = std::tanh(sum);
    }

    // Bounded residuals in engineering units; the model can refine, never rewrite.
    struct Target { const char* node; const char* param; float limit; };
    const Target targets[kOutputs] = {
        { "Input Gain",   "gain",           4.0f },
        { "Noise Gate",   "gateThreshold",  8.0f },
        { "Compressor",   "threshold",      8.0f },
        { "Saturation",   "drive",          3.0f },
        { "De-Esser",     "deEssAmount",   15.0f },
        { "Output Gain",  "outGain",        6.0f },
    };
    for (int o = 0; o < kOutputs; ++o)
    {
        const float current = settings.lookup(targets[o].node, targets[o].param, 0.0f);
        settings.set(targets[o].node, targets[o].param, current + out[o] * targets[o].limit);
    }
}

// ------------------------------------------------------------ engine helpers

NodeState captureState(AudioEngine& engine)
{
    NodeState state;
    for (int i = 0; i < engine.getNumNodes(); ++i)
    {
        auto* node = engine.getNode(i);
        if (node == nullptr) continue;
        NodeState::Entry entry;
        entry.node = node->getName();
        entry.bypassed = node->isBypassed();
        for (const auto& p : node->getParameters())
            if (p.param != nullptr) entry.params.emplace_back(p.id, p.param->get());
        state.entries.push_back(std::move(entry));
    }
    return state;
}

void restoreState(AudioEngine& engine, const NodeState& state)
{
    for (const auto& entry : state.entries)
        for (int i = 0; i < engine.getNumNodes(); ++i)
        {
            auto* node = engine.getNode(i);
            if (node == nullptr || node->getName() != entry.node) continue;
            node->setBypassed(entry.bypassed);
            for (const auto& p : node->getParameters())
                if (p.param != nullptr)
                    for (const auto& saved : entry.params)
                        if (saved.first == p.id) p.param->set(saved.second);
            break;
        }
}

void applySettings(AudioEngine& engine, const TargetSettings& settings)
{
    for (const auto& s : settings.values)
        for (int i = 0; i < engine.getNumNodes(); ++i)
        {
            auto* node = engine.getNode(i);
            if (node == nullptr || node->getName() != s.node) continue;
            for (auto& p : node->getParameters())
                if (p.id == s.param && p.param != nullptr) p.param->set(s.value);
            break;
        }
}

ChainMeasurement measureChain(AudioEngine& engine, const float* samples, int count)
{
    ChainMeasurement result;
    if (samples == nullptr || count <= 0) return result;

    const double rate = engine.getCurrentSampleRate();
    const int speechStart = count / 4;
    const int maxSamples = juce::jmin(count - speechStart,
                                      (int) juce::jmax(2048.0, rate * 4.0)); // <= 4 s keeps it cheap
    if (maxSamples <= 0) return result;

    // Copy out of the capture buffer: the nodes process in place.
    std::vector<float> work((size_t) maxSamples);
    std::copy_n(samples + speechStart, maxSamples, work.begin());

    constexpr int chunk = 4096;
    float preLimiterPeak = 0.0f;
    for (int i = 0; i < engine.getNumNodes(); ++i)
    {
        auto* node = engine.getNode(i);
        if (node == nullptr || node->isBypassed()) continue;
        // The slow leveler adapts while we render, which would make the static
        // trim unmeasurable; it is level-independent by design, so measure the
        // chain without it and let it normalise whatever arrives afterwards.
        if (node->getName() == "Auto Level") continue;
        if (node->getName() == "Limiter")
            for (float v : work) preLimiterPeak = juce::jmax(preLimiterPeak, std::abs(v));
        for (int pos = 0; pos < maxSamples; pos += chunk)
            node->process(work.data() + pos, juce::jmin(chunk, maxSamples - pos));
    }

    double sum = 0.0;
    float peak = 0.0f;
    for (float v : work)
    {
        sum += (double) v * v;
        peak = juce::jmax(peak, std::abs(v));
    }
    result.rmsDb = toDb((float) std::sqrt(sum / (double) maxSamples));
    result.peakDb = toDb(peak);
    result.preLimiterPeakDb = toDb(preLimiterPeak);

    auto* limiter = [&engine]
    {
        for (int i = 0; i < engine.getNumNodes(); ++i)
            if (engine.getNode(i) != nullptr && engine.getNode(i)->getName() == "Limiter")
                return engine.getNode(i);
        return (DspNode*) nullptr;
    }();
    if (limiter != nullptr && ! limiter->isBypassed())
    {
        float ceilingDb = -1.5f;
        for (const auto& p : limiter->getParameters())
            if (p.id == "limitThreshold") ceilingDb = p.param->get();
        result.limiterGrDb = juce::jmax(0.0f, result.preLimiterPeakDb - ceilingDb);
    }

    // The offline render moved every filter and envelope; clear them so the
    // live chain does not start from measurement state.
    for (int i = 0; i < engine.getNumNodes(); ++i)
        if (engine.getNode(i) != nullptr) engine.getNode(i)->reset();

    return result;
}

} // namespace SmartTune
