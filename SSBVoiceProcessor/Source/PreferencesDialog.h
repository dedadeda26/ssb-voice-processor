#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"

class PreferencesComponent : public juce::Component
{
public:
    explicit PreferencesComponent(AudioEngine& engine);
    ~PreferencesComponent() override = default;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refreshDeviceLists();
    void refreshDeviceChoices();
    void applySettings();
    void saveStartupSettings();
    void saveWindowBehaviour();
    void loadStartupSettings();
    void styleLabel(juce::Label& lbl, bool title = false);
    void closeDialog();

    void chooseBackgroundImage();
    void clearBackgroundImage();
    void refreshBackgroundUI();

    AudioEngine& engine;
    bool initialising = true;   // suppress settings writes while building the dialog

    juce::Label audioTitle, startupTitle, windowTitle, backgroundTitle;
    juce::Label deviceTypeLabel, inputLabel, outputLabel, sampleRateLabel, bufferLabel;
    juce::ComboBox deviceTypeBox, inputBox, outputBox, sampleRateBox, bufferSizeBox;
    juce::TextButton applyBtn;
    juce::Label deviceStatusLabel;

    juce::ToggleButton autoStartToggle;

    juce::Label closeBehaviourLabel, minimiseBehaviourLabel;
    juce::ComboBox closeBehaviourBox, minimiseBehaviourBox;

    juce::TextButton chooseBgBtn, clearBgBtn;
    juce::Label      bgPathLabel;
    juce::Label      bgOpacityLabel, bgBlurLabel, bgDimLabel;
    juce::Slider     bgOpacitySlider, bgBlurSlider, bgDimSlider;

    juce::TextButton closeBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};
