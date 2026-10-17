#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <utility>
#include "AudioEngine.h"
#include "Localization.h"
#include "PresetManager.h"
#include "AutoCalibrator.h"
#include "LevelMeter.h"
#include "NodeCard.h"
#include "ModuleSelector.h"
#include "CalibrationOverlay.h"

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    explicit MainComponent(bool openDevices = true, juce::File presetDirectory = {}, juce::PropertiesFile* settings = nullptr);
    bool selectPreset(const juce::String& name);
    bool saveCurrentPreset(const juce::String& name);
    bool removePreset(const juce::String& name);
    AudioEngine& getAudioEngine() { return audioEngine; }

    // Used by the tray icon menu so both entry points stay in sync.
    bool isMuted() const { return audioEngine.isMuted(); }
    bool isBypass() const { return audioEngine.isBypass(); }
    void setMuted(bool m);
    void setBypass(bool b);
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    void saveSettings();

private:
    void timerCallback() override;

    void rebuildCurrentCard();
    void updateLanguage();
    void showPreferences();
    void refreshPresetList();
    void syncModuleStates();
    void markCustomPreset();
    void markStyleCustom();
    void onPresetSelected();
    void onSavePreset();
    void onDeletePreset();
    void onResetDefaults();
    void onAutoCalibrate();
    void onUndoAutoTune();
    void onStyleSelected();

    void saveUISettings();
    void loadUISettings();

    AudioEngine audioEngine;
    juce::PropertiesFile* uiSettings = nullptr;
    juce::String activePresetName;
    bool customPreset = false;
    std::unique_ptr<PresetManager> presetManager;
    std::unique_ptr<AutoCalibrator> autoCalibrator;
    std::unique_ptr<CalibrationOverlay> calibOverlay;

    juce::Label appTitle;
    juce::ComboBox styleBox;
    juce::ComboBox presetBox;
    juce::TextButton savePresetBtn;
    juce::TextButton deletePresetBtn;
    juce::TextButton resetBtn;
    juce::TextButton autoCalibBtn;
    juce::TextButton undoBtn;
    juce::TextButton muteBtn;
    juce::TextButton bypassBtn, simpleBtn, advancedBtn, settingsBtn;
    juce::ComboBox langBox;

    ModuleSelector moduleSelector;
    std::unique_ptr<NodeCard> currentCard;
    std::map<std::pair<int, bool>, std::unique_ptr<NodeCard>> cardCache;
    std::pair<int, bool> currentCardKey { -1, true };
    int currentNodeIndex = 0;

    LevelMeter inMeter  { "IN",  [this] { return audioEngine.getInputLevel(); },  true };
    LevelMeter outMeter { "OUT", [this] { return audioEngine.getOutputLevel(); }, true };

    juce::Label statusLabel;
    bool simpleMode = true;
    bool deviceLostShown = false;
    bool devicesOpened = false;

    int lastBgVersion = -1;
    unsigned int calibrationGeneration = 0;
    SmartTune::NodeState autoTuneUndoState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
