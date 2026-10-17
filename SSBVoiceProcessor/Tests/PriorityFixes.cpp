#include "AudioEngine.h"
#include "AutoCalibrator.h"
#include "CalibrationOverlay.h"
#include "Nodes/MultiBandCompNode.h"
#include "Nodes/LimiterNode.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "TestSupport.h"

static void testCrossovers()
{
    double worstDb = 0;
    int cases = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float low : { 100.0f, 300.0f, 800.0f })
            for (float high : { 1500.0f, 3000.0f, 6000.0f })
            {
                // An impulse response tests the actual node, phase compensation,
                // JUCE filters and block processing together (not just formulas).
                MultiBandCompNode node;
                node.lowAmount.set(0); node.midAmount.set(0); node.highAmount.set(0);
                node.crossLowHz.set(low); node.crossHighHz.set(high);
                node.prepare(sr, 257);
                constexpr int count = 65536;
                std::vector<float> impulse(count, 0);
                impulse[0] = 1;
                for (int pos = 0; pos < count; pos += 257)
                    node.process(impulse.data() + pos, std::min(257, count - pos));
                juce::dsp::FFT fft(16);
                std::vector<float> spectrum(count * 2, 0);
                std::copy(impulse.begin(), impulse.end(), spectrum.begin());
                fft.performFrequencyOnlyForwardTransform(spectrum.data(), true);
                for (int i = 1; i < static_cast<int>(count * .45); ++i)
                {
                    const double db = std::abs(20 * std::log10(std::max(1.0e-12f, spectrum[static_cast<size_t>(i)])));
                    worstDb = std::max(worstDb, db);
                    require(db < 0.01, "Multi-band unity sum is not flat");
                }
                ++cases;
            }
    std::cout << "PASS: " << cases << " actual multi-band impulse responses; worst deviation=" << worstDb << " dB\n";
}

static void testFinalOutput()
{
    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    engine->audioDeviceAboutToStart(&device);
    LimiterNode* limiter = nullptr;
    for (int i = 0; i < engine->getNumNodes(); ++i)
    {
        auto* node = engine->getNode(i);
        node->setBypassed(true);
        if (node->getName() == "Limiter") limiter = dynamic_cast<LimiterNode*>(node);
        if (node->getName() == "Output Gain")
        {
            node->setBypassed(false);
            // Output gain now sits after the limiter (load-matching trim), so the
            // ceiling checks below are made with it neutral.
            for (auto p : node->getParameters()) p.param->set(0);
        }
    }
    require(limiter != nullptr && engine->getNode(engine->getNumNodes()-1)->getName() == "Output Gain",
            "Output gain is not the last stage");
    limiter->setBypassed(false);
    std::vector<float> input(512), output;
    double maxExcess = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float ceiling : { -12.0f, -2.0f, 0.0f })
            for (float release : { 5.0f, 50.0f, 500.0f })
            {
                device.rate = sr;
                engine->audioDeviceAboutToStart(&device);
                limiter->thresholdDb.set(ceiling);
                limiter->releaseMs.set(release);
                const float upper = juce::Decibels::decibelsToGain(ceiling);
                for (int block = 0; block < 8; ++block)
                {
                    for (size_t i = 0; i < input.size(); ++i)
                        input[i] = block < 2 ? 1.0f : 2.0f * std::sin(static_cast<float>(i + block * 512) * .137f);
                    process(*engine, input, output);
                    for (float sample : output)
                    {
                        maxExcess = std::max(maxExcess, static_cast<double>(std::abs(sample)-upper));
                        require(std::isfinite(sample) && std::abs(sample) <= upper, "Final output exceeds configured ceiling");
                    }
                }
            }
    // Keep the band-limit enabled as well to exercise the actual full tail.
    for (int i = 0; i < engine->getNumNodes(); ++i)
        if (engine->getNode(i)->getName() == "Band-Limit") engine->getNode(i)->setBypassed(false);
    limiter->thresholdDb.set(-2);
    input.assign(512, 0.9f);
    process(*engine, input, output);
    for (float sample : output) require(std::abs(sample) <= juce::Decibels::decibelsToGain(-2.0f), "Filtered tail exceeds ceiling");
    limiter->setBypassed(true);
    process(*engine, input, output);
    for (float sample : output) require(std::isfinite(sample) && std::abs(sample) <= 1, "Module bypass defeats safety");
    engine->setBypass(true);
    // Bypass is now crossfaded (8 ms) so that toggling it cannot click; let the
    // fade complete, then the bypassed path must be an exact pass-through with
    // the safety clamp still applied to out-of-range/non-finite samples.
    std::vector<float> settle(512, 0.0f), settleOut;
    process(*engine, settle, settleOut);
    input = { -100.0f, 100.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
    process(*engine, input, output);
    // The final safety boundary is -1 dBFS (headroom for the radio), not 0 dBFS.
    constexpr float finalCeiling = 0.8912509f;
    require(std::abs(output[0] + finalCeiling) < 1e-5f
                && std::abs(output[1] - finalCeiling) < 1e-5f
                && output[2] == 0 && output[3] == 0,
            "Global bypass defeats safety");
    // With the output trim pushed +12 dB the limiter ceiling is no longer the
    // bound (it is a pre-trim stage); the engine's 0 dBFS clamp must still hold.
    engine->setBypass(false);
    for (int i = 0; i < engine->getNumNodes(); ++i)
        if (engine->getNode(i)->getName() == "Output Gain")
            for (auto p : engine->getNode(i)->getParameters()) p.param->set(12);
    input.assign(512, 0.3f);
    process(*engine, input, output);
    for (float sample : output)
        require(std::isfinite(sample) && std::abs(sample) <= 1.0f, "Final 0 dBFS clamp failed");
    std::cout << "PASS: final output with +12 dB gain, 27 rate/ceiling/release combinations, bypass and non-finite input; excess=" << maxExcess << "\n";
}

static void testCalibration()
{
    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    double now = 0;
    AutoCalibrator calibration(*engine, [&] { return now; });
    int finishes = 0;
    auto result = AutoCalibrator::Result::succeeded;
    calibration.onFinished = [&](AutoCalibrator::Result value, const juce::String& message) {
        ++finishes; result = value; require(message.isNotEmpty(), "Missing calibration feedback");
    };
    auto* inputGain = engine->getNode(0)->getParameters()[0].param;
    inputGain->set(-7);

    calibration.start();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "No device was accepted");
    device.inputEnabled = false;
    engine->audioDeviceAboutToStart(&device);
    calibration.start();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "No input channel was accepted");
    device.inputEnabled = true; device.outputEnabled = false;
    engine->audioDeviceAboutToStart(&device);
    calibration.start();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "No output channel was accepted");
    device.outputEnabled = true;
    engine->audioDeviceAboutToStart(&device);
    calibration.start(); now += 2001; pumpTimer();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "No-data timeout did not fire");

    calibration.start(); engine->audioDeviceStopped(); pumpTimer();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "Disconnect did not fail");
    engine->audioDeviceAboutToStart(&device);
    calibration.start(); engine->audioDeviceError("test error"); pumpTimer();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "Device error did not fail");
    engine->audioDeviceAboutToStart(&device);
    calibration.start(); engine->stopCapture(); pumpTimer();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "Partial capture was treated as success");

    calibration.start();
    CalibrationOverlay overlay;
    overlay.setSize(1000, 600);
    overlay.onCancel = [&] { calibration.cancel(); };
    overlay.setProgress(.5f);
    overlay.setStage(0, juce::String::fromUTF8(u8"正在采集音频…请正常说话"));
    const auto snapshot = overlay.createComponentSnapshot(overlay.getLocalBounds());
    auto png = juce::File::getCurrentWorkingDirectory().getChildFile("calibration-overlay.png").createOutputStream();
    require(png != nullptr && png->setPosition(0) && png->truncate().wasOk()
            && juce::PNGImageFormat().writeImageToStream(snapshot, *png), "Overlay snapshot failed");
    require(overlay.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Esc was not handled");
    require(!calibration.isRunning() && !engine->isCapturing() && result == AutoCalibrator::Result::cancelled, "Esc cancellation failed");
    const int afterCancel = finishes;
    pumpTimer(); require(finishes == afterCancel, "Cancellation finished more than once");

    calibration.start();
    auto* button = dynamic_cast<juce::TextButton*>(overlay.getChildComponent(0));
    require(button != nullptr && button->isVisible(), "Cancel button missing");
    button->onClick();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::cancelled, "Button cancellation failed");
    require(inputGain->get() == -7, "Failed/cancelled calibration changed parameters");

    // Stalled and slowly arriving data are separate timeout cases.
    std::vector<float> input(512, .1f), output;
    calibration.start(0.2);
    now += 1500; process(*engine, input, output); pumpTimer();
    require(calibration.isRunning(), "Capture failed before its deadline");
    now += 1800; process(*engine, input, output); pumpTimer();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::failed, "Overall capture deadline did not fire");

    calibration.start(2.0);
    input.assign(96000, 0.0f);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = (i < 24000 ? .001f : .1f) * std::sin((float)i * .137f);
    process(*engine, input, output);
    pumpTimer();
    require(!calibration.isRunning() && result == AutoCalibrator::Result::succeeded, "Complete capture did not succeed");
    require(engine->getCapturedSamples() == 96000, "Capture did not stop at exact target");
    const int afterSuccess = finishes;
    pumpTimer(); require(finishes == afterSuccess, "Success finished more than once");
    std::cout << "PASS: calibration no-device/channels, stall/deadline, disconnect/error, partial capture, Esc/button cancel, restart/success (" << finishes << " results)\n";
}

void testP2Fixes();
void testOptimizationFixes();

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    try { if (argc > 1 && juce::String(argv[1]) == "--p2") testP2Fixes();
          else if (argc > 1 && juce::String(argv[1]) == "--opt") testOptimizationFixes();
          else { testCrossovers(); testFinalOutput(); testCalibration(); } }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    std::cout << "All priority-fix regression tests passed.\n";
}
