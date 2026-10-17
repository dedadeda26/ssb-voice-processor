#include "TestSupport.h"
#include "TutorialWindow.h"
#include "Nodes/InputGainNode.h"
#include "Nodes/OutputGainNode.h"
#include "Nodes/SaturationNode.h"
#include "Nodes/BandLimitNode.h"
#include "Nodes/LimiterNode.h"
#include "Nodes/MultiBandCompNode.h"
#include "Nodes/SpectralNrNode.h"
#include "Nodes/AutoLevelNode.h"
#include "SelfTest.h"
#include "NodeCard.h"
#include "MainComponent.h"
#include "FreqResponse.h"
#include "SmartTune.h"
#include "CalibrationAnalysis.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
float maxStep(const float* data, int count)
{
    float worst = 0.0f;
    for (int i = 1; i < count; ++i)
        worst = std::max(worst, std::abs(data[i] - data[i - 1]));
    return worst;
}

float maxStepInRange(const std::vector<float>& v, int from, int to)
{
    from = juce::jmax(1, from);
    to = juce::jmin((int) v.size(), to);
    return maxStep(v.data() + from - 1, to - from + 1);
}

// A gain change of +12 dB on a steady level must not step the waveform: the
// ramp spreads the jump over ~12 ms, so no adjacent sample pair may differ by
// more than a small fraction of the level change.
void testGainRamp()
{
    InputGainNode gain;
    gain.prepare(48000.0, 512);
    constexpr int total = 4096;
    std::vector<float> buffer(total, 0.25f);
    gain.process(buffer.data(), 512);

    gain.gainDb.set(12.0f);
    for (int pos = 512; pos < total; pos += 512)
        gain.process(buffer.data() + pos, 512);

    const float step = maxStepInRange(buffer, 400, total);
    require(step < 0.02f, "Input gain step was not ramped (zipper/click)");
    require(buffer[total - 1] > 0.24f * 3.9f, "Input gain ramp did not reach the new target");

    OutputGainNode out;
    out.prepare(48000.0, 512);
    std::fill(buffer.begin(), buffer.end(), 0.25f);
    out.process(buffer.data(), 512);
    out.gainDb.set(-12.0f);
    for (int pos = 512; pos < total; pos += 512)
        out.process(buffer.data() + pos, 512);
    require(maxStepInRange(buffer, 400, total) < 0.02f, "Output gain step was not ramped");

    SaturationNode sat;
    sat.prepare(48000.0, 512);
    std::fill(buffer.begin(), buffer.end(), 0.2f);
    sat.driveDb.set(0.0f);
    sat.process(buffer.data(), 512);
    sat.driveDb.set(24.0f);
    for (int pos = 512; pos < total; pos += 512)
        sat.process(buffer.data() + pos, 512);
    require(maxStepInRange(buffer, 400, total) < 0.02f, "Saturation drive step was not ramped");

    std::cout << "PASS: input/output gain and saturation drive are ramped (worst step "
              << step << " FS)\n";
}

// Toggling a module bypass must crossfade, not cut.
void testBypassCrossfade()
{
    InputGainNode node;
    node.prepare(48000.0, 512);
    node.gainDb.set(12.0f);
    constexpr int total = 4096;
    std::vector<float> buffer(total, 0.25f);
    for (int pos = 0; pos < total; pos += 512)
        node.render(buffer.data() + pos, 512);
    require(std::abs(buffer[total - 1] - 1.0f) < 0.02f, "Active node did not apply its gain");

    node.setBypassed(true);
    std::fill(buffer.begin(), buffer.end(), 0.25f);
    for (int pos = 0; pos < total; pos += 512)
        node.render(buffer.data() + pos, 512);
    const float step = maxStepInRange(buffer, 1, total);
    require(step < 0.02f, "Bypass toggle produced a waveform discontinuity");
    require(std::abs(buffer[total - 1] - 0.25f) < 0.01f, "Bypassed node did not pass dry signal");

    node.setBypassed(false);
    std::fill(buffer.begin(), buffer.end(), 0.25f);
    for (int pos = 0; pos < total; pos += 512)
        node.render(buffer.data() + pos, 512);
    require(maxStepInRange(buffer, 1, total) < 0.02f, "Un-bypass produced a waveform discontinuity");
    require(std::abs(buffer[total - 1] - 1.0f) < 0.02f, "Un-bypassed node did not apply its gain");

    std::cout << "PASS: per-module bypass crossfades in both directions\n";
}

// Global bypass on the engine must also crossfade, and a soft transition must
// duck the output and ramp back instead of stepping.
void testEngineTransitions()
{
    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    engine->audioDeviceAboutToStart(&device);
    engine->setBypass(true);

    std::vector<float> input(4096, 0.25f), output;
    process(*engine, input, output);
    require(std::abs(output.back() - 0.25f) < 1e-6f, "Bypassed engine did not pass samples bit-exactly");

    engine->setBypass(false);            // engage the whole chain, which removes DC
    process(*engine, input, output);
    const float step = maxStepInRange(output, 1, (int) output.size());
    require(step < 0.02f, "Global bypass toggle produced a waveform discontinuity");
    require(std::abs(output.back()) < 0.05f, "Chain did not take effect after un-bypass");

    engine->beginSoftTransition(25.0);
    process(*engine, input, output);
    require(std::abs(output.front()) < 0.02f, "Soft transition did not duck the first sample");
    require(maxStepInRange(output, 1, (int) output.size()) < 0.02f, "Soft transition ramped in steps");
    require(! engine->isTransitioning(), "Soft transition did not finish");

    std::vector<float> second(4096, 0.25f), secondOut;
    process(*engine, second, secondOut);
    require(std::abs(secondOut.front() - output.back()) < 0.05f, "Output jumped after the transition");

    std::cout << "PASS: global bypass crossfade and preset soft transition\n";
}

// The band limit must reach the SSB stopband requirement: 24 dB/oct means at
// least ~24 dB down one octave above the cutoff, with the passband untouched.
void testBandLimitSlope()
{
    constexpr double rate = 48000.0;
    auto measure = [&](float frequency)
    {
        BandLimitNode node;
        node.prepare(rate, 512);
        std::vector<float> buffer(48000);
        for (size_t i = 0; i < buffer.size(); ++i)
            buffer[i] = 0.25f * std::sin(2.0f * juce::MathConstants<float>::pi
                                         * frequency * (float) i / (float) rate);
        // Warm up the filters, then measure over a whole number of cycles.
        node.process(buffer.data(), 24000);
        node.process(buffer.data() + 24000, 24000);
        double sum = 0.0;
        for (size_t i = 24000; i < buffer.size(); ++i) sum += (double) buffer[i] * buffer[i];
        return juce::Decibels::gainToDecibels((float) std::sqrt(sum / 24000.0), -100.0f);
    };

    const float passband = measure(1000.0f) - juce::Decibels::gainToDecibels(0.25f * 0.70710678f);
    const float stopband = measure(6200.0f) - juce::Decibels::gainToDecibels(0.25f * 0.70710678f);
    require(passband > -1.5f && passband < 1.5f, "Band-limit passband is no longer flat");
    require(stopband <= -20.0f, "Band-limit stopband is too shallow (12 dB/oct regression)");
    std::cout << "PASS: band limit is " << juce::String(stopband, 1)
              << " dB at 6.2 kHz (passband " << juce::String(passband, 1) << " dB)\n";
}

// Raising a multi-band Amount knob must no longer act as a volume control: the
// node's automatic make-up keeps the level roughly constant.
void testMultiBandAutoMakeup()
{
    constexpr double rate = 48000.0;
    auto level = [&](float amount)
    {
        MultiBandCompNode node;
        node.lowAmount.set(amount);
        node.midAmount.set(amount);
        node.highAmount.set(amount);
        node.prepare(rate, 512);
        std::vector<float> block(512);
        double sum = 0.0;
        for (int b = 0; b < 60; ++b)
        {
            for (size_t i = 0; i < block.size(); ++i)
            {
                const float t = (float) (b * 512 + (int) i) / (float) rate;
                block[i] = 0.3f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * t)
                         + 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 2400.0f * t);
            }
            node.process(block.data(), 512);
            if (b >= 50) for (float v : block) sum += (double) v * v; // settled
        }
        return juce::Decibels::gainToDecibels((float) std::sqrt(sum / (10.0 * 512.0)), -100.0f);
    };

    const float off = level(0.0f);
    const float heavy = level(80.0f);
    require(std::abs(heavy - off) < 3.0f, "Multi-band Amount still changes loudness");
    require(heavy < off + 1.0f, "Multi-band make-up overshoots");

    // The explicit threshold must actually drive the reduction.
    auto measuredGr = [&](float thresholdDb)
    {
        MultiBandCompNode node;
        node.midAmount.set(100.0f);
        node.threshDb.set(thresholdDb);
        node.prepare(rate, 512);
        std::vector<float> block(512);
        for (int b = 0; b < 40; ++b)
        {
            for (size_t i = 0; i < block.size(); ++i)
            {
                const float t = (float) (b * 512 + (int) i) / (float) rate;
                block[i] = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * t);
            }
            node.process(block.data(), 512);
        }
        return node.getBandGrDb(1);
    };
    require(measuredGr(-20.0f) < -6.0f, "Multi-band threshold knob has no effect");
    require(measuredGr(-6.0f) > -1.0f, "Multi-band compresses below its threshold");
    std::cout << "PASS: multi-band auto make-up keeps level within "
              << juce::String(std::abs(heavy - off), 1) << " dB across Amount 0-80\n";
}

// The look-ahead limiter must attenuate before the peak arrives, so the safety
// clamp never has to act.
void testLimiterLookahead()
{
    constexpr double rate = 48000.0;
    LimiterNode limiter;
    limiter.prepare(rate, 512);
    limiter.thresholdDb.set(-6.0f);
    const float ceiling = juce::Decibels::decibelsToGain(-6.0f);

    std::vector<float> buffer(4800);
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        const float t = (float) i / (float) rate;
        // 6 dB over the ceiling, plus a hard transient at the start.
        buffer[i] = (i < 100 ? 2.0f : 1.0f) * std::sin(2.0f * juce::MathConstants<float>::pi * 700.0f * t);
    }
    limiter.process(buffer.data(), (int) buffer.size());

    for (float sample : buffer)
        require(std::abs(sample) <= ceiling + 1e-4f, "Look-ahead limiter exceeded its ceiling");
    require(limiter.getClampCount() == 0, "Look-ahead limiter still needed the safety clamp");
    std::cout << "PASS: look-ahead limiter holds " << juce::String(ceiling, 2)
              << " with 0 safety clamps\n";
}
// 4x oversampling must remove the in-band alias images that tanh produces at the
// device rate. 17th harmonic of 2.9 kHz = 49.3 kHz folds to 1.3 kHz at 48 kHz.
void testSaturationAntialiasing()
{
    constexpr double rate = 48000.0;
    constexpr int fftSize = 32768;
    constexpr int total = fftSize + 4096;

    SaturationNode sat;
    sat.prepare(rate, 512);
    sat.driveDb.set(24.0f);
    sat.mixPct.set(100.0f);

    std::vector<float> out((size_t) total);
    for (int i = 0; i < total; ++i)
        out[(size_t) i] = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 2900.0f
                                          * (float) i / (float) rate);
    for (int pos = 0; pos < total; pos += 512)
        sat.process(out.data() + pos, juce::jmin(512, total - pos));

    juce::dsp::FFT fft(15);
    std::vector<float> data((size_t) fftSize * 2, 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                                               * (float) i / (float) (fftSize - 1));
        data[(size_t) i] = out[(size_t) (i + 4096)] * w;
    }
    fft.performFrequencyOnlyForwardTransform(data.data());

    const double binHz = rate / (double) fftSize;
    auto binFor = [&](double hz) { return juce::jmax(1, (int) std::lround(hz / binHz)); };
    const float fundamental = data[(size_t) binFor(2900.0)];
    float spur = 0.0f;
    for (int bin = binFor(1000.0); bin <= binFor(1600.0); ++bin)
        spur = juce::jmax(spur, data[(size_t) bin]);

    const float spurDb = juce::Decibels::gainToDecibels(spur / juce::jmax(1e-9f, fundamental), -120.0f);
    require(spurDb < -55.0f, "Saturation still aliases into the transmitted band");
    std::cout << "PASS: saturation in-band alias spur at +24 dB drive = "
              << juce::String(spurDb, 1) << " dBc\n";
}

// Spectral NR must be transparent before a noise profile exists, then actually
// remove the profiled noise without eating a signal well above it.
void testSpectralNoiseReduction()
{
    constexpr double rate = 48000.0;
    auto makeNoise = [&](int count, juce::Random& rng, float amplitude)
    {
        std::vector<float> v((size_t) count);
        float low = 0.0f;
        for (auto& sample : v)
        {
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            low = low * 0.6f + white * 0.4f;         // a little low-frequency tilt
            sample = amplitude * (0.6f * white + 0.4f * low);
        }
        return v;
    };

    // 1) No profile: bit-exact pass-through, no added latency.
    SpectralNrNode plain;
    plain.prepare(rate, 512);
    std::vector<float> buffer(4096);
    juce::Random rng(1234);
    for (size_t i = 0; i < buffer.size(); ++i) buffer[i] = 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
    const auto reference = buffer;
    for (int pos = 0; pos < (int) buffer.size(); pos += 512)
        plain.process(buffer.data() + pos, 512);
    require(buffer == reference, "Spectral NR is not transparent without a profile");

    // 2) With the profile of this very noise, the noise must go down a lot.
    juce::Random profileRng(99);
    const auto noise = makeNoise(48000, profileRng, 0.02f);
    SpectralNrNode node;
    node.prepare(rate, 512);
    node.amountPct.set(100.0f);
    node.setNoiseProfile(noise.data(), (int) noise.size(), rate);
    require(node.hasProfile(), "Noise profile was not accepted");

    auto runThrough = [&](const std::vector<float>& input)
    {
        std::vector<float> out = input;
        for (int pos = 0; pos < (int) out.size(); pos += 512)
            node.process(out.data() + pos, juce::jmin(512, (int) out.size() - pos));
        double sum = 0.0;
        const int skip = 8192;
        for (int i = skip; i < (int) out.size(); ++i) sum += (double) out[(size_t) i] * out[(size_t) i];
        return juce::Decibels::gainToDecibels((float) std::sqrt(sum / (double) (out.size() - skip)), -100.0f);
    };

    juce::Random noiseRng(4242);
    const auto noiseOnly = makeNoise(48000, noiseRng, 0.02f);
    const float inputLevel = juce::Decibels::gainToDecibels(
        (float) std::sqrt(std::inner_product(noiseOnly.begin(), noiseOnly.end(), noiseOnly.begin(), 0.0)
                          / (double) noiseOnly.size()), -100.0f);
    const float residual = runThrough(noiseOnly);
    require(residual < inputLevel - 10.0f, "Spectral NR did not attenuate the profiled noise");

    // 3) A voice-band tone well above the noise must survive.
    std::vector<float> tone((size_t) 48000);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f
                                  * (float) i / (float) rate);
    node.reset();
    const float toneOut = runThrough(tone);
    const float toneIn = juce::Decibels::gainToDecibels(0.2f * 0.70710678f, -100.0f);
    require(toneOut > toneIn - 1.5f, "Spectral NR attenuated a clean voice-band tone");

    std::cout << "PASS: spectral NR is transparent without a profile, removes "
              << juce::String(inputLevel - residual, 1) << " dB of profiled noise, keeps the tone\n";
}

// The slow leveler must bring very different input levels to the same target
// without reacting to individual syllables.
void testAutoLevel()
{
    constexpr double rate = 48000.0;
    AutoLevelNode agc;
    agc.prepare(rate, 512);
    agc.targetDb.set(-12.0f);
    agc.speedMs.set(1000.0f);

    auto runLevel = [&](float amplitude, int blocks)
    {
        std::vector<float> block(512);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float t = (float) (b * 512 + i) / (float) rate;
                block[(size_t) i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * 500.0f * t);
            }
            agc.process(block.data(), 512);
        }
        double sum = 0.0;
        for (float v : block) sum += (double) v * v;
        return juce::Decibels::gainToDecibels((float) std::sqrt(sum / 512.0), -100.0f);
    };

    // ~-30 dBFS RMS, then ~-6 dBFS RMS; 4 s each is ~4 time constants.
    const float quiet = runLevel(0.0447f, 375);
    require(std::abs(quiet + 12.0f) < 2.0f, "AGC did not lift a quiet signal to the target");

    const float loud = runLevel(0.5f, 375);
    require(std::abs(loud + 12.0f) < 2.5f, "AGC did not pull a loud signal back to the target");

    // Fast changes must not be chased: one loud block alone barely moves the gain.
    runLevel(0.05f, 200);
    const float gainBefore = agc.getCurrentGainDb();
    runLevel(0.5f, 1);
    require(std::abs(agc.getCurrentGainDb() - gainBefore) < 1.0f,
            "AGC reacted to a single block (pumping)");

    std::cout << "PASS: slow AGC settles at " << juce::String(quiet, 1) << " / "
              << juce::String(loud, 1) << " dBFS from -30 and -6 dBFS inputs\n";
}

// The shipped self test must agree with the individual checks above: if any of
// its own thresholds ever regress, this fails with the report attached.
void testSelfTestReport()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto results = SelfTest::run(rate);
        require(results.size() == 4, "Self test did not run every check");
        require(SelfTest::allPassed(results),
                ("Self test failed:\n" + SelfTest::report(results)).toRawUTF8());
        const auto text = SelfTest::report(results);
        require(text.contains("PASS") && results[0].detail.isNotEmpty()
                    && results[3].detail.isNotEmpty(),
                "Self test report is missing detail");
    }
    std::cout << "PASS: self test passes at 44.1/48/96 kHz\n";
}

// Visual check that a parameter-heavy module now lays its knobs out at a usable
// size (the old fixed 4-per-row grid produced ~28 px knobs in a 640 px window).
void testKnobLayoutSnapshot()
{
    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    engine->audioDeviceAboutToStart(&device);

    DspNode* multiband = nullptr;
    for (int i = 0; i < engine->getNumNodes(); ++i)
        if (engine->getNode(i)->getName() == "Multi-Band") multiband = engine->getNode(i);
    require(multiband != nullptr, "Multi-band node missing");

    NodeCard card(multiband, *engine, false); // advanced mode: all 9 knobs + meters
    card.setLargePanel(true);
    card.setSize(1500, 460); // roughly the card height in a 640 px window
    const auto image = card.createComponentSnapshot(card.getLocalBounds(), true, 1.0f);
    auto stream = juce::File::getCurrentWorkingDirectory().getChildFile("knob-layout.png").createOutputStream();
    require(stream && stream->setPosition(0) && stream->truncate().wasOk()
                && juce::PNGImageFormat().writeImageToStream(image, *stream),
            "Could not write the knob layout snapshot");
    std::cout << "PASS: multi-band card layout rendered (" << image.getWidth() << "x" << image.getHeight() << ")\n";
}

// Settings that must survive a restart: the 3D view gain (reported lost) and the
// whole chain when no named preset is active.
void testSessionPersistence()
{
    // %TEMP% can point at a per-session sandbox folder that no longer exists,
    // so use the working directory (the build folder when run under ctest).
    auto dir = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("ssb-settings", "", false);
    require(dir.createDirectory().wasOk(), "Could not create a temp settings dir");

    juce::PropertiesFile::Options opts;
    opts.applicationName = "ssb-test";
    opts.filenameSuffix = ".settings";
    juce::PropertiesFile props(dir.getChildFile("test.settings"), opts);

    {
        // Set the value while the app is "running" (loadUISettings happens in the
        // constructor), then destroy it: that is the exit path that writes settings.
        auto main = std::make_unique<MainComponent>(false, dir.getChildFile("Presets"), &props);
        setSharedWaterfallGain(2.4f);
    }
    require(std::abs(props.getDoubleValue("waterfallGain", 0.0) - 2.4) < 0.01,
            "Waterfall gain was not written on exit");
    require(props.getValue("sessionState").isNotEmpty(), "Chain state was not written on exit");

    setSharedWaterfallGain(1.0f);
    {
        auto main = std::make_unique<MainComponent>(false, dir.getChildFile("Presets"), &props);
        require(std::abs(getSharedWaterfallGain() - 2.4f) < 0.01f,
                "Waterfall gain was not restored on startup");
    }

    dir.deleteRecursively();
    std::cout << "PASS: waterfall gain and chain state survive a restart\n";
}

// The drawn curves must describe the real filters. These are textbook values, so
// a wrong coefficient layout (a0 handling) shows up immediately.
void testDrawnResponsesAreReal()
{
    constexpr double rate = 48000.0;

    // 2nd-order Butterworth high-pass at 90 Hz: -12.3 dB one octave down, ~0 dB up.
    const auto hp = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(rate, 90.0f, 0.70710678f);
    const float hpLow = biquadMagnitudeDb(hp, rate, 45.0f);
    const float hpHigh = biquadMagnitudeDb(hp, rate, 1000.0f);
    require(std::abs(hpLow + 12.3f) < 0.6f, "Drawn high-pass curve disagrees with the real filter");
    require(std::abs(hpHigh) < 0.2f, "Drawn high-pass passband is wrong");

    // Peak filter, +6 dB at 1.6 kHz: exact gain at the centre, ~0 dB far away.
    const auto peak = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(
        rate, 1600.0f, 0.8f, juce::Decibels::decibelsToGain(6.0f));
    const float peakCentre = biquadMagnitudeDb(peak, rate, 1600.0f);
    const float peakFar = biquadMagnitudeDb(peak, rate, 100.0f);
    require(std::abs(peakCentre - 6.0f) < 0.05f, "Drawn EQ peak does not hit its gain");
    require(std::abs(peakFar) < 0.6f, "Drawn EQ peak leaks at a distance");

    // Q must be visible: a narrower peak has less gain off-centre.
    const auto narrow = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(
        rate, 1600.0f, 2.5f, juce::Decibels::decibelsToGain(6.0f));
    require(biquadMagnitudeDb(narrow, rate, 2200.0f) < biquadMagnitudeDb(peak, rate, 2200.0f) - 1.0f,
            "EQ curve ignores the Q knob");

    // Low shelf, -6 dB at 200 Hz: -6 dB in the shelf, ~0 dB above it.
    const auto shelf = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf(
        rate, 200.0f, 0.70710678f, juce::Decibels::decibelsToGain(-6.0f));
    require(std::abs(biquadMagnitudeDb(shelf, rate, 30.0f) + 6.0f) < 0.6f, "Drawn low shelf depth is wrong");
    require(std::abs(biquadMagnitudeDb(shelf, rate, 3000.0f)) < 0.6f, "Drawn low shelf leaks into the highs");

    std::cout << "PASS: drawn curves match the real filters (" << juce::String(hpLow, 1)
              << " dB at 45 Hz, peak centre " << juce::String(peakCentre, 1) << " dB)\n";
}

void testLimiterStillHolds()
{
    LimiterNode limiter;
    limiter.prepare(48000.0, 512);
    limiter.thresholdDb.set(-3.0f);
    std::vector<float> buffer(4096);
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = 0.9f * std::sin((float) i * 0.05f);
    for (int pos = 0; pos < (int) buffer.size(); pos += 512)
        limiter.render(buffer.data() + pos, 512);
    const float ceiling = juce::Decibels::decibelsToGain(-3.0f);
    for (float sample : buffer)
        require(std::abs(sample) <= ceiling + 1e-4f, "Limiter exceeded its ceiling");
    std::cout << "PASS: limiter ceiling holds through render()\n";
}
} // namespace

namespace
{
// Synthetic "quiet room then speech" recording with a harmonic source, two
// formants and periodic sibilance, used to drive the smart-tune rules.
std::vector<float> smartRecording(double rate)
{
    const int n = (int) (rate * 4.0);
    std::vector<float> v((size_t) n, 0.0f);
    const double pi = juce::MathConstants<double>::pi;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        const double noise = 0.0012 * (std::sin(2 * pi * 60.0 * t)
                                       + 0.5 * std::sin(2 * pi * 180.0 * t + 1.0));
        if (t < 1.0) { v[(size_t) i] = (float) noise; continue; }

        double speech = 0.0;
        for (int h = 1; h <= 40; ++h)
        {
            const double f = 120.0 * h;
            if (f > rate * 0.45) break;
            double amp = 1.0 / std::pow(f / 120.0, 1.05);
            amp += 1.8 * std::exp(-std::pow((f - 500.0) / 300.0, 2.0));
            amp += 1.2 * std::exp(-std::pow((f - 1800.0) / 500.0, 2.0));
            speech += amp * std::sin(2 * pi * f * t + h * 0.7);
        }
        speech *= 0.6 + 0.4 * std::sin(2 * pi * 3.1 * t);

        double sib = 0.0;
        if (std::fmod(t, 0.8) > 0.58)
            for (int k = 0; k < 5; ++k)
                sib += std::sin(2 * pi * (5200.0 + 700.0 * k) * t + k);

        v[(size_t) i] = (float) (noise + speech + 0.02 * sib);
    }

    // Normalise the speech section to a realistic ~-22 dBFS RMS.
    double sum = 0.0;
    int count = 0;
    for (int i = n / 4; i < n; ++i) { sum += (double) v[(size_t) i] * v[(size_t) i]; ++count; }
    const double rms = std::sqrt(sum / (double) count);
    const float scale = (float) (0.079 / juce::jmax(1.0e-9, rms));
    for (auto& sample : v) sample *= scale;
    return v;
}

std::vector<float> flatState(AudioEngine& engine)
{
    std::vector<float> result;
    for (int i = 0; i < engine.getNumNodes(); ++i)
    {
        result.push_back(engine.getNode(i)->isBypassed() ? 1.0f : 0.0f);
        for (const auto& p : engine.getNode(i)->getParameters()) result.push_back(p.param->get());
    }
    return result;
}

void testSmartTune()
{
    const double rate = 48000.0;
    const auto samples = smartRecording(rate);
    const int count = (int) samples.size();

    const auto plan = CalibrationAnalysis::analyze(samples.data(), count, rate, true, true);
    require(plan.valid(), "smart-tune recording was rejected by the quality gate");

    const auto features = SmartTune::extract(samples.data(), count, rate);
    require(features.speechRmsDb > -40.0f && features.speechRmsDb < -10.0f, "speech level measurement implausible");
    require(features.snrDb > 15.0f, "SNR measurement implausible");
    require(features.crestDb > 3.0f && features.crestDb < 40.0f, "crest measurement implausible");
    require(features.tiltDbPerOct < 0.0f, "spectral tilt sign is wrong for this source");
    require(features.sibilanceDb > -60.0f, "sibilance band was not measured");

    const auto settings = SmartTune::propose(features, samples.data(), count, rate, plan);
    const char* requiredNodes[] = { "Input Gain", "High-Pass", "Noise Gate", "Spectral NR", "EQ",
                                    "Multi-Band", "Compressor", "Saturation", "De-Esser",
                                    "Auto Level", "Limiter", "Output Gain" };
    for (const char* wanted : requiredNodes)
    {
        bool found = false;
        for (const auto& s : settings.values) if (s.node == wanted) found = true;
        require(found, "smart tune left a module untouched");
    }
    require(settings.lookup("Saturation", "drive", 99.0f) <= 6.0f, "saturation proposal is too hot");
    require(settings.lookup("Compressor", "threshold", 0.0f) <= -10.0f, "compressor threshold implausible");
    require(settings.lookup("Compressor", "threshold", 0.0f) >= -36.0f, "compressor threshold out of range");
    require(settings.lookup("Input Gain", "gain", 0.0f) >= -12.0f && settings.lookup("Input Gain", "gain", 0.0f) <= 6.0f,
            "input gain proposal outside the servoed range");
    require(settings.summary.containsIgnoreCase("dB"), "missing human-readable summary");

    // Apply to a real engine, trim the output gain against a real render and
    // check the result lands on the TX target without clipping.
    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    engine->audioDeviceAboutToStart(&device);
    const auto before = flatState(*engine);
    const auto pristine = SmartTune::captureState(*engine);

    SmartTune::applySettings(*engine, settings);
    auto measured = SmartTune::measureChain(*engine, samples.data(), count);
    require(measured.rmsDb > -60.0f && measured.rmsDb < -1.0f, "chain measurement implausible");

    const float ceilingDb = settings.lookup("Limiter", "limitThreshold", -1.5f);
    const float targetRmsDb = juce::jlimit(-16.0f, -10.0f, ceilingDb - 11.5f);
    float outGain = 0.0f;
    auto* outputGainParam = [&]
    {
        for (int i = 0; i < engine->getNumNodes(); ++i)
            if (engine->getNode(i)->getName() == "Output Gain")
                return engine->getNode(i)->getParameters()[0].param;
        return (Param*) nullptr;
    }();
    require(outputGainParam != nullptr, "output gain node missing");
    for (int pass = 0; pass < 4; ++pass)
    {
        const float delta = juce::jlimit(-24.0f, 12.0f,
                                         juce::jmin(targetRmsDb - measured.rmsDb,
                                                    ceilingDb - measured.peakDb));
        if (std::abs(delta) < 0.5f) break;
        const float previousRms = measured.rmsDb;
        outGain = juce::jlimit(-24.0f, 12.0f, outGain + delta);
        outputGainParam->set(outGain);
        measured = SmartTune::measureChain(*engine, samples.data(), count);
        if (delta > 0.0f && measured.rmsDb - previousRms < delta * 0.5f) break;
    }

    // The trim now sits after the limiter, so it may stop at the ceiling instead
    // of the RMS target: either it reached the target, or it used all the headroom.
    const bool reachedTarget = std::abs(measured.rmsDb - targetRmsDb) < 1.5f;
    const bool usedHeadroom = measured.peakDb > ceilingDb - 0.6f;
    require(reachedTarget || usedHeadroom,
            "smart tune neither reached the TX level target nor used the available headroom");
    const float ceiling = juce::Decibels::decibelsToGain(ceilingDb);
    require(measured.peakDb <= juce::Decibels::gainToDecibels(ceiling) + 0.1f,
            "smart tune output exceeds the limiter ceiling");

    // Undo must put every node back exactly, which is what the UI button relies on.
    SmartTune::restoreState(*engine, pristine);
    require(flatState(*engine) == before, "undo did not restore every parameter");

    // The MLP hook must be a no-op without weights and bounded with them.
    SmartTune::ResidualMlp empty;
    require(! empty.hasWeights(), "untrained model claims weights");
    auto refined = settings;
    empty.refine(refined, features);
    require(refined.values.size() == settings.values.size(), "untrained model changed the proposal");
    for (size_t i = 0; i < refined.values.size(); ++i)
        require(refined.values[i].value == settings.values[i].value, "untrained model changed a value");

    std::vector<float> weights((size_t) SmartTune::ResidualMlp::kWeightCount, 0.1f);
    SmartTune::ResidualMlp trained(std::move(weights));
    require(trained.hasWeights(), "weighted model rejected its weights");
    auto shifted = settings;
    trained.refine(shifted, features);
    require(std::abs(shifted.lookup("Saturation", "drive", 0.0f) - settings.lookup("Saturation", "drive", 0.0f)) <= 3.01f,
            "model residual exceeded its bound");
    require(std::abs(shifted.lookup("Input Gain", "gain", 0.0f) - settings.lookup("Input Gain", "gain", 0.0f)) <= 4.01f,
            "model residual exceeded its bound");

    std::cout << "PASS: smart tune touches all 11 modules, lands on "
              << juce::String(measured.rmsDb, 1) << " dBFS RMS, respects the limiter ceiling; "
              << "model residuals bounded\n";
}
} // namespace

// Renders every tutorial page to a PNG so the layout can be inspected without
// launching the GUI.
void testTutorialPages()
{
    for (int page = 0; page < 4; ++page)
    {
        auto comp = createTutorialComponent (page);
        require (comp != nullptr, "tutorial page could not be built");
        comp->setSize (980, 680);

        juce::Image image (juce::Image::ARGB, 980, 680, true);
        {
            juce::Graphics g (image);
            comp->paintEntireComponent (g, true);
        }

        auto file = juce::File::getCurrentWorkingDirectory()
                        .getChildFile ("tutorial-page" + juce::String (page + 1) + ".png");
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);

        // Fixed layout sanity: buttons must sit inside the window.
        for (int i = 0; i < comp->getNumChildComponents(); ++i)
        {
            auto* child = comp->getChildComponent (i);
            require (comp->getLocalBounds().contains (child->getBounds()),
                     "tutorial control outside the window");
        }
    }
}

void testOptimizationFixes()
{
    testGainRamp();
    testBypassCrossfade();
    testEngineTransitions();
    testLimiterStillHolds();
    testBandLimitSlope();
    testMultiBandAutoMakeup();
    testLimiterLookahead();
    testSaturationAntialiasing();
    testSpectralNoiseReduction();
    testAutoLevel();
    testSelfTestReport();
    testKnobLayoutSnapshot();
    testSessionPersistence();
    testDrawnResponsesAreReal();
    testSmartTune();
    testTutorialPages();
}
