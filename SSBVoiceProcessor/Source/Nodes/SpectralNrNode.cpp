#include "SpectralNrNode.h"
#include <algorithm>

SpectralNrNode::SpectralNrNode() : DspNode("Spectral NR") {}

void SpectralNrNode::rebuildWindow()
{
    window.assign((size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) (fftSize - 1));
}

void SpectralNrNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    juce::ignoreUnused(bs);
    rebuildWindow();
    analysis.assign((size_t) fftSize, 0.0f);
    spectrum.assign((size_t) fftSize * 2, 0.0f);
    ola.assign((size_t) fftSize, 0.0f);
    fifoIn.assign((size_t) hop, 0.0f);
    gainState.assign((size_t) (fftSize / 2 + 1), 1.0f);
    fifoFill = 0;
}

void SpectralNrNode::reset()
{
    std::fill(analysis.begin(), analysis.end(), 0.0f);
    std::fill(spectrum.begin(), spectrum.end(), 0.0f);
    std::fill(ola.begin(), ola.end(), 0.0f);
    std::fill(fifoIn.begin(), fifoIn.end(), 0.0f);
    std::fill(gainState.begin(), gainState.end(), 1.0f);
    fifoFill = 0;
}

void SpectralNrNode::clearNoiseProfile()
{
    noiseProfile.clear();
    reset();
}

void SpectralNrNode::setNoiseProfile(const float* samples, int count, double sampleRate)
{
    if (samples == nullptr || count < fftSize || ! std::isfinite(sampleRate) || sampleRate <= 0.0)
    {
        clearNoiseProfile();
        return;
    }

    if (window.size() != (size_t) fftSize) rebuildWindow();

    std::vector<float> accumulated((size_t) (fftSize / 2 + 1), 0.0f);
    std::vector<float> frame((size_t) fftSize * 2, 0.0f);
    int frames = 0;

    for (int pos = 0; pos + fftSize <= count; pos += hop)
    {
        for (int i = 0; i < fftSize; ++i) frame[(size_t) i] = samples[pos + i] * window[(size_t) i];
        std::fill(frame.begin() + fftSize, frame.end(), 0.0f);
        fft.performRealOnlyForwardTransform(frame.data());

        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const float re = frame[(size_t) (2 * k)];
            const float im = frame[(size_t) (2 * k + 1)];
            accumulated[(size_t) k] += re * re + im * im;
        }
        ++frames;
    }

    if (frames == 0)
    {
        clearNoiseProfile();
        return;
    }

    noiseProfile.assign(accumulated.begin(), accumulated.end());
    for (auto& value : noiseProfile) value /= (float) frames;

    // Guard against a degenerate (all-zero) profile so the gain maths stays sane.
    double total = 0.0;
    for (float v : noiseProfile) total += (double) v;
    if (total <= 1e-20)
    {
        clearNoiseProfile();
        return;
    }

    reset();
}

void SpectralNrNode::process(float* buffer, int numSamples)
{
    if (noiseProfile.empty() || amountPct.get() <= 0.5f) return;
    if (analysis.size() != (size_t) fftSize) prepare(sampleRate, juce::jmax(1, blockSize));

    int pos = 0;
    while (pos < numSamples)
    {
        const int take = juce::jmin(numSamples - pos, hop - fifoFill);
        std::copy_n(buffer + pos, take, fifoIn.begin() + fifoFill);
        fifoFill += take;
        pos += take;
        if (fifoFill >= hop)
        {
            processHop(buffer + pos - hop);
            fifoFill = 0;
        }
    }
}

void SpectralNrNode::processHop(float* out)
{
    // Slide the analysis frame and append the new hop.
    std::copy(analysis.begin() + hop, analysis.end(), analysis.begin());
    std::copy(fifoIn.begin(), fifoIn.end(), analysis.end() - hop);

    for (int i = 0; i < fftSize; ++i) spectrum[(size_t) i] = analysis[(size_t) i] * window[(size_t) i];
    std::fill(spectrum.begin() + fftSize, spectrum.end(), 0.0f);
    fft.performRealOnlyForwardTransform(spectrum.data());

    const float amount = juce::jlimit(0.0f, 1.0f, amountPct.get() * 0.01f);
    const float floorGain = juce::Decibels::decibelsToGain(
        juce::jlimit(-40.0f, 0.0f, maxReductionDb.get()));

    const int halfBins = fftSize / 2;
    for (int k = 1; k < halfBins; ++k)
    {
        const float re = spectrum[(size_t) (2 * k)];
        const float im = spectrum[(size_t) (2 * k + 1)];
        const float power = re * re + im * im;
        const float noise = noiseProfile[(size_t) k];

        float gain = 1.0f;
        if (noise > 0.0f)
        {
            const float ratio = noise / juce::jmax(1.0e-20f, power);
            gain = 1.0f - overSubtraction * ratio;
            gain = juce::jlimit(floorGain, 1.0f, gain);
            gain = 1.0f + amount * (gain - 1.0f); // blend towards transparency
        }

        // Fast attack on the way down, slow recovery upwards: less musical noise.
        float& state = gainState[(size_t) k];
        if (gain < state) state = gain;
        else              state += (gain - state) * 0.25f;

        const float g = state;
        spectrum[(size_t) (2 * k)] *= g;
        spectrum[(size_t) (2 * k + 1)] *= g;

        // Keep the mirrored negative bin consistent: some JUCE FFT backends read
        // it, so scaling only the positive side would corrupt the inverse.
        const int mirror = fftSize - k;
        if (mirror > halfBins && mirror < fftSize)
        {
            spectrum[(size_t) (2 * mirror)] *= g;
            spectrum[(size_t) (2 * mirror + 1)] *= g;
        }
    }

    fft.performRealOnlyInverseTransform(spectrum.data());

    for (int i = 0; i < fftSize; ++i) ola[(size_t) i] += spectrum[(size_t) i];
    for (int i = 0; i < hop; ++i) out[i] = ola[(size_t) i];

    std::copy(ola.begin() + hop, ola.end(), ola.begin());
    std::fill(ola.end() - hop, ola.end(), 0.0f);
}

std::vector<DspNode::ParamDescriptor> SpectralNrNode::getParameters()
{
    return {
        { "nrAmount",       "param.nrAmount",    "%",  0.0f, 100.0f, 50.0f, 0, true,  &amountPct },
        { "nrMaxReduction", "param.nrReduction", "dB", -30.0f, -3.0f, -18.0f, 0, false, &maxReductionDb },
    };
}
