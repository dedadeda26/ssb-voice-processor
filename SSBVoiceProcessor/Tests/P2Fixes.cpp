#include "TestSupport.h"
#include "CalibrationAnalysis.h"
#include "MainComponent.h"
#include "SimpleMacro.h"
#include "Nodes/NoiseGateNode.h"
#include "Nodes/CompressorNode.h"
#include "Nodes/DeEsserNode.h"
#include "Nodes/EqNode.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#if __APPLE__
 #include <dlfcn.h>
#endif

namespace
{
struct TemporaryDirectory
{
    // %TEMP% can point at a per-session sandbox folder that no longer exists,
    // so use the working directory (the build folder when run under ctest).
    juce::File path = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("ssb-p2-tests", "", false);
    TemporaryDirectory() { require(path.createDirectory().wasOk(), "Could not create test directory"); }
    ~TemporaryDirectory() { path.deleteRecursively(); }
};

std::vector<float> state(AudioEngine& engine)
{
    std::vector<float> result;
    for (int i = 0; i < engine.getNumNodes(); ++i)
    {
        result.push_back(engine.getNode(i)->isBypassed() ? 1.0f : 0.0f);
        for (auto p : engine.getNode(i)->getParameters()) result.push_back(p.param->get());
    }
    return result;
}

void testGateAndMacros()
{
    NoiseGateNode gate;
    gate.prepare(48000, 512);
    std::vector<float> silence(48000, 0), speech(2400, .2f);
    gate.process(silence.data(), (int)silence.size());
    gate.process(speech.data(), (int)speech.size());
    const float at5 = speech[239] / .2f, at10 = speech[479] / .2f;
    require(at5 > .5f && at10 > .8f && speech.back() / .2f > .99f, "Gate opening still uses slow release");

    CompressorNode compressor;
    SimpleMacro punch(compressor);
    const auto before = std::vector<float>{ compressor.thresholdDb.get(), compressor.ratio.get(), compressor.makeupDb.get() };
    punch.apply(punch.initialValue());
    require(std::abs(compressor.thresholdDb.get()-before[0]) < 1e-5f, "Initial macro changes threshold");
    punch.apply(100);
    require(compressor.thresholdDb.get() <= -30 && compressor.ratio.get() >= 7 && compressor.makeupDb.get() <= 3,
            "Full compressor macro does not compress or adds excessive makeup");
    DeEsserNode deesser;
    SimpleMacro sibilance(deesser);
    sibilance.apply(100);
    require(deesser.threshDb.get() <= -40 && deesser.amountPct.get() == 100, "Full de-esser macro raises threshold");
    EqNode eq;
    SimpleMacro tone(eq);
    tone.apply(100);
    require(eq.lowGain.get() < 0 && eq.highGain.get() > 0, "Tone macro only changes overall gain");

    compressor.thresholdDb.set(-31); compressor.ratio.set(6); compressor.makeupDb.set(2);
    SimpleMacro custom(compressor);
    custom.apply(custom.initialValue() + .01f);
    require(std::abs(compressor.thresholdDb.get()+31) < .02f && std::abs(compressor.ratio.get()-6) < .02f
            && std::abs(compressor.makeupDb.get()-2) < .02f, "Advanced settings jump on first macro movement");
    std::cout << "PASS: gate gain at 5/10 ms=" << at5 << "/" << at10 << "; macro directions, safe endpoints, advanced anchor continuity\n";
}

void testRatesAndFFT()
{
    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    device.block = 257;
    int ranges = 0;
    for (double rate : { 8000.0, 16000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        device.rate = rate;
        engine->audioDeviceAboutToStart(&device);
        for (int i = 0; i < engine->getNumNodes(); ++i)
        {
            auto* node = engine->getNode(i);
            for (auto p : node->getParameters()) if (p.unit == "Hz")
            {
                require(p.minValue > 0 && p.minValue < p.maxValue && p.maxValue < rate * .5, "UI frequency range exceeds Nyquist");
                require(p.param->get() >= p.minValue && p.param->get() <= p.maxValue, "Actual frequency outside UI range");
                ++ranges;
            }
            for (bool maximum : { false, true })
            {
                for (auto p : node->getParameters()) p.param->set(maximum ? p.maxValue : p.minValue);
                std::vector<float> buffer(8193);
                for (size_t j = 0; j < buffer.size(); ++j) buffer[j] = .05f * std::sin((float)j * .173f);
                node->process(buffer.data(), (int)buffer.size());
                for (float sample : buffer) require(std::isfinite(sample), "Low-rate node produced non-finite samples");
            }
        }
    }
    std::array<float, AudioEngine::kSpecBands> reference {};
    constexpr int total = 75000;
    for (int block : { 256, 1, 480, 512, 768, 1024, 2048, 8193, 65537 })
    {
        auto fftEngine = std::make_unique<AudioEngine>(false);
        device.rate = 48000;
        fftEngine->audioDeviceAboutToStart(&device);
        fftEngine->setBypass(true);
        std::vector<float> input(total), output(total);
        for (int i = 0; i < total; ++i) input[(size_t)i] = .2f * std::sin((float)i * .137f);
        for (int offset = 0; offset < total; offset += block)
        {
            const float* in[] = { input.data() + offset };
            float* out[] = { output.data() + offset };
            fftEngine->audioDeviceIOCallbackWithContext(in, 1, out, 1, std::min(block, total-offset), {});
        }
        require(output == input, "Chunked engine changed/lost raw samples");
        require(fftEngine->getFFTFrameCount() == total / AudioEngine::kFFTSize, "FFT discarded crossing-frame samples");
        fftEngine->consumeVisualization();
        for (int band = 0; band < AudioEngine::kSpecBands; ++band)
        {
            const float actual = fftEngine->getSpectrumBand(band);
            if (block == 256) reference[(size_t)band] = actual;
            else require(std::abs(actual-reference[(size_t)band]) < 1e-6f, "FFT spectrum depends on host block size");
        }
        std::vector<float> tail(776, 0), ignored;
        process(*fftEngine, tail, ignored);
        require(fftEngine->getFFTFrameCount() == 74, "FFT remainder was not preserved");
    }
    std::cout << "PASS: " << ranges << " UI/DSP frequency ranges at 7 rates; 9 host block sizes, exact audio and FFT frame/spectrum equivalence\n";
}

std::vector<float> recording(double rate, float speechLevel = .1f, float noiseLevel = .002f)
{
    std::vector<float> result((size_t)(rate * 2));
    for (size_t i = 0; i < result.size(); ++i)
    {
        const double time = i / rate;
        const float noise = noiseLevel * (float)std::sin(2 * juce::MathConstants<double>::pi * 35 * time);
        const float voice = i < result.size()/4 ? 0.0f
            : speechLevel * (.7f + .3f * (float)std::sin(time * 17)) * (float)std::sin(time * 2 * juce::MathConstants<double>::pi * 700);
        result[i] = noise + voice;
    }
    return result;
}

void testCalibrationQuality()
{
    for (double rate : { 8000.0, 16000.0, 48000.0, 96000.0 })
    {
        auto samples = recording(rate);
        const auto active = CalibrationAnalysis::analyze(samples.data(), (int)samples.size(), rate, true, true);
        const auto noGain = CalibrationAnalysis::analyze(samples.data(), (int)samples.size(), rate, false, true);
        const auto noFilter = CalibrationAnalysis::analyze(samples.data(), (int)samples.size(), rate, true, false);
        require(active.valid() && noGain.valid() && noFilter.valid(), "Valid quiet/speech recording was rejected");
        require(std::abs(active.gateNoiseDb-noGain.gateNoiseDb-active.inputGain) < .01f, "Gate threshold ignores effective input gain");
        require(active.gateNoiseDb < noFilter.gateNoiseDb-12, "Gate threshold ignores pre-gate high-pass");
        require(std::abs(active.gateThreshold - juce::jlimit(-80.0f, -20.0f, active.gateNoiseDb+8)) < .001f, "Wrong gate margin");
        auto silence = std::vector<float>(samples.size(), 0);
        require(!CalibrationAnalysis::analyze(silence.data(), (int)silence.size(), rate, true, true).valid(), "Silence accepted");
        const auto noise = recording(rate, 0);
        require(!CalibrationAnalysis::analyze(noise.data(), (int)noise.size(), rate, true, true).valid(), "Steady noise accepted");
        const auto noisy = recording(rate, .001f, .01f);
        require(!CalibrationAnalysis::analyze(noisy.data(), (int)noisy.size(), rate, true, true).valid(), "Low-SNR recording accepted");
        std::fill(samples.begin()+samples.size()/4, samples.end(), 1.0f);
        require(!CalibrationAnalysis::analyze(samples.data(), (int)samples.size(), rate, true, true).valid(), "Clipped recording accepted");
        samples[0] = std::numeric_limits<float>::quiet_NaN();
        require(!CalibrationAnalysis::analyze(samples.data(), (int)samples.size(), rate, true, true).valid(), "NaN recording accepted");
    }
    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    engine->audioDeviceAboutToStart(&device);
    AutoCalibrator calibrator(*engine);
    auto result = AutoCalibrator::Result::succeeded;
    calibrator.onFinished = [&](auto r, const auto&) { result = r; };
    engine->getNode(0)->getParameters()[0].param->set(-7);
    const auto before = state(*engine);
    calibrator.start(1);
    std::vector<float> silence(48000, 0), output;
    process(*engine, silence, output); pumpTimer();
    require(result == AutoCalibrator::Result::failed && state(*engine) == before, "Failed quality check committed parameters");
    calibrator.start(2);
    const auto voice = recording(48000);
    process(*engine, voice, output); pumpTimer();
    require(result == AutoCalibrator::Result::succeeded && state(*engine) != before, "Valid calibration did not commit");
    std::cout << "PASS: calibration silence/noise/SNR/clipping/NaN rejection, 4 rates, gain/HP/bypass compensation and commit-on-success\n";
}

void testPresets()
{
    TemporaryDirectory files;
    auto engine = std::make_unique<AudioEngine>(false);
    PresetManager presets(*engine, files.path.getChildFile("Presets"));
    engine->getNode(0)->getParameters()[0].param->set(-9);
    engine->getNode(2)->setBypassed(true);
    const auto expected = state(*engine);
    require(presets.savePreset(juce::String::fromUTF8(u8"测试 音色")), "Valid Unicode preset did not save");
    presets.loadDefaults();
    require(presets.loadPreset(juce::String::fromUTF8(u8"测试 音色")) && state(*engine) == expected, "Preset round-trip changed state");
    const char* badNames[] = { "", "../other", "a/b", "a\\b", "/tmp/escape", "C:\\escape", ".", "..", "x.", "x ",
                             " CON", "CON", "nul.json", "COM1", "LPT9.txt", "a:b", "a?b", "a\nname",
                             u8"COM¹", u8"COM².txt", u8"COM³", u8"LPT¹", u8"LPT²", u8"LPT³.json" };
    for (const char* name : badNames)
    {
        const auto text = juce::String::fromUTF8(name);
        require(!PresetManager::isValidName(text), "Invalid preset name accepted");
        require(!presets.savePreset(text) && !presets.loadPreset(text) && !presets.deletePreset(text), "Invalid path reached disk operation");
    }
    const char* invalid[] = {
        "null", "{", "{\"nodes\":[]}", "{\"version\":9,\"nodes\":[{}]}",
        "{\"version\":\"1\",\"nodes\":[{}]}",
        "{\"version\":1,\"nodes\":[{\"id\":\"Input Gain\",\"bypassed\":\"false\",\"params\":{}}]}",
        "{\"version\":1,\"nodes\":[{\"id\":\"Input Gain\",\"bypassed\":false,\"params\":{\"gain\":\"9\"}}]}",
        "{\"version\":1,\"nodes\":[{\"id\":\"Input Gain\",\"bypassed\":false,\"params\":{\"gain\":1e100}}]}",
        "{\"version\":1,\"nodes\":[{\"id\":\"Input Gain\",\"bypassed\":false,\"params\":{\"gain\":14}},{\"id\":\"Unknown\",\"bypassed\":false,\"params\":{}}]}",
        "{\"version\":1,\"nodes\":[{\"id\":\"Input Gain\",\"bypassed\":false,\"params\":{}},{\"id\":\"Input Gain\",\"bypassed\":false,\"params\":{}}]}",
        "{\"version\":1,\"nodes\":[{\"id\":\"Input Gain\",\"bypassed\":false,\"params\":{\"typo\":3}}]}"
    };
    for (const char* json : invalid)
    {
        require(presets.getPresetFile("bad").replaceWithText(json), "Cannot write invalid fixture");
        require(!presets.loadPreset("bad") && presets.getLastError().isNotEmpty() && state(*engine) == expected,
                "Invalid preset partially modified live state or reported success");
    }
    require(presets.getPresetFile("legacy").replaceWithText("{\"nodes\":[{\"id\":\"Input Gain\",\"params\":{\"gain\":-3}}]}"), "Legacy fixture failed");
    require(presets.loadPreset("legacy") && engine->getNode(0)->getParameters()[0].param->get() == -3
            && !engine->getNode(2)->isBypassed(), "Legacy migration/default fill failed");
    const auto blocker = files.path.getChildFile("blocked");
    require(blocker.replaceWithText("keep"), "Blocker failed");
    PresetManager unwritable(*engine, blocker.getChildFile("Presets"));
    require(!unwritable.savePreset("test") && blocker.loadFileAsString() == "keep", "Write failure not reported");
#if !JUCE_WINDOWS
    const auto outside = files.path.getChildFile("outside.json");
    require(outside.replaceWithText("outside"), "Symlink fixture failed");
    require(outside.createSymbolicLink(presets.getPresetDir().getChildFile("link.json"), false), "Cannot create test symlink");
    require(!presets.savePreset("link") && !presets.loadPreset("link") && !presets.deletePreset("link")
            && outside.loadFileAsString() == "outside", "Symlink preset reached another file");
#endif
    require(presets.deletePreset("legacy") && !presets.loadPreset("legacy"), "Preset deletion failed");
    std::cout << "PASS: preset round trip, Unicode, 24 invalid names, 11 malformed schemas, atomic failure, legacy migration and write/delete/symlink errors\n";
}

struct DeviceFlags { int failOpen = 0; };
class ManagedDevice : public TestDevice
{
public:
    explicit ManagedDevice(DeviceFlags& f) : TestDevice("SSB Test"), flags(f) {}
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0, 96000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 128, 256, 512 }; }
    juce::String open(const juce::BigInteger& in, const juce::BigInteger& out, double sr, int bs) override
    {
        if (flags.failOpen > 0) { --flags.failOpen; return "Simulated device busy"; }
        inputs = in; outputs = out; rate = sr; block = bs; opened = true; return {};
    }
    void close() override { opened = false; }
    bool isOpen() override { return opened; }
    void start(juce::AudioIODeviceCallback* cb) override { callback = cb; playing = true; if (cb) cb->audioDeviceAboutToStart(this); }
    void stop() override { if (playing && callback) callback->audioDeviceStopped(); playing = false; callback = nullptr; }
    bool isPlaying() override { return playing; }
    juce::BigInteger getActiveInputChannels() const override { return inputs; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs; }
private:
    DeviceFlags& flags;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::BigInteger inputs, outputs;
    bool opened = false, playing = false;
};
class ManagedType : public juce::AudioIODeviceType
{
public:
    explicit ManagedType(DeviceFlags& f) : AudioIODeviceType("SSB Test"), flags(f) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool input) const override { return { input ? "Test input" : "Test output" }; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override { return new ManagedDevice(flags); }
private:
    DeviceFlags& flags;
};

void installTestType(AudioEngine& engine, DeviceFlags& flags)
{
    auto& manager = engine.getDeviceManager();
    std::vector<juce::AudioIODeviceType*> original;
    for (auto* type : manager.getAvailableDeviceTypes()) original.push_back(type);
    for (auto* type : original) manager.removeAudioDeviceType(type);
    manager.addAudioDeviceType(std::make_unique<ManagedType>(flags));
}

void testDeviceSettings()
{
    TemporaryDirectory files;
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = -1;
    juce::PropertiesFile settings(files.path.getChildFile("device.settings"), options);
    DeviceFlags flags;
    auto engine = std::make_unique<AudioEngine>(false, &settings);
    installTestType(*engine, flags);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Test input"; setup.outputDeviceName = "Test output";
    setup.sampleRate = 48000; setup.bufferSize = 512;
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.setBit(0); setup.outputChannels.setBit(0);
    const auto firstError = engine->applyDeviceSettings("SSB Test", setup);
    require(firstError.isEmpty(), firstError.toRawUTF8());
    const auto persisted = settings.getValue("audioDeviceState");
    require(persisted.isNotEmpty() && settings.getFile().existsAsFile(), "Audio device state was not saved");
    flags.failOpen = 1;
    setup.bufferSize = 256;
    const auto error = engine->applyDeviceSettings("SSB Test", setup);
    require(error.contains("busy") && engine->getDeviceManager().getCurrentAudioDevice() != nullptr
            && engine->getDeviceManager().getAudioDeviceSetup().bufferSize == 512
            && settings.getValue("audioDeviceState") == persisted, "Failed device change did not roll back");
    require(engine->applyDeviceSettings("Unknown", setup).isNotEmpty(), "Unknown device type accepted");
    juce::PropertiesFile reloaded(settings.getFile(), options);
    auto restarted = std::make_unique<AudioEngine>(false, &reloaded);
    installTestType(*restarted, flags);
    require(restarted->restoreDeviceSettings().isEmpty(), "Saved device state did not restore");
    const auto restored = restarted->getDeviceManager().getAudioDeviceSetup();
    require(restored.inputDeviceName == "Test input" && restored.outputDeviceName == "Test output"
            && restored.sampleRate == 48000 && restored.bufferSize == 512, "Device state changed across restart");
    auto unavailable = juce::XmlDocument::parse(persisted);
    require(unavailable != nullptr, "Invalid saved device fixture");
    unavailable->setAttribute("audioInputDeviceName", "Unavailable input");
    unavailable->setAttribute("audioOutputDeviceName", "Unavailable output");
    reloaded.setValue("audioDeviceState", unavailable.get());
    const auto desired = reloaded.getValue("audioDeviceState");
    require(restarted->restoreDeviceSettings().isNotEmpty()
            && restarted->getDeviceManager().getCurrentAudioDevice() != nullptr
            && reloaded.getValue("audioDeviceState") == desired, "Missing-device fallback or saved preference retention failed");

    const auto blocker = files.path.getChildFile("blocked-settings");
    require(blocker.replaceWithText("keep"), "Settings blocker failed");
    juce::PropertiesFile blocked(blocker.getChildFile("device.settings"), options);
    auto cannotSave = std::make_unique<AudioEngine>(false, &blocked);
    installTestType(*cannotSave, flags);
    require(cannotSave->applyDeviceSettings("SSB Test", setup).contains("save")
            && cannotSave->getDeviceManager().getCurrentAudioDevice() == nullptr
            && blocker.loadFileAsString() == "keep", "Settings save failure did not roll back/report");
    std::cout << "PASS: real AudioDeviceManager with simulated device: persistence/restart, busy-device rollback, missing-device fallback and save-failure feedback\n";
}

void testUI()
{
    TemporaryDirectory files;
    auto main = std::make_unique<MainComponent>(false, files.path.getChildFile("Presets"));
    auto child = [&](const char* id) { auto* c = main->findChildWithID(id); require(c != nullptr, "Missing UI control id"); return c; };
    auto& engine = main->getAudioEngine();
    engine.getNode(0)->getParameters()[0].param->set(-8);
    engine.getNode(2)->setBypassed(true);
    require(main->saveCurrentPreset("voice") && main->selectPreset({}) && main->selectPreset("voice"), "UI preset sequence failed");
    auto* modules = dynamic_cast<ModuleSelector*>(child("modules"));
    auto* presets = dynamic_cast<juce::ComboBox*>(child("preset"));
    require(modules && modules->isNodeBypassed(2) && presets && presets->getText() == "voice", "Loaded bypass state not reflected in UI");
    const auto before = state(engine);
    require(!main->selectPreset("missing") && state(engine) == before && presets->getText() == "voice", "Failed load corrupted UI state");
    require(main->removePreset("voice") && engine.getNode(0)->getParameters()[0].param->get() == 0
            && !engine.getNode(2)->isBypassed() && !modules->isNodeBypassed(2) && presets->getSelectedId() == 1,
            "Deleting active preset left stale DSP or UI state");
    auto* languages = dynamic_cast<juce::ComboBox*>(child("language"));
    const char* controls[] = { "title", "style", "preset", "save", "delete", "reset", "calibration", "bypass", "simple", "advanced", "settings", "language" };
    for (int language : { 1, 2 })
    {
        languages->setSelectedId(language, juce::sendNotificationSync);
        for (int width : { 1000, 1360, 1600 })
        {
            main->setSize(width, 640);
            require(child("simple")->getY() == child("advanced")->getY(), "Mode switch pair split across rows");
            for (int i = 0; i < 12; ++i)
            {
                const auto a = child(controls[i])->getBounds();
                require(a.getWidth() >= 60 && a.getHeight() >= 30 && main->getLocalBounds().contains(a), "Toolbar control is clipped/empty");
                for (int j = 0; j < i; ++j) require(!a.intersects(child(controls[j])->getBounds()), "Toolbar controls overlap");
            }
            if (width == 1000)
            {
                const auto image = main->createComponentSnapshot(main->getLocalBounds(), true, 2.0f);
                auto stream = juce::File::getCurrentWorkingDirectory().getChildFile(language == 1 ? "p2-ui-zh.png" : "p2-ui-en.png").createOutputStream();
                require(stream && stream->setPosition(0) && stream->truncate().wasOk()
                        && juce::PNGImageFormat().writeImageToStream(image, *stream), "UI screenshot failed");
            }
        }
    }
    std::cout << "PASS: preset UI load/failure/delete synchronization; 12 toolbar controls, 2 languages x 3 widths, 2x rendering\n";
}

void testRealtimeAllocations()
{
#if __APPLE__
    using Begin = void(*)();
    using End = unsigned long long(*)();
    const auto begin = (Begin)dlsym(RTLD_DEFAULT, "ssb_begin_allocation_probe");
    const auto end = (End)dlsym(RTLD_DEFAULT, "ssb_end_allocation_probe");
    require(begin && end, "Allocation probe not loaded; run through CTest");
    begin();
    volatile char* proof = (char*)std::malloc(73);
    proof[0] = 1;
    std::free((void*)proof);
    const auto probeEvents = end();
    require(probeEvents >= 2, "Allocation probe did not detect malloc/free");

    auto engine = std::make_unique<AudioEngine>(false);
    TestDevice device;
    device.block = 257;
    engine->audioDeviceAboutToStart(&device);
    std::vector<Param*> parameters;
    for (int i = 0; i < engine->getNumNodes(); ++i)
        for (auto p : engine->getNode(i)->getParameters()) parameters.push_back(p.param);
    std::vector<float> input(65537), output(65537);
    for (size_t i = 0; i < input.size(); ++i) input[i] = .03f * std::sin((float)i * .13f);
    unsigned long long events = 0;
    for (int cycle = 0; cycle < 4; ++cycle)
        for (int block : { 1, 480, 768, 2048, 8193, 65537 })
        {
            for (auto* parameter : parameters)
            {
                const auto range = parameter->getRange();
                parameter->set(range.minimum + (range.maximum-range.minimum) * (cycle % 2 ? .8f : .2f));
            }
            const float* in[] = { input.data() }; float* out[] = { output.data() };
            begin();
            engine->audioDeviceIOCallbackWithContext(in, 1, out, 1, block, {});
            const auto blockEvents = end();
            events += blockEvents;
            if (blockEvents != 0) std::cout << "Heap events: cycle=" << cycle << ", block=" << block << ", events=" << blockEvents << "\n";
            engine->consumeVisualization();
        }
    std::cout << "Allocation probe: " << events << " malloc/calloc/realloc/free events in 24 parameter-changing callbacks\n";
    require(events == 0, "Real-time callback allocated or freed heap memory");
    std::cout << "PASS: actual callback heap allocation/free count=0, including first callback, filter updates and 65537-sample blocks\n";
#else
    std::cout << "SKIP: malloc interposition is macOS-specific; functional callback tests still run\n";
#endif
}
}

void testP2Fixes()
{
    testGateAndMacros();
    testRatesAndFFT();
    testCalibrationQuality();
    testPresets();
    testDeviceSettings();
    testUI();
    testRealtimeAllocations();
}
