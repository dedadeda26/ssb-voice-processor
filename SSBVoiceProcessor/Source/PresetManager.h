#pragma once
#include <juce_core/juce_core.h>
#include "AudioEngine.h"

class PresetManager
{
public:
    explicit PresetManager(AudioEngine& engine, juce::File directory = {});
    static bool isValidName(const juce::String& name);
    const juce::String& getLastError() const { return lastError; }
    juce::StringArray getPresetNames() const;
    juce::File getPresetDir() const;
    juce::File getPresetFile(const juce::String& name) const;
    bool savePreset(const juce::String& name);
    bool loadPreset(const juce::String& name);
    bool deletePreset(const juce::String& name);
    bool presetExists(const juce::String& name) const;
    void loadDefaults();

    // Whole-chain state as a var, used to remember the session between launches.
    juce::var exportState() const { return serialize(); }
    bool importState(const juce::var& data) { return deserialize(data); }

private:
    AudioEngine& engine;
    juce::File directory;
    juce::String lastError;
    bool fail(const juce::String& error) { lastError = error; return false; }
    juce::var serialize() const;
    bool deserialize(const juce::var& data);
};
