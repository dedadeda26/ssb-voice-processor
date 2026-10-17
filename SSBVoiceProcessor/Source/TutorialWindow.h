#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// 首次启动时的简短教程（4 页，图文）。一个自绘的小窗口，不依赖主界面组件。
// 只在正式运行时弹出：调用方负责判断"是否第一次启动"。
void showFirstRunTutorial (juce::Component* owner);

// 离屏构建某一页（用于渲染截图 / 自检），返回的组件由调用方持有。
std::unique_ptr<juce::Component> createTutorialComponent (int startPage);
