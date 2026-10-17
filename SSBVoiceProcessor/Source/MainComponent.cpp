#include "MainComponent.h"
#include "PreferencesDialog.h"
#include "StylePresets.h"
#include "TutorialWindow.h"
#include "UiStyle.h"
#include "AppSettings.h"
#include "BackgroundImageCache.h"

MainComponent::MainComponent(bool openDevices, juce::File presetDirectory, juce::PropertiesFile* settings)
    : audioEngine(openDevices, settings)
{
    devicesOpened = openDevices;
    uiSettings = settings ? settings : (openDevices ? AppSettings::get() : nullptr);
    presetManager = std::make_unique<PresetManager>(audioEngine, presetDirectory);
    autoCalibrator = std::make_unique<AutoCalibrator>(audioEngine);

    if (openDevices) BackgroundImageCache::get().loadFromSettings();

    appTitle.setText("SSB Voice Processor", juce::dontSendNotification);
    appTitle.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    appTitle.setColour(juce::Label::textColourId, UiStyle::text);
    addAndMakeVisible(appTitle);

    styleBox.onChange = [this] { onStyleSelected(); };
    addAndMakeVisible(styleBox);

    presetBox.onChange = [this] { onPresetSelected(); };
    addAndMakeVisible(presetBox);

    savePresetBtn.onClick = [this] { onSavePreset(); };
    addAndMakeVisible(savePresetBtn);

    deletePresetBtn.setColour(juce::TextButton::buttonColourId,
                              juce::Colour(0xff4a2028));
    deletePresetBtn.setColour(juce::TextButton::textColourOffId,
                              juce::Colour(0xffffa0a8));
    deletePresetBtn.onClick = [this] { onDeletePreset(); };
    addAndMakeVisible(deletePresetBtn);

    resetBtn.onClick = [this] { onResetDefaults(); };
    addAndMakeVisible(resetBtn);

    autoCalibBtn.setColour(juce::TextButton::buttonColourId, UiStyle::accentGlow);
    autoCalibBtn.setColour(juce::TextButton::buttonOnColourId, UiStyle::autocalBot);
    autoCalibBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    autoCalibBtn.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    autoCalibBtn.onClick = [this] { onAutoCalibrate(); };
    addAndMakeVisible(autoCalibBtn);

    undoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3142));
    undoBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb8c4d8));
    undoBtn.setTooltip(juce::String::fromUTF8(u8"撤销自适应调参，恢复到调参前的全部参数"));
    undoBtn.onClick = [this] { onUndoAutoTune(); };
    addAndMakeVisible(undoBtn);
    undoBtn.setVisible(false);

    autoCalibrator->onProgress = [this](float p) {
        autoCalibBtn.setButtonText(juce::String(juce::roundToInt(p * 100.0f)) + "%");
        if (calibOverlay) calibOverlay->setProgress(p);
    };
    autoCalibrator->onStage = [this](int idx, const juce::String& text) {
        if (calibOverlay) calibOverlay->setStage(idx, text);
    };
    autoCalibrator->onFinished = [this](AutoCalibrator::Result result, const juce::String& message) {
        autoCalibBtn.setButtonText(Loc::t("toolbar.autocal"));
        statusLabel.setText(message, juce::dontSendNotification);
        statusLabel.setTooltip(message);
        if (result == AutoCalibrator::Result::succeeded)
        {
            markCustomPreset();
            // Every module was rewritten: offer a one-click way back.
            undoBtn.setVisible(autoTuneUndoState.entries.size() > 0);
            resized();
        }
        if (calibOverlay) {
            calibOverlay->setCancelEnabled(false);
            if (result == AutoCalibrator::Result::succeeded) calibOverlay->setProgress(1.0f);
        }

        juce::Component::SafePointer<MainComponent> safe(this);
        const auto generation = calibrationGeneration;
        // Defer destruction so the cancel button's own callback can return.
        juce::Timer::callAfterDelay(result == AutoCalibrator::Result::succeeded ? 700 : 1, [safe, generation]() {
            if (safe == nullptr || safe->calibrationGeneration != generation) return;
            if (safe->calibOverlay) {
                safe->removeChildComponent(safe->calibOverlay.get());
                safe->calibOverlay.reset();
            }
            safe->rebuildCurrentCard();
        });
    };

    bypassBtn.setClickingTogglesState(true);
    bypassBtn.setToggleState(false, juce::dontSendNotification);
    bypassBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3142));
    bypassBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe88b30));
    bypassBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    bypassBtn.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
    bypassBtn.onClick = [this] { audioEngine.setBypass(bypassBtn.getToggleState()); };
    addAndMakeVisible(bypassBtn);

    muteBtn.setClickingTogglesState(true);
    muteBtn.setToggleState(false, juce::dontSendNotification);
    muteBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3142));
    muteBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd64545));
    muteBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    muteBtn.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    muteBtn.setTooltip(juce::String::fromUTF8(u8"静音：切断送往电台的输出（8 ms 淡出，不会爆音）"));
    muteBtn.onClick = [this] { audioEngine.setMuted(muteBtn.getToggleState()); };
    addAndMakeVisible(muteBtn);

    simpleBtn.setClickingTogglesState(true);
    simpleBtn.setRadioGroupId(1);
    simpleBtn.setToggleState(true, juce::dontSendNotification);
    simpleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1f2533));
    simpleBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a7fd0));
    simpleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa8b3c8));
    simpleBtn.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    simpleBtn.onClick = [this] {
        simpleMode = true;
        simpleBtn.setToggleState(true, juce::dontSendNotification);
        advancedBtn.setToggleState(false, juce::dontSendNotification);
        rebuildCurrentCard(); saveUISettings(); resized();
    };
    addAndMakeVisible(simpleBtn);

    advancedBtn.setClickingTogglesState(true);
    advancedBtn.setRadioGroupId(1);
    advancedBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1f2533));
    advancedBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a7fd0));
    advancedBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa8b3c8));
    advancedBtn.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    advancedBtn.onClick = [this] {
        simpleMode = false;
        advancedBtn.setToggleState(true, juce::dontSendNotification);
        simpleBtn.setToggleState(false, juce::dontSendNotification);
        rebuildCurrentCard(); saveUISettings(); resized();
    };
    addAndMakeVisible(advancedBtn);

    settingsBtn.onClick = [this] { showPreferences(); };
    addAndMakeVisible(settingsBtn);

    langBox.addItem(juce::String(L"中文"), 1);
    langBox.addItem("EN", 2);
    langBox.setSelectedId(1);
    langBox.onChange = [this] {
        Loc::setLang(langBox.getSelectedId() == 2 ? Loc::Lang::EN : Loc::Lang::ZH);
        updateLanguage(); saveUISettings();
    };
    addAndMakeVisible(langBox);

    juce::StringArray nodeNames;
    for (int i = 0; i < audioEngine.getNumNodes(); ++i)
        nodeNames.add(audioEngine.getNode(i)->getName());
    moduleSelector.setModules(nodeNames);
    moduleSelector.setSelected(0);
    moduleSelector.onSelectionChange = [this](int idx) {
        currentNodeIndex = idx;
        rebuildCurrentCard(); saveUISettings();
    };
    moduleSelector.onToggleBypass = [this](int idx, bool on) {
        auto* node = audioEngine.getNode(idx);
        if (node != nullptr) node->setBypassed(!on);
        markCustomPreset();
        if (idx == currentNodeIndex) rebuildCurrentCard();
    };
    addAndMakeVisible(moduleSelector);

    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);

    statusLabel.setText(Loc::t("status.ready"), juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, UiStyle::textFaint);
    statusLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(statusLabel);

    const std::pair<juce::Component*, const char*> identifiers[] = {
        { &appTitle, "title" }, { &styleBox, "style" }, { &presetBox, "preset" },
        { &savePresetBtn, "save" }, { &deletePresetBtn, "delete" }, { &resetBtn, "reset" },
        { &autoCalibBtn, "calibration" }, { &undoBtn, "undo" }, { &bypassBtn, "bypass" },
        { &muteBtn, "mute" },
        { &simpleBtn, "simple" }, { &advancedBtn, "advanced" }, { &settingsBtn, "settings" },
        { &langBox, "language" }, { &moduleSelector, "modules" }, { &statusLabel, "status" }
    };
    for (const auto& id : identifiers) id.first->setComponentID(id.second);
    setSize(1360, 820);

    updateLanguage();
    refreshPresetList();
    loadUISettings();
    syncModuleStates();
    rebuildCurrentCard();

    if (audioEngine.getDeviceStatus().isNotEmpty())
        statusLabel.setText(audioEngine.getDeviceStatus(), juce::dontSendNotification);

    // 记录初始版本，启动轮询
    lastBgVersion = BackgroundImageCache::get().getVersion();
    startTimerHz(5);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveUISettings();
}

void MainComponent::timerCallback()
{
    int v = BackgroundImageCache::get().getVersion();
    if (v != lastBgVersion)
    {
        lastBgVersion = v;
        repaint();
    }

    // Surface a lost device: the meters drop to zero but without this the user
    // has no way to tell "no input" from "the sound card is gone".
    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    const bool running = device != nullptr && device->isOpen();
    if (! running && ! deviceLostShown)
    {
        deviceLostShown = true;
        statusLabel.setText(Loc::t("status.deviceLost"), juce::dontSendNotification);
    }
    else if (running && deviceLostShown)
    {
        deviceLostShown = false;
        statusLabel.setText(Loc::t("status.ready"), juce::dontSendNotification);
    }
}

void MainComponent::saveSettings() { saveUISettings(); }

void MainComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto& cache = BackgroundImageCache::get();
    bool hasBg = cache.getImageFile().existsAsFile();

    if (hasBg)
    {
        // ---- 纯黑底 ----
        g.setColour(juce::Colours::black);
        g.fillRect(r);

        const auto& img = cache.getProcessed(getWidth(), getHeight());
        if (img.isValid())
        {
            // ---- 图片 100% 不透明绘制（不用 setOpacity，避免色偏） ----
            g.drawImageAt(img, 0, 0);

            // ---- 用黑色遮盖来模拟"不透明度" ----
            // 图片越淡 → 叠加越多黑。色相 100% 保留。
            float op = juce::jlimit(0.0f, 1.0f, cache.getOpacity());
            if (op < 0.999f)
            {
                g.setColour(juce::Colours::black.withAlpha(1.0f - op));
                g.fillRect(r);
            }
        }
    }
    else
    {
        // 无背景图时使用软件原有深蓝渐变
        juce::ColourGradient grad(UiStyle::bgTop, r.getCentreX(), r.getY(),
                                  UiStyle::bgBottom, r.getCentreX(), r.getBottom(),
                                  false);
        g.setGradientFill(grad);
        g.fillRect(r);
    }

    // 顶部工具栏区域（0-175）加一层深色蒙版，保 UI 可读
    {
        juce::Rectangle<float> zone(0.0f, 0.0f, (float) getWidth(), 175.0f);
        juce::ColourGradient z(
            juce::Colour(0xc60a0a0c), zone.getCentreX(), zone.getY(),
            juce::Colour(0x400a0a0c), zone.getCentreX(), zone.getBottom(),
            false);
        g.setGradientFill(z);
        g.fillRect(zone);
    }
    // 底部状态栏
    {
        juce::Rectangle<float> zone(0.0f, (float)(getHeight() - 40),
                                    (float) getWidth(), 40.0f);
        juce::ColourGradient z(
            juce::Colour(0x400a0a0c), zone.getCentreX(), zone.getY(),
            juce::Colour(0xc60a0a0c), zone.getCentreX(), zone.getBottom(),
            false);
        g.setGradientFill(z);
        g.fillRect(zone);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(14);

    auto buttonWidth = [](juce::TextButton& button, int minimum) {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(juce::Font(juce::FontOptions(15.0f)), button.getButtonText(), 0, 0);
        return juce::jmax(minimum, (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + 28);
    };
    constexpr int rowHeight = 40, gap = 8;
    const int settingsWidth = buttonWidth(settingsBtn, 90), languageWidth = 80;
    settingsBtn.setBounds(area.getRight() - settingsWidth, area.getY(), settingsWidth, 34);
    langBox.setBounds(settingsBtn.getX() - gap - languageWidth, area.getY(), languageWidth, 34);
    std::vector<std::pair<juce::Component*, int>> items = {
        { &appTitle, 210 }, { &styleBox, 120 }, { &presetBox, 150 },
        { &savePresetBtn, buttonWidth(savePresetBtn, 72) },
        { &deletePresetBtn, buttonWidth(deletePresetBtn, 72) },
        { &resetBtn, buttonWidth(resetBtn, 72) },
        { &autoCalibBtn, buttonWidth(autoCalibBtn, 190) },
        { &bypassBtn, buttonWidth(bypassBtn, 90) },
        { &muteBtn, buttonWidth(muteBtn, 76) },
        { &simpleBtn, buttonWidth(simpleBtn, 76) },
        { &advancedBtn, buttonWidth(advancedBtn, 76) }
    };
    // The undo button only exists between a successful smart tune and the next
    // edit, so it must not reserve toolbar space the rest of the time.
    if (undoBtn.isVisible())
        items.insert(items.begin() + 7, { &undoBtn, buttonWidth(undoBtn, 96) });
    int row = 0, x = area.getX(), right = langBox.getX() - gap;
    for (const auto& item : items)
    {
        const int groupWidth = item.first == &simpleBtn
                                   ? item.second + gap + buttonWidth(advancedBtn, 76) : item.second;
        if (x + groupWidth > right && x > area.getX())
        {
            ++row; x = area.getX(); right = area.getRight();
        }
        item.first->setBounds(x, area.getY() + row * (rowHeight + gap), item.second, 34);
        x += item.second + gap;
    }
    area.removeFromTop((row + 1) * (rowHeight + gap));

    area.removeFromTop(10);

    auto bottomBar = area.removeFromBottom(22);
    statusLabel.setBounds(bottomBar);
    area.removeFromBottom(8);

    auto inArea = area.removeFromLeft(44);
    auto outArea = area.removeFromRight(44);
    int meterH = juce::jmin(inArea.getHeight() - 20, 460);
    inMeter.setBounds(inArea.withSizeKeepingCentre(38, meterH));
    outMeter.setBounds(outArea.withSizeKeepingCentre(38, meterH));
    area.removeFromLeft(12);
    area.removeFromRight(12);

    auto iconBar = area.removeFromTop(96);
    moduleSelector.setBounds(iconBar);
    area.removeFromTop(12);

    if (currentCard != nullptr) currentCard->setBounds(area);
    if (calibOverlay != nullptr) calibOverlay->setBounds(getLocalBounds());
}

void MainComponent::setMuted(bool m)
{
    audioEngine.setMuted(m);
    muteBtn.setToggleState(m, juce::dontSendNotification);
}

void MainComponent::setBypass(bool b)
{
    audioEngine.setBypass(b);
    bypassBtn.setToggleState(b, juce::dontSendNotification);
}

void MainComponent::rebuildCurrentCard()
{
    auto* node = audioEngine.getNode(currentNodeIndex);
    if (node == nullptr) return;

    // Cards are cached per (module, simple/advanced): switching modules used to
    // destroy and rebuild every knob, which is where the visible lag came from.
    const auto key = std::make_pair(currentNodeIndex, simpleMode);

    if (currentCard != nullptr)
    {
        removeChildComponent(currentCard.get());
        cardCache[currentCardKey] = std::move(currentCard);
    }

    auto& slot = cardCache[key];
    if (slot == nullptr)
    {
        slot = std::make_unique<NodeCard>(node, audioEngine, simpleMode);
        slot->setLargePanel(true);
        slot->onParametersChanged = [this] { markCustomPreset(); };
    }

    currentCard = std::move(slot);
    currentCardKey = key;
    addAndMakeVisible(currentCard.get());
    currentCard->refreshFromParams(); // values may have changed while it was hidden
    if (calibOverlay != nullptr) calibOverlay->toFront(false);
    resized();
}

void MainComponent::updateLanguage()
{
    bypassBtn.setButtonText(Loc::t("toolbar.bypass"));
    muteBtn.setButtonText(Loc::t("toolbar.mute"));
    simpleBtn.setButtonText(Loc::t("toolbar.simple"));
    advancedBtn.setButtonText(Loc::t("toolbar.advanced"));
    settingsBtn.setButtonText(Loc::t("toolbar.settings"));
    savePresetBtn.setButtonText(Loc::t("toolbar.save"));
    deletePresetBtn.setButtonText(Loc::t("toolbar.delete"));
    resetBtn.setButtonText(Loc::t("toolbar.reset"));
    autoCalibBtn.setButtonText(Loc::t("toolbar.autocal"));
    undoBtn.setButtonText(Loc::t("toolbar.undo"));
    statusLabel.setText(Loc::t("status.ready"), juce::dontSendNotification);

    int prevStyle = styleBox.getSelectedId();
    styleBox.clear(juce::dontSendNotification);
    styleBox.addItem(Loc::t("style.balanced"), 1);
    styleBox.addItem(Loc::t("style.full"),     2);
    styleBox.addItem(Loc::t("style.dx"),       3);
    styleBox.addItem(Loc::t("style.warm"),     4);
    styleBox.setSelectedId(prevStyle > 0 ? prevStyle : 1, juce::dontSendNotification);

    refreshPresetList();

    moduleSelector.repaint();
    rebuildCurrentCard();
    resized();
}

void MainComponent::showPreferences()
{
    juce::DialogWindow::LaunchOptions opts;
    opts.content.set(new PreferencesComponent(audioEngine), true);
    opts.dialogTitle = Loc::t("dialog.preferences");
    opts.dialogBackgroundColour = juce::Colour(0xff1a1e28);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void MainComponent::refreshPresetList()
{
    presetBox.clear(juce::dontSendNotification);
    presetBox.addItem(Loc::t("preset.default"), 1);
    int id = 2, selected = 1;
    for (const auto& name : presetManager->getPresetNames())
    {
        presetBox.addItem(name, id);
        if (name == activePresetName) selected = id;
        ++id;
    }
    if (customPreset || (activePresetName.isNotEmpty() && selected == 1))
    {
        presetBox.setSelectedId(0, juce::dontSendNotification);
        presetBox.setText(Loc::getLang() == Loc::Lang::ZH ? juce::String::fromUTF8(u8"自定义（未保存）") : "Custom (unsaved)", juce::dontSendNotification);
    }
    else presetBox.setSelectedId(selected, juce::dontSendNotification);
}

void MainComponent::syncModuleStates()
{
    for (int i = 0; i < audioEngine.getNumNodes(); ++i)
        moduleSelector.setNodeBypassed(i, audioEngine.getNode(i)->isBypassed());
}

void MainComponent::markCustomPreset()
{
    // A style is a one-shot command, not a stored state: any manual edit means
    // the combo no longer describes what is loaded.
    markStyleCustom();
    if (customPreset) return;
    activePresetName.clear(); customPreset = true;
    refreshPresetList();
}

void MainComponent::markStyleCustom()
{
    if (styleBox.getSelectedId() <= 0) return;
    styleBox.setSelectedId(0, juce::dontSendNotification);
    styleBox.setText(Loc::getLang() == Loc::Lang::ZH ? juce::String::fromUTF8(u8"自定义")
                                                     : juce::String("Custom"),
                     juce::dontSendNotification);
}

bool MainComponent::selectPreset(const juce::String& name)
{
    if (name.isEmpty()) presetManager->loadDefaults();
    else if (!presetManager->loadPreset(name))
    {
        statusLabel.setText(presetManager->getLastError(), juce::dontSendNotification);
        refreshPresetList();
        return false;
    }
    activePresetName = name; customPreset = false;
    refreshPresetList(); syncModuleStates(); rebuildCurrentCard(); saveUISettings();
    statusLabel.setText(Loc::t("status.ready"), juce::dontSendNotification);
    return true;
}

bool MainComponent::saveCurrentPreset(const juce::String& name)
{
    if (!presetManager->savePreset(name))
    {
        statusLabel.setText(presetManager->getLastError(), juce::dontSendNotification);
        return false;
    }
    activePresetName = name; customPreset = false;
    refreshPresetList(); saveUISettings();
    statusLabel.setText(Loc::getLang() == Loc::Lang::ZH ? juce::String::fromUTF8(u8"预设已保存") : "Preset saved", juce::dontSendNotification);
    return true;
}

bool MainComponent::removePreset(const juce::String& name)
{
    if (!presetManager->deletePreset(name))
    {
        statusLabel.setText(presetManager->getLastError(), juce::dontSendNotification);
        return false;
    }
    if (!customPreset && activePresetName == name)
    {
        presetManager->loadDefaults(); activePresetName.clear(); customPreset = false;
    }
    refreshPresetList(); syncModuleStates(); rebuildCurrentCard(); saveUISettings();
    statusLabel.setText(Loc::getLang() == Loc::Lang::ZH ? juce::String::fromUTF8(u8"预设已删除") : "Preset deleted", juce::dontSendNotification);
    return true;
}

void MainComponent::onPresetSelected()
{
    if (presetBox.getSelectedId() > 1) selectPreset(presetBox.getText());
    else if (presetBox.getSelectedId() == 1) selectPreset({});
}

void MainComponent::onSavePreset()
{
    juce::String initial = presetBox.getSelectedId() > 1 ? presetBox.getText() : juce::String();
    auto* aw = new juce::AlertWindow("Save Preset", "Enter preset name:",
                                     juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", initial);
    aw->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe(this);
    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe, aw](int result) {
            if (result != 1 || safe == nullptr) return;
            const auto name = aw->getTextEditorContents("name").trim();
            if (name.isEmpty()) return;
            // Never overwrite a different preset without asking.
            if (safe->presetManager->presetExists(name))
            {
                juce::Component::SafePointer<MainComponent> safeInner(safe.getComponent());
                juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::WarningIcon,
                    "Overwrite preset",
                    "Preset \"" + name + "\" already exists. Overwrite it?",
                    "Overwrite", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create([safeInner, name](int confirm) {
                        if (confirm == 1 && safeInner != nullptr) safeInner->saveCurrentPreset(name);
                    }));
                return;
            }
            safe->saveCurrentPreset(name);
        }), true);
}

void MainComponent::onDeletePreset()
{
    juce::String name;
    const int id = presetBox.getSelectedId();
    if (id > 1) name = presetBox.getText();
    else if (activePresetName.isNotEmpty()) name = activePresetName;

    if (name.isEmpty())
    {
        // Previously this branch returned silently, so the button looked broken.
        statusLabel.setText(Loc::getLang() == Loc::Lang::ZH
                                ? juce::String::fromUTF8(u8"请先在预设列表中选择要删除的预设")
                                : juce::String("Select a preset before deleting"),
                            juce::dontSendNotification);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Delete Preset",
        "Are you sure you want to delete \"" + name + "\"?",
        "Delete", "Cancel", nullptr,
        juce::ModalCallbackFunction::create([safe, name](int result) {
            if (result != 1 || safe == nullptr) return;
            safe->removePreset(name);
        }));
}

void MainComponent::onResetDefaults()
{
    presetManager->loadDefaults();
    activePresetName.clear(); customPreset = false;
    refreshPresetList();
    moduleSelector.setSelected(currentNodeIndex);
    for (int i = 0; i < audioEngine.getNumNodes(); ++i) {
        auto* node = audioEngine.getNode(i);
        if (node != nullptr) moduleSelector.setNodeBypassed(i, node->isBypassed());
    }
    rebuildCurrentCard();

    saveUISettings();
    statusLabel.setText(Loc::t("status.reset"), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe(this);
    juce::Timer::callAfterDelay(2000, [safe]() {
        if (safe == nullptr) return;
        safe->statusLabel.setText(Loc::t("status.ready"), juce::dontSendNotification);
    });
}

void MainComponent::onAutoCalibrate()
{
    if (autoCalibrator->isRunning()) {
        autoCalibrator->cancel();
        return;
    }
    // Snapshot first: a successful smart tune rewrites all ten modules.
    autoTuneUndoState = SmartTune::captureState(audioEngine);
    undoBtn.setVisible(false);
    resized();
    ++calibrationGeneration;
    calibOverlay = std::make_unique<CalibrationOverlay>();
    calibOverlay->levelGetter = [this]() { return audioEngine.getInputLevel(); };
    calibOverlay->onCancel = [this]() { autoCalibrator->cancel(); };
    addAndMakeVisible(calibOverlay.get());
    calibOverlay->setBounds(getLocalBounds());
    calibOverlay->toFront(true);
    calibOverlay->grabKeyboardFocus();
    autoCalibrator->start(8.0);
}

void MainComponent::onUndoAutoTune()
{
    if (! autoTuneUndoState) return;
    {
        const juce::ScopedLock lock(audioEngine.getDeviceManager().getAudioCallbackLock());
        SmartTune::restoreState(audioEngine, autoTuneUndoState);
    }
    autoTuneUndoState = {};
    undoBtn.setVisible(false);
    audioEngine.beginSoftTransition();
    syncModuleStates();
    rebuildCurrentCard();
    markCustomPreset();
    resized();
    statusLabel.setText(Loc::t("status.undoDone"), juce::dontSendNotification);
    statusLabel.setTooltip({});
}

void MainComponent::onStyleSelected()
{
    int id = styleBox.getSelectedId();
    if (id <= 0) return;
    StylePresets::applyStyle(audioEngine, id);
    markCustomPreset();
    rebuildCurrentCard();
}

void MainComponent::saveUISettings()
{
    if (auto* s = uiSettings) {
        s->setValue("lastPreset", activePresetName);
        s->setValue("lang", (int) Loc::getLang());
        s->setValue("simpleMode", simpleMode);
        s->setValue("selectedModule", currentNodeIndex);
        s->setValue("bypass", audioEngine.isBypass());
        s->setValue("styleId", styleBox.getSelectedId());
        // Written here as well as by the slider itself, so a failed incremental
        // save cannot lose it.
        s->setValue("waterfallGain", (double) getSharedWaterfallGain());
        // Remember the whole chain so an unsaved tweak survives the restart.
        if (presetManager != nullptr)
            s->setValue("sessionState", juce::JSON::toString(presetManager->exportState(), true));
        s->save(); // force write: this is the last chance to persist anything
    }
}

void MainComponent::loadUISettings()
{
    auto* s = uiSettings;
    if (s == nullptr) return;

    int lang = s->getIntValue("lang", 0);
    Loc::setLang(lang == 1 ? Loc::Lang::EN : Loc::Lang::ZH);
    langBox.setSelectedId(lang == 1 ? 2 : 1, juce::dontSendNotification);
    updateLanguage();

    simpleMode = s->getBoolValue("simpleMode", true);
    simpleBtn.setToggleState(simpleMode, juce::dontSendNotification);
    advancedBtn.setToggleState(!simpleMode, juce::dontSendNotification);

    // Bypass is part of the on-air state: leaving the unit in bypass must not
    // silently re-enable processing on the next launch.
    const bool bypassed = s->getBoolValue("bypass", false);
    bypassBtn.setToggleState(bypassed, juce::dontSendNotification);
    audioEngine.setBypass(bypassed);

    const int savedStyle = s->getIntValue("styleId", 1);
    styleBox.setSelectedId(juce::jlimit(0, 4, savedStyle), juce::dontSendNotification);

    int mod = s->getIntValue("selectedModule", 0);
    if (mod >= 0 && mod < audioEngine.getNumNodes()) {
        currentNodeIndex = mod;
        moduleSelector.setSelected(mod);
    }

    const auto lastPreset = s->getValue("lastPreset", "");
    if (lastPreset.isNotEmpty()) selectPreset(lastPreset);

    // 3D view gain is also restored here so it never depends on when the card was
    // first built.
    setSharedWaterfallGain((float) s->getDoubleValue("waterfallGain", 1.0));

    // No named preset was active: bring back the exact chain from last session.
    if (activePresetName.isEmpty())
    {
        const auto json = s->getValue("sessionState");
        if (json.isNotEmpty())
        {
            juce::var data;
            if (juce::JSON::parse(json, data).wasOk() && presetManager->importState(data))
            {
                syncModuleStates();
                rebuildCurrentCard();
            }
        }
    }

    // 首次启动：弹一次简短教程（只在实际运行、并且带着真实设置文件时）。
    if (devicesOpened && ! s->getBoolValue("tutorialShown", false))
    {
        s->setValue("tutorialShown", true);
        s->save();

        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync([safe]
        {
            if (safe != nullptr) showFirstRunTutorial(safe.getComponent());
        });
    }
}
