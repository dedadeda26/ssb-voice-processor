#pragma once
#include <juce_core/juce_core.h>

class Loc
{
public:
    enum class Lang { ZH, EN };

    static void setLang(Lang l) { lang = l; }
    static Lang getLang() { return lang; }
    static juce::String t(const juce::String& key);

private:
    static Lang lang;
};
