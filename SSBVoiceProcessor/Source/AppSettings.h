#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace AppSettings
{
    // 全局应用属性（惰性初始化）
    juce::PropertiesFile* get();

    // 实际使用的设置文件路径（可能回退到程序目录）
    juce::File getFile();
}
