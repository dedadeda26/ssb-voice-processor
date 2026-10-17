#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace UiStyle
{
    // 背景
    const juce::Colour bgTop    { 0xff212836 };
    const juce::Colour bgBottom { 0xff131822 };

    // 卡片
    const juce::Colour cardTop    { 0xff2b3243 };
    const juce::Colour cardBottom { 0xff20263a };
    const juce::Colour cardBorder { 0xff3c4459 };
    const juce::Colour cardHighlight { 0xff4a5268 };

    // 半透明覆盖层用中性色（有背景图时使用，不污染图片色相）
    const juce::Colour overlayBlackStrong { 0xb8000000 };  // 72% 黑
    const juce::Colour overlayBlackMed    { 0x78000000 };  // 47% 黑
    const juce::Colour overlayBlackSoft   { 0x50000000 };  // 31% 黑

    // 文字
    const juce::Colour text       { 0xfff0f4fb };
    const juce::Colour textDim    { 0xffa8b3c8 };
    const juce::Colour textFaint  { 0xff707b90 };

    // 主强调色（蓝）
    const juce::Colour accent     { 0xff5eb8ff };
    const juce::Colour accentGlow { 0xff7eccff };
    const juce::Colour accentSoft { 0xff2c6fb8 };

    // 新增：分类色
    const juce::Colour accentIO    { 0xff5eb8ff };   // 蓝 —— 输入/输出
    const juce::Colour accentDyn   { 0xffff9f4a };   // 橙 —— 动态
    const juce::Colour accentTone  { 0xffb58cff };   // 紫 —— 音色
    const juce::Colour accentClean { 0xff56dcc4 };   // 青 —— 处理
    const juce::Colour accentWarm  { 0xffff6b9d };   // 粉 —— 温暖/饱和

    // 状态
    const juce::Colour onGreen    { 0xff3ddc84 };
    const juce::Colour offGray    { 0xff4a5268 };

    // Knob
    const juce::Colour knobTop    { 0xff3a4255 };
    const juce::Colour knobMid    { 0xff262c3b };
    const juce::Colour knobBottom { 0xff161b27 };
    const juce::Colour knobRing   { 0xff404758 };
    const juce::Colour knobTickOff{ 0xff434b5e };
    const juce::Colour knobPtr    { 0xfff6f8fd };

    // 电平表
    const juce::Colour meterBg    { 0xff151a24 };
    const juce::Colour meterLow   { 0xff3ddc84 };
    const juce::Colour meterMid   { 0xfff4c542 };
    const juce::Colour meterHigh  { 0xffef4444 };

    // 自动校准按钮
    const juce::Colour autocalTop { 0xff7eccff };
    const juce::Colour autocalBot { 0xff3a8ee0 };

    constexpr int topBarHeight = 52;
    constexpr int cardGap      = 18;

    // 按模块名返回分类色
    inline juce::Colour categoryColour(const juce::String& nodeName)
    {
        if (nodeName == "Input Gain" || nodeName == "Output Gain")
            return accentIO;
        if (nodeName == "Noise Gate" || nodeName == "Multi-Band"
            || nodeName == "Compressor" || nodeName == "Limiter")
            return accentDyn;
        if (nodeName == "EQ" || nodeName == "De-Esser")
            return accentTone;
        if (nodeName == "High-Pass" || nodeName == "Band-Limit")
            return accentClean;
        if (nodeName == "Saturation")
            return accentWarm;
        return accentGlow;
    }
}
