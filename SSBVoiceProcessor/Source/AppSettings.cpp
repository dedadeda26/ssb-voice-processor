#include "AppSettings.h"

namespace AppSettings
{
    namespace
    {
        // Some locked-down machines (security suites, group policy) deny writes
        // under %APPDATA%\Roaming. The old code then silently lost every setting,
        // which is exactly what "my options are not remembered" looked like. Probe
        // first, and fall back to a file next to the executable.
        bool canWriteInto(const juce::File& dir)
        {
            if (! dir.createDirectory()) return false;
            auto probe = dir.getChildFile(".write-probe");
            if (! probe.replaceWithText("x")) return false;
            probe.deleteFile();
            return true;
        }
    }

    juce::PropertiesFile* get()
    {
        static std::unique_ptr<juce::PropertiesFile> props;

        if (props == nullptr)
        {
            juce::PropertiesFile::Options opts;
            opts.applicationName = "SSBVoiceProcessor";
            opts.filenameSuffix  = ".settings";
            opts.folderName      = "SSBVoiceProcessor";

            const auto defaultFile = opts.getDefaultFile();

            // Deterministic location: the folder holding the executable, if it is
            // writable, otherwise the usual per-user folder. Choosing purely by
            // "whichever can be written right now" produced two different settings
            // files depending on how the app was launched, so the user's choices
            // appeared to be forgotten.
            const auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                    .getParentDirectory();

            juce::File file;
            if (canWriteInto(exeDir))
                file = exeDir.getChildFile("SSBVoiceProcessor.settings");
            else if (canWriteInto(defaultFile.getParentDirectory()))
                file = defaultFile;
            else
                file = defaultFile; // nothing is writable: keep the standard path

            props = std::make_unique<juce::PropertiesFile>(file, opts);
        }

        return props.get();
    }

    juce::File getFile()
    {
        if (auto* p = get()) return p->getFile();
        return {};
    }
}
