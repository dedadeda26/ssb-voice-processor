#include "Localization.h"
#include <map>

Loc::Lang Loc::lang = Loc::Lang::ZH;

namespace {
    struct Pair { const wchar_t* zh; const char* en; };

    static const std::map<juce::String, Pair>& dict()
    {
        static const std::map<juce::String, Pair> d = {
            { "app.title",        { L"SSB 语音处理器",   "SSB Voice Processor" } },
            { "toolbar.preset",   { L"预设",             "Preset" } },
            { "toolbar.bypass",   { L"原声",             "Bypass" } },
            { "toolbar.mute",     { L"静音",             "Mute" } },
            { "toolbar.simple",   { L"简单",             "Simple" } },
            { "toolbar.advanced", { L"高级",             "Advanced" } },
            { "toolbar.lang",     { L"语言",             "Language" } },
            { "toolbar.settings", { L"设置",             "Settings" } },
            { "toolbar.save",     { L"保存",             "Save" } },
            { "toolbar.reset",    { L"重置",             "Reset" } },
            { "toolbar.delete",   { L"删除预设",         "Delete" } },
            { "toolbar.autocal",  { L"自适应调参",       "ADAPTIVE" } },
            { "toolbar.undo",     { L"撤销调参",         "Undo" } },
            { "preset.default",   { L"默认",             "Default" } },
            { "panel.chain",      { L"处理链路",         "Processing Chain" } },
            { "status.ready",     { L"就绪",             "Ready" } },
            { "status.reset",     { L"已恢复默认设置",   "Reset to defaults" } },
            { "status.undoDone",  { L"已撤销调参，参数已恢复", "Setup reverted" } },
            { "status.deviceLost",{ L"音频设备已断开，请在设置里重新选择", "Audio device lost - reselect it in Preferences" } },
            { "meter.in",         { L"输入",             "IN" } },
            { "meter.out",        { L"输出",             "OUT" } },

            { "style.balanced",   { L"平衡",             "Balanced" } },
            { "style.full",       { L"饱满",             "Full" } },
            { "style.dx",         { L"DX 穿透",          "DX Punch" } },
            { "style.warm",       { L"温暖广播",         "Warm Broadcast" } },

            { "dialog.preferences", { L"设置",           "Preferences" } },
            { "device.panel",     { L"音频设备",         "Audio Devices" } },
            { "device.type",      { L"驱动类型",         "Driver Type" } },
            { "device.input",     { L"输入设备",         "Input Device" } },
            { "device.output",    { L"输出设备",         "Output Device" } },
            { "device.sampleRate",{ L"采样率",           "Sample Rate" } },
            { "device.buffer",    { L"缓冲大小",         "Buffer Size" } },
            { "device.apply",     { L"应用",             "Apply" } },

            { "node.inputGain",   { L"输入增益",         "INPUT GAIN" } },
            { "node.hpf",         { L"高通滤波",         "HIGH-PASS" } },
            { "node.gate",        { L"噪声门",           "NOISE GATE" } },
            { "node.spectralNr",  { L"频谱降噪",         "SPECTRAL NR" } },
            { "node.eq",          { L"EQ",               "EQ" } },
            { "node.mbComp",      { L"多段压缩",         "MULTI-BAND" } },
            { "node.comp",        { L"主压缩",           "COMPRESSOR" } },
            { "node.sat",         { L"饱和",             "SATURATION" } },
            { "node.deEsser",     { L"去齿音",           "DE-ESSER" } },
            { "node.limiter",     { L"限幅器",           "LIMITER" } },
            { "node.autoLevel",   { L"自动电平",         "AUTO LEVEL" } },
            { "node.bandLimit",   { L"SSB 带限",         "SSB BAND" } },
            { "node.outputGain",  { L"输出增益",         "OUTPUT GAIN" } },

            { "macro.tone",       { L"音色",             "TONE" } },
            { "macro.force",      { L"力度",             "FORCE" } },
            { "macro.punch",      { L"冲击力",           "PUNCH" } },
            { "macro.warmth",     { L"温暖",             "WARMTH" } },
            { "macro.sibilance",  { L"齿音抑制",         "DE-ESS" } },
            { "macro.gate",       { L"抑噪",             "GATE" } },

            { "param.gain",       { L"增益",             "Gain" } },
            { "param.cutoff",     { L"低切",             "Low Cut" } },
            { "param.lowGain",    { L"温暖",             "Warmth" } },
            { "param.lowFreq",    { L"低频",             "Low Freq" } },
            { "param.midGain",    { L"清晰",             "Clarity" } },
            { "param.midFreq",    { L"中频",             "Mid Freq" } },
            { "param.midQ",       { L"中频Q",            "Mid Q" } },
            { "param.highGain",   { L"临场",             "Presence" } },
            { "param.highFreq",   { L"高频",             "High Freq" } },
            { "param.threshold",  { L"阈值",             "Threshold" } },
            { "param.ratio",      { L"冲击力",           "Punch" } },
            { "param.attack",     { L"启动",             "Attack" } },
            { "param.release",    { L"释放",             "Release" } },
            { "param.knee",       { L"拐点",             "Knee" } },
            { "param.makeup",     { L"补偿",             "Makeup" } },
            { "param.lowAmount",  { L"低频",             "Low" } },
            { "param.midAmount",  { L"中频",             "Mid" } },
            { "param.highAmount", { L"高频",             "High" } },
            { "param.crossLow",   { L"分频低",           "X-Over Low" } },
            { "param.crossHigh",  { L"分频高",           "X-Over High" } },
            { "param.mbThresh",   { L"门限",             "Threshold" } },
            { "param.drive",      { L"驱动",             "Drive" } },
            { "param.mix",        { L"混合",             "Mix" } },
            { "param.output",     { L"输出",             "Output" } },
            { "param.deEssFreq",  { L"齿音频率",         "Sib Freq" } },
            { "param.deEssThresh",{ L"齿音阈值",         "Sib Thresh" } },
            { "param.deEssAmount",{ L"齿音",             "De-Ess" } },
            { "param.gateThresh", { L"门限",             "Gate Thresh" } },
            { "param.gateAmount", { L"抑噪",             "Gate Amount" } },
            { "param.gateAttack", { L"启动",             "Attack" } },
            { "param.gateRelease",{ L"释放",             "Release" } },
            { "param.gateHold",   { L"保持",             "Hold" } },
            { "param.nrAmount",   { L"降噪量",           "NR Amount" } },
            { "param.nrReduction",{ L"最大衰减",         "Max Cut" } },
            { "param.limitThresh",{ L"限制阈值",         "Limit Thresh" } },
            { "param.limitRelease",{ L"释放",            "Release" } },
            { "param.agcTarget", { L"目标电平",         "Target" } },
            { "param.agcSpeed",  { L"速度",             "Speed" } },
            { "param.agcMax",    { L"最大增益",         "Max Gain" } },
            { "param.outGain",    { L"增益",             "Gain" } },

            { "param.bandLow",    { L"低端截止",         "Low Cutoff" } },
            { "param.bandHigh",   { L"高端截止",         "High Cutoff" } },

            { "help.inputGain", { L"输入增益 — 调整麦克风信号的整体增益。3D 频谱瀑布显示实时信号。",
                                  "Input Gain — Adjust the overall gain of the microphone signal. The 3D spectrum waterfall shows the live signal." } },
            { "help.hpf",       { L"高通滤波 — 切掉低频隆隆声和呼吸噪声。SSB 建议 100 Hz 左右。",
                                  "High-Pass — Removes low-frequency rumble and breath noise. 100 Hz is a good starting point for SSB." } },
            { "help.gate",      { L"噪声门 — 低于门限的安静段被衰减。横条为输入电平，红线为门限位置。",
                                  "Noise Gate — Attenuates quiet passages below the threshold. The bar shows input level, the red line marks the threshold." } },
            { "help.eq",        { L"EQ — 动态频响视图。背景为实时频谱，亮线为当前 EQ 曲线。",
                                  "EQ — Dynamic frequency response view. The background is the live spectrum, the bright curve is the current EQ response." } },
            { "help.mbComp",    { L"多段压缩 — 每个频段独立压缩，显示 IN/OUT/GR 三个电平。",
                                  "Multi-Band — Each band is compressed independently. IN/OUT/GR levels are shown per band." } },
            { "help.comp",      { L"主压缩 — 左侧为压缩曲线，右侧为 IN/OUT/GR 电平。",
                                  "Compressor — Left: compression curve. Right: IN/OUT/GR levels." } },
            { "help.sat",       { L"饱和 — 软饱和产生温暖和谐波。曲线为传递函数。",
                                  "Saturation — Soft saturation adds warmth and harmonics. The curve shows the transfer function." } },
            { "help.deEsser",   { L"去齿音 — 动态频响视图，齿音频率处实时陷波。",
                                  "De-Esser — Dynamic frequency response view with a live notch at the sibilance frequency." } },
            { "help.limiter",   { L"限幅器 — 峰值限幅防止削波。红线为限制上限。",
                                  "Limiter — Peak limiting prevents clipping. The red lines mark the ceiling." } },
            { "help.spectralNr",{ L"频谱降噪 — 用一键智调录到的底噪谱做谱减；没有底噪谱时完全不处理。",
                                  "Spectral NR — Subtracts the noise profile captured during Auto Tune; transparent until a profile exists." } },
            { "help.bandLimit", { L"SSB 带限 — 默认 90-3100 Hz，匹配 SSB 通带。可手动调节上下限。",
                                  "SSB Band-Limit — Default 90-3100 Hz, matched to SSB passband. Cutoffs are adjustable." } },
            { "help.outputGain",{ L"输出增益 — 最终输出增益。3D 频谱瀑布显示实时信号。",
                                  "Output Gain — Final output gain. The 3D spectrum waterfall shows the live signal." } },
        };
        return d;
    }
}

juce::String Loc::t(const juce::String& key)
{
    auto& d = dict();
    auto it = d.find(key);
    if (it == d.end()) return key;
    if (lang == Lang::ZH)
        return juce::String(it->second.zh);
    return juce::String(it->second.en);
}
