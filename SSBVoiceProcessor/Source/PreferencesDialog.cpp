#include "PreferencesDialog.h"
#include "Localization.h"
#include "AppSettings.h"
#include "BackgroundImageCache.h"

#ifdef _WIN32
 #include <windows.h>
#endif

namespace {
    // 开机自启：只写 HKCU\...\Run 一项（程序现在默认以管理员运行，这条通道可用）。
    // 之前的"启动文件夹 + schtasks 计划任务"多通道已移除——schtasks 会同步等待
    // 最多 5 秒，正是切换开关时假死的来源。
    bool setAutoStart(bool enable)
    {
#ifdef _WIN32
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
            return false;

        bool ok = true;
        if (enable)
        {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            juce::String quoted = "\"" + juce::String(exePath) + "\"";
            const auto* wide = quoted.toWideCharPointer();
            ok = RegSetValueExW(hKey, L"SSBVoiceProcessor", 0, REG_SZ,
                    (const BYTE*) wide,
                    (DWORD) ((wcslen(wide) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        }
        else
        {
            RegDeleteValueW(hKey, L"SSBVoiceProcessor");
        }

        RegCloseKey(hKey);
        return ok;
#else
        juce::ignoreUnused(enable);
        return true;
#endif
    }
}

void PreferencesComponent::styleLabel(juce::Label& lbl, bool title)
{
    if (title) {
        lbl.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        lbl.setColour(juce::Label::textColourId, juce::Colour(0xff7eccff));
    } else {
        lbl.setFont(juce::Font(juce::FontOptions(14.0f)));
        lbl.setColour(juce::Label::textColourId, juce::Colour(0xffd8e0f0));
    }
    lbl.setJustificationType(juce::Justification::centredRight);
}

PreferencesComponent::PreferencesComponent(AudioEngine& e) : engine(e)
{
    auto setupTitle = [this](juce::Label& lbl, const juce::String& txt) {
        lbl.setText(txt, juce::dontSendNotification);
        styleLabel(lbl, true);
        lbl.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lbl);
    };
    setupTitle(audioTitle,      Loc::t("device.panel"));
    setupTitle(startupTitle,    juce::String::fromUTF8(u8"启动 / Startup"));
    setupTitle(windowTitle,     juce::String::fromUTF8(u8"窗口行为 / Window"));
    setupTitle(backgroundTitle, juce::String::fromUTF8(u8"背景图片 / Background"));

    deviceTypeLabel.setText(Loc::t("device.type"), juce::dontSendNotification);
    inputLabel.setText(Loc::t("device.input"), juce::dontSendNotification);
    outputLabel.setText(Loc::t("device.output"), juce::dontSendNotification);
    sampleRateLabel.setText(Loc::t("device.sampleRate"), juce::dontSendNotification);
    bufferLabel.setText(Loc::t("device.buffer"), juce::dontSendNotification);
    for (auto* l : { &deviceTypeLabel, &inputLabel, &outputLabel, &sampleRateLabel, &bufferLabel })
    { styleLabel(*l, false); addAndMakeVisible(l); }

    addAndMakeVisible(deviceTypeBox); addAndMakeVisible(inputBox);
    addAndMakeVisible(outputBox); addAndMakeVisible(sampleRateBox);
    addAndMakeVisible(bufferSizeBox);

    deviceTypeBox.onChange = [this] {
        auto& dm = engine.getDeviceManager();
        auto typeName = deviceTypeBox.getText();
        if (typeName.isEmpty()) return;
        for (auto* type : dm.getAvailableDeviceTypes())
            if (type->getTypeName() == typeName) { dm.setCurrentAudioDeviceType(typeName, true); break; }

        inputBox.clear(juce::dontSendNotification);
        outputBox.clear(juce::dontSendNotification);
        sampleRateBox.clear(juce::dontSendNotification);
        bufferSizeBox.clear(juce::dontSendNotification);
        auto* type = dm.getCurrentDeviceTypeObject();
        if (type == nullptr) return;
        type->scanForDevices();
        auto inputNames  = type->getDeviceNames(true);
        auto outputNames = type->getDeviceNames(false);
        for (int i = 0; i < inputNames.size(); ++i)  inputBox.addItem(inputNames[i], i + 1);
        for (int i = 0; i < outputNames.size(); ++i) outputBox.addItem(outputNames[i], i + 1);
        if (inputBox.getNumItems() > 0)  inputBox.setSelectedId(1, juce::dontSendNotification);
        if (outputBox.getNumItems() > 0) outputBox.setSelectedId(1, juce::dontSendNotification);

        auto* device = dm.getCurrentAudioDevice();
        if (device == nullptr) return;
        int id = 1;
        for (auto sr : device->getAvailableSampleRates()) {
            sampleRateBox.addItem(juce::String(sr, 0), id);
            if (juce::approximatelyEqual(sr, device->getCurrentSampleRate()))
                sampleRateBox.setSelectedId(id, juce::dontSendNotification);
            ++id;
        }
        id = 1;
        for (auto bs : device->getAvailableBufferSizes()) {
            bufferSizeBox.addItem(juce::String(bs), id);
            if (bs == device->getCurrentBufferSizeSamples())
                bufferSizeBox.setSelectedId(id, juce::dontSendNotification);
            ++id;
        }
    };

    applyBtn.setButtonText(Loc::t("device.apply"));
    applyBtn.onClick = [this] { applySettings(); };
    addAndMakeVisible(applyBtn);

    autoStartToggle.setButtonText(juce::String::fromUTF8(u8"开机时自动启动 / Auto start"));
    autoStartToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffd8e0f0));
    autoStartToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff5eb8ff));
    autoStartToggle.onClick = [this] { saveStartupSettings(); };
    addAndMakeVisible(autoStartToggle);

    closeBehaviourLabel.setText(juce::String::fromUTF8(u8"关闭窗口时"), juce::dontSendNotification);
    minimiseBehaviourLabel.setText(juce::String::fromUTF8(u8"最小化时"), juce::dontSendNotification);
    styleLabel(closeBehaviourLabel, false); styleLabel(minimiseBehaviourLabel, false);
    addAndMakeVisible(closeBehaviourLabel); addAndMakeVisible(minimiseBehaviourLabel);

    closeBehaviourBox.addItem(juce::String::fromUTF8(u8"退出软件"), 1);
    closeBehaviourBox.addItem(juce::String::fromUTF8(u8"隐藏到状态栏"), 2);
    addAndMakeVisible(closeBehaviourBox);
    minimiseBehaviourBox.addItem(juce::String::fromUTF8(u8"最小化到任务栏"), 1);
    minimiseBehaviourBox.addItem(juce::String::fromUTF8(u8"隐藏到状态栏"), 2);
    addAndMakeVisible(minimiseBehaviourBox);
    closeBehaviourBox.onChange = [this] { saveWindowBehaviour(); };
    minimiseBehaviourBox.onChange = [this] { saveWindowBehaviour(); };

    // 背景
    chooseBgBtn.setButtonText(juce::String::fromUTF8(u8"选择图片"));
    chooseBgBtn.onClick = [this] { chooseBackgroundImage(); };
    addAndMakeVisible(chooseBgBtn);
    clearBgBtn.setButtonText(juce::String::fromUTF8(u8"清除"));
    clearBgBtn.onClick = [this] { clearBackgroundImage(); };
    addAndMakeVisible(clearBgBtn);
    bgPathLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    bgPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff909ab0));
    bgPathLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0x40000000));
    bgPathLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(bgPathLabel);

    auto setupSliderLabel = [this](juce::Label& l, const juce::String& txt) {
        l.setText(txt, juce::dontSendNotification);
        styleLabel(l, false);
        addAndMakeVisible(l);
    };
    setupSliderLabel(bgOpacityLabel, juce::String::fromUTF8(u8"不透明度"));
    setupSliderLabel(bgBlurLabel,    juce::String::fromUTF8(u8"模糊"));
    setupSliderLabel(bgDimLabel,     juce::String::fromUTF8(u8"全局暗度"));

    auto setupSlider = [](juce::Slider& s, double maxV, double step, const juce::String& suffix) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
        s.setRange(0.0, maxV, step);
        s.setTextValueSuffix(suffix);
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe0e8f8));
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x40000000));
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
        s.setColour(juce::Slider::trackColourId, juce::Colour(0xff7eccff));
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0x80000000));
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    };
    setupSlider(bgOpacitySlider, 100.0, 1.0, "%");
    setupSlider(bgBlurSlider,     40.0, 0.5, "px");
    setupSlider(bgDimSlider,     100.0, 1.0, "%");
    addAndMakeVisible(bgOpacitySlider);
    addAndMakeVisible(bgBlurSlider);
    addAndMakeVisible(bgDimSlider);

    bgOpacitySlider.onValueChange = [this] {
        BackgroundImageCache::get().setOpacity((float)(bgOpacitySlider.getValue() / 100.0));
    };
    bgBlurSlider.onValueChange = [this] {
        BackgroundImageCache::get().setBlur((float) bgBlurSlider.getValue());
    };
    bgDimSlider.onValueChange = [this] {
        BackgroundImageCache::get().setDim((float)(bgDimSlider.getValue() / 100.0));
    };

    closeBtn.setButtonText("Close");
    closeBtn.onClick = [this] { closeDialog(); };
    addAndMakeVisible(closeBtn);

    refreshDeviceLists();
    loadStartupSettings();
    refreshBackgroundUI();
    initialising = false; // 之后用户的改动才写回
    setSize(700, 820);
}

void PreferencesComponent::closeDialog()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
    {
        juce::Component::SafePointer<juce::DialogWindow> safe(dw);
        safe->exitModalState(0);
    }
}

void PreferencesComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad(juce::Colour(0xff1a1e28),
                              r.getCentreX(), r.getY(),
                              juce::Colour(0xff0f1218),
                              r.getCentreX(), r.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRect(r);
}

void PreferencesComponent::resized()
{
    auto area = getLocalBounds().reduced(24);
    auto bottomBar = area.removeFromBottom(46);
    bottomBar.removeFromTop(12);

    audioTitle.setBounds(area.removeFromTop(26));
    area.removeFromTop(8);

    auto row = [&](juce::Label& lbl, juce::ComboBox& box) {
        auto r = area.removeFromTop(30);
        lbl.setBounds(r.removeFromLeft(120)); r.removeFromLeft(12); box.setBounds(r);
        area.removeFromTop(6);
    };
    row(deviceTypeLabel, deviceTypeBox);
    row(inputLabel, inputBox);
    row(outputLabel, outputBox);
    row(sampleRateLabel, sampleRateBox);
    row(bufferLabel, bufferSizeBox);
    area.removeFromTop(6);
    applyBtn.setBounds(area.removeFromTop(32).removeFromLeft(140));
    area.removeFromTop(18);

    startupTitle.setBounds(area.removeFromTop(26));
    area.removeFromTop(8);
    autoStartToggle.setBounds(area.removeFromTop(28).removeFromLeft(340));
    area.removeFromTop(18);

    windowTitle.setBounds(area.removeFromTop(26));
    area.removeFromTop(8);
    auto rowCombo = [&](juce::Label& lbl, juce::ComboBox& box) {
        auto r = area.removeFromTop(30);
        lbl.setBounds(r.removeFromLeft(120)); r.removeFromLeft(12); box.setBounds(r);
        area.removeFromTop(8);
    };
    rowCombo(closeBehaviourLabel, closeBehaviourBox);
    rowCombo(minimiseBehaviourLabel, minimiseBehaviourBox);
    area.removeFromTop(18);

    backgroundTitle.setBounds(area.removeFromTop(26));
    area.removeFromTop(8);

    {
        auto r = area.removeFromTop(32);
        chooseBgBtn.setBounds(r.removeFromLeft(120));
        r.removeFromLeft(8);
        clearBgBtn.setBounds(r.removeFromLeft(80));
        r.removeFromLeft(12);
        bgPathLabel.setBounds(r);
        area.removeFromTop(8);
    }
    auto sliderRow = [&](juce::Label& lbl, juce::Slider& s) {
        auto r = area.removeFromTop(30);
        lbl.setBounds(r.removeFromLeft(120)); r.removeFromLeft(12); s.setBounds(r);
        area.removeFromTop(6);
    };
    sliderRow(bgOpacityLabel, bgOpacitySlider);
    sliderRow(bgBlurLabel,    bgBlurSlider);
    sliderRow(bgDimLabel,     bgDimSlider);

    closeBtn.setBounds(bottomBar.removeFromRight(140).reduced(0, 6));
}

void PreferencesComponent::chooseBackgroundImage()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select Background Image",
        juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

    auto mode = juce::FileBrowserComponent::openMode
              | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(mode, [this](const juce::FileChooser& fc) {
        auto result = fc.getResult();
        if (result.existsAsFile())
        {
            BackgroundImageCache::get().setImageFile(result);
            refreshBackgroundUI();
            if (auto* parent = getParentComponent()) parent->repaint();
        }
        fileChooser.reset();
    });
}

void PreferencesComponent::clearBackgroundImage()
{
    BackgroundImageCache::get().clearImage();
    refreshBackgroundUI();
    if (auto* parent = getParentComponent()) parent->repaint();
}

void PreferencesComponent::refreshBackgroundUI()
{
    auto& cache = BackgroundImageCache::get();
    auto f = cache.getImageFile();
    bgPathLabel.setText(f.existsAsFile() ? f.getFullPathName()
                                         : juce::String::fromUTF8(u8"（未选择）"),
                        juce::dontSendNotification);
    bgOpacitySlider.setValue(cache.getOpacity() * 100.0, juce::dontSendNotification);
    bgBlurSlider.setValue(cache.getBlur(), juce::dontSendNotification);
    bgDimSlider.setValue(cache.getDim() * 100.0, juce::dontSendNotification);
}

void PreferencesComponent::refreshDeviceLists()
{
    auto& dm = engine.getDeviceManager();
    deviceTypeBox.clear(juce::dontSendNotification);
    int id = 1;
    juce::String currentType = dm.getCurrentAudioDeviceType();
    int currentTypeId = 1;
    for (auto* type : dm.getAvailableDeviceTypes()) {
        deviceTypeBox.addItem(type->getTypeName(), id);
        if (type->getTypeName() == currentType) currentTypeId = id;
        ++id;
    }
    deviceTypeBox.setSelectedId(currentTypeId, juce::dontSendNotification);

    auto* type = dm.getCurrentDeviceTypeObject();
    if (type == nullptr) return;
    type->scanForDevices();
    auto inputNames  = type->getDeviceNames(true);
    auto outputNames = type->getDeviceNames(false);

    inputBox.clear(juce::dontSendNotification);
    outputBox.clear(juce::dontSendNotification);
    for (int i = 0; i < inputNames.size(); ++i)  inputBox.addItem(inputNames[i], i + 1);
    for (int i = 0; i < outputNames.size(); ++i) outputBox.addItem(outputNames[i], i + 1);

    auto setup = dm.getAudioDeviceSetup();
    inputBox.setText(setup.inputDeviceName, juce::dontSendNotification);
    outputBox.setText(setup.outputDeviceName, juce::dontSendNotification);

    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr) return;

    sampleRateBox.clear(juce::dontSendNotification);
    id = 1;
    for (auto sr : device->getAvailableSampleRates()) {
        sampleRateBox.addItem(juce::String(sr, 0), id);
        if (juce::approximatelyEqual(sr, device->getCurrentSampleRate()))
            sampleRateBox.setSelectedId(id, juce::dontSendNotification);
        ++id;
    }
    bufferSizeBox.clear(juce::dontSendNotification);
    id = 1;
    for (auto bs : device->getAvailableBufferSizes()) {
        bufferSizeBox.addItem(juce::String(bs), id);
        if (bs == device->getCurrentBufferSizeSamples())
            bufferSizeBox.setSelectedId(id, juce::dontSendNotification);
        ++id;
    }
}

void PreferencesComponent::applySettings()
{
    auto& dm = engine.getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();

    auto typeName = deviceTypeBox.getText();
    if (typeName.isNotEmpty())
        for (auto* type : dm.getAvailableDeviceTypes())
            if (type->getTypeName() == typeName) { dm.setCurrentAudioDeviceType(typeName, true); break; }

    setup.inputDeviceName  = inputBox.getText();
    setup.outputDeviceName = outputBox.getText();
    setup.sampleRate       = sampleRateBox.getText().getDoubleValue();
    setup.bufferSize       = bufferSizeBox.getText().getIntValue();
    dm.setAudioDeviceSetup(setup, true);
    refreshDeviceLists();
}

void PreferencesComponent::saveWindowBehaviour()
{
    if (initialising) return; // 下拉框还没填好时不回写，避免把用户的设置改成默认值

    auto sanitise = [](int id) { return juce::jlimit(1, 2, id); };
    if (auto* s = AppSettings::get())
    {
        s->setValue("closeBehaviour", sanitise(closeBehaviourBox.getSelectedId()));
        s->setValue("minimiseBehaviour", sanitise(minimiseBehaviourBox.getSelectedId()));
        s->save();
    }
}

void PreferencesComponent::saveStartupSettings()
{
    const bool wanted = autoStartToggle.getToggleState();
    const bool applied = setAutoStart(wanted);
    if (! applied) autoStartToggle.setToggleState(! wanted, juce::dontSendNotification);

    if (auto* s = AppSettings::get())
    {
        s->setValue("autoStart", applied && wanted);
        s->setValue("closeBehaviour", juce::jlimit(1, 2, closeBehaviourBox.getSelectedId()));
        s->setValue("minimiseBehaviour", juce::jlimit(1, 2, minimiseBehaviourBox.getSelectedId()));
        s->save();
    }
}

void PreferencesComponent::loadStartupSettings()
{
    auto* s = AppSettings::get();
    if (s == nullptr) return;
    autoStartToggle.setToggleState(s->getBoolValue("autoStart", false), juce::dontSendNotification);
    closeBehaviourBox.setSelectedId(juce::jlimit(1, 2, s->getIntValue("closeBehaviour", 2)),
                                    juce::dontSendNotification);
    minimiseBehaviourBox.setSelectedId(juce::jlimit(1, 2, s->getIntValue("minimiseBehaviour", 1)),
                                       juce::dontSendNotification);
    // 打开设置窗口时不再改写 Run 键（会复活用户删掉的启动项）
}
