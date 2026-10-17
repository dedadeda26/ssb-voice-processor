#include "PresetManager.h"
#include <cmath>
#include <limits>
#include <set>

PresetManager::PresetManager(AudioEngine& e, juce::File dir) : engine(e), directory(dir)
{
    if (directory == juce::File{})
        directory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("SSBVoiceProcessor").getChildFile("Presets");
}

juce::File PresetManager::getPresetDir() const { return directory; }
bool PresetManager::isValidName(const juce::String& name)
{
    if (name.isEmpty() || name.length() > 80 || name != name.trim()
        || name == "." || name == ".." || name.endsWithChar('.')) return false;
    for (auto c : name)
        if (c < 32 || juce::String("<>:\"/\\|?*").containsChar(c)) return false;
    const auto stem = name.upToFirstOccurrenceOf(".", false, false).toUpperCase();
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    if (stem.length() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT"))
        && ((stem[3] >= '1' && stem[3] <= '9') || stem[3] == 0xb9 || stem[3] == 0xb2 || stem[3] == 0xb3)) return false;
    return true;
}
juce::File PresetManager::getPresetFile(const juce::String& name) const
{
    if (!isValidName(name)) return {};
    const auto file = directory.getChildFile(name + ".json");
    return file.getParentDirectory() == directory && !file.isSymbolicLink() ? file : juce::File{};
}
juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    if (!directory.isDirectory()) return names;
    for (const auto& entry : juce::RangedDirectoryIterator(directory, false, "*.json"))
    {
        const auto file = entry.getFile();
        const auto name = file.getFileNameWithoutExtension();
        if (file.existsAsFile() && !file.isSymbolicLink() && isValidName(name)) names.add(name);
    }
    names.sort(true);
    return names;
}
juce::var PresetManager::serialize() const
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", 1);
    juce::Array<juce::var> nodesArr;
    for (int i = 0; i < engine.getNumNodes(); ++i)
    {
        auto* node = engine.getNode(i);
        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("id", node->getName());
        object->setProperty("bypassed", node->isBypassed());
        juce::DynamicObject::Ptr parameters = new juce::DynamicObject();
        for (const auto& p : node->getParameters())
            if (p.param) parameters->setProperty(p.id, (double)p.param->get());
        object->setProperty("params", juce::var(parameters.get()));
        nodesArr.add(juce::var(object.get()));
    }
    root->setProperty("nodes", nodesArr);
    return juce::var(root.get());
}
bool PresetManager::deserialize(const juce::var& data)
{
    if (!data.isObject()) return fail("Preset root must be an object");
    const auto version = data.getProperty("version", 0);
    if ((!version.isInt() && !version.isInt64()) || ((int)version != 0 && (int)version != 1))
        return fail("Unsupported preset version");
    // Version 0 is the legacy name/params format without a version tag.
    const auto nodesValue = data.getProperty("nodes", {});
    const auto* array = nodesValue.getArray();
    if (!array || array->isEmpty()) return fail("Preset contains no nodes");
    struct Pending { DspNode* node; bool bypassed = false; std::vector<std::pair<Param*, float>> values; };
    std::vector<Pending> pending;
    // Missing nodes/parameters in legacy or partial presets start from defaults,
    // not from whichever preset happened to be active before the load.
    for (int i = 0; i < engine.getNumNodes(); ++i)
    {
        auto* node = engine.getNode(i);
        Pending state { node, false, {} };
        for (const auto& p : node->getParameters())
            if (p.param) state.values.emplace_back(p.param, p.param->getDefault());
        pending.push_back(std::move(state));
    }
    std::set<juce::String> seen;
    for (const auto& value : *array)
    {
        const auto* object = value.getDynamicObject();
        if (!object) return fail("Preset node must be an object");
        const auto id = object->getProperty("id");
        if (!id.isString() || !seen.insert(id.toString()).second) return fail("Invalid or duplicate node id");
        auto state = std::find_if(pending.begin(), pending.end(), [&](const Pending& p) { return p.node->getName() == id.toString(); });
        if (state == pending.end()) return fail("Unknown preset node: " + id.toString());
        const auto bypass = object->getProperty("bypassed");
        if (!bypass.isBool() && !((int)version == 0 && bypass.isVoid())) return fail("Bypass must be a boolean");
        state->bypassed = bypass.isBool() && (bool)bypass;
        const auto parametersValue = object->getProperty("params");
        const auto* parameters = parametersValue.getDynamicObject();
        if (!parameters) return fail("Node params must be an object");
        const auto descriptors = state->node->getParameters();
        for (const auto& property : parameters->getProperties())
        {
            const auto parameter = std::find_if(descriptors.begin(), descriptors.end(), [&](const auto& p) { return p.id == property.name.toString(); });
            if (parameter == descriptors.end() || !parameter->param) return fail("Unknown parameter: " + property.name.toString());
            const auto& number = property.value;
            if (!number.isInt() && !number.isInt64() && !number.isDouble()) return fail("Parameter must be numeric");
            const double numeric = (double)number;
            if (!std::isfinite(numeric) || std::abs(numeric) > std::numeric_limits<float>::max())
                return fail("Parameter must be finite and representable");
            // Clamp valid numeric values to the current device's parameter range.
            const auto range = parameter->param->getRange();
            const float result = juce::jlimit(range.minimum, range.maximum, (float)numeric);
            for (auto& p : state->values) if (p.first == parameter->param) p.second = result;
        }
    }
    // Validation is complete. The audio callback sees either complete state.
    const juce::ScopedLock lock(engine.getDeviceManager().getAudioCallbackLock());
    for (const auto& state : pending)
    {
        for (const auto& p : state.values) p.first->set(p.second);
        state.node->setBypassed(state.bypassed);
    }
    engine.beginSoftTransition();
    return true;
}
bool PresetManager::savePreset(const juce::String& name)
{
    lastError.clear();
    const auto file = getPresetFile(name);
    if (file == juce::File{}) return fail("Invalid preset name or path");
    const auto created = directory.createDirectory();
    if (created.failed()) return fail(created.getErrorMessage());
    juce::var data;
    {
        const juce::ScopedLock lock(engine.getDeviceManager().getAudioCallbackLock());
        data = serialize();
    }
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data, true))
        || !temporary.overwriteTargetFileWithTemporary()) return fail("Could not write preset: " + file.getFullPathName());
    return true;
}
bool PresetManager::loadPreset(const juce::String& name)
{
    lastError.clear();
    const auto file = getPresetFile(name);
    if (file == juce::File{} || !file.existsAsFile()) return fail("Preset not found or invalid name");
    if (file.getSize() > 1024 * 1024) return fail("Preset file is too large");
    juce::FileInputStream input(file);
    if (input.failedToOpen()) return fail(input.getStatus().getErrorMessage());
    juce::var data;
    const auto parsed = juce::JSON::parse(input.readEntireStreamAsString(), data);
    if (parsed.failed()) return fail(parsed.getErrorMessage());
    return deserialize(data);
}
bool PresetManager::presetExists(const juce::String& name) const
{
    const auto file = getPresetFile(name);
    return file != juce::File{} && file.existsAsFile();
}

bool PresetManager::deletePreset(const juce::String& name)
{
    lastError.clear();
    const auto file = getPresetFile(name);
    if (file == juce::File{} || !file.existsAsFile() || !file.deleteFile()) return fail("Could not delete preset");
    return true;
}
void PresetManager::loadDefaults()
{
    lastError.clear();
    const juce::ScopedLock lock(engine.getDeviceManager().getAudioCallbackLock());
    for (int i = 0; i < engine.getNumNodes(); ++i)
    {
        auto* node = engine.getNode(i);
        node->setBypassed(false);
        for (auto& p : node->getParameters()) if (p.param) p.param->set(p.param->getDefault());
    }
    engine.beginSoftTransition();
}
