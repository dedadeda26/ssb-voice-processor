#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <complex>

// Magnitude response of a biquad section, for drawing curves that match the
// audio path.
//
// juce::dsp::IIR::ArrayCoefficients returns the *unnormalised* layout
// { b0, b1, b2, a0, a1, a2 }, whereas Coefficients::assignImpl stores
// { b0, b1, b2, a1, a2, type } after dividing by a0. Getting this wrong draws a
// plausible but wrong curve, so the normalisation happens here explicitly.
inline float biquadMagnitudeDb(const std::array<float, 6>& c, double sampleRate, float hz)
{
    if (! std::isfinite(sampleRate) || sampleRate <= 0.0 || ! std::isfinite(hz) || hz < 0.0f)
        return 0.0f;

    using cd = std::complex<double>;
    const double w = 2.0 * juce::MathConstants<double>::pi * (double) hz / sampleRate;
    const cd z1 = std::polar(1.0, -w);
    const cd z2 = z1 * z1;

    const double a0 = std::abs((double) c[3]) > 1.0e-12 ? (double) c[3] : 1.0;
    const cd num = ((double) c[0] + (double) c[1] * z1 + (double) c[2] * z2) / a0;
    const cd den = 1.0 + ((double) c[4] / a0) * z1 + ((double) c[5] / a0) * z2;

    const double magnitude = std::abs(num) / juce::jmax(1.0e-9, std::abs(den));
    return (float) (20.0 * std::log10(juce::jmax(1.0e-9, magnitude)));
}

// Sum of several sections, for multi-band curves.
template <size_t N>
inline float biquadCascadeMagnitudeDb(const std::array<std::array<float, 6>, N>& sections,
                                      double sampleRate, float hz)
{
    float total = 0.0f;
    for (const auto& section : sections) total += biquadMagnitudeDb(section, sampleRate, hz);
    return total;
}
