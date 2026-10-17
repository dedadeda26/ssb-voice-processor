#pragma once
#include "../DspNode.h"
#include <vector>

// Spectral noise reduction driven by the noise profile measured during a
// smart-tune capture.
//
// 1024-point FFT, 50 % overlap, Hann analysis window and overlap-add without a
// synthesis window (Hann at 50 % overlap sums to exactly 1, so the frame is
// reconstructed bit-for-bit when the gain is 1). Per bin it applies a
// spectral-subtraction gain with an over-subtraction factor, a hard floor and a
// fast-down/slow-up time constant to avoid "musical noise".
//
// Without a profile the node returns immediately, so it is transparent (and adds
// no latency) on a fresh install or before the first smart tune.
class SpectralNrNode : public DspNode
{
public:
    SpectralNrNode();
    void prepare(double, int) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    std::vector<ParamDescriptor> getParameters() override;

    // `samples` should be the quiet part of a calibration capture.
    void setNoiseProfile(const float* samples, int count, double sampleRate);
    void clearNoiseProfile();
    bool hasProfile() const { return ! noiseProfile.empty(); }
    int getProfileBins() const { return (int) noiseProfile.size(); }

    // The analysis frame is 1024 with a 512 hop; the reconstruction is aligned to
    // the input, so the node reports no added latency (the first hop is faded in
    // by the window).
    int getLatencySamples() const { return 0; }
    int getFrameSize() const { return fftSize; }

    Param amountPct      { 50.0f, 0.0f, 100.0f };
    Param maxReductionDb { -18.0f, -30.0f, -3.0f };

private:
    static constexpr int kOrder = 10;
    static constexpr int fftSize = 1 << kOrder;
    static constexpr int hop = fftSize / 2;

    void processHop(float* out);
    void rebuildWindow();

    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, analysis, spectrum, ola, fifoIn, gainState, noiseProfile;
    int fifoFill = 0;
    float overSubtraction = 1.6f;
};
