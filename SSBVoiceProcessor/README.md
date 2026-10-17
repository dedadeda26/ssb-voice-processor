# SSB Voice Processor

SSB 实时语音处理。带限模块默认 90–3100 Hz，可调节、可旁路；需要 100–2900 Hz 时可在界面设置。

构建：
    cmake -B build -G "Visual Studio 18 2026" -A x64 -DJUCE_DIR=D:/Dev/JUCE
    cmake --build build --config Release

优先问题修复后，处理链末端为 Band-Limit → Output Gain → Limiter。
启用 Limiter 时按所设阈值限制最终样本峰值；总旁路或 Limiter 旁路时，
仍保留 0 dBFS 样本峰值保护，超限信号会被截断。此保护不等同于 true-peak 限幅。

自动校准支持“取消 / Cancel”按钮和 Esc；无有效输入/输出设备时拒绝开始，
采集中连续 2 秒无数据或超过目标时长加 3 秒时退出并显示失败原因。
校准共 8 秒：前 2 秒保持安静，后 6 秒正常说话。静音、稳定底噪、语音太弱、
明显削波或无效采样会导致校准失败并保留当前参数。校准使用电平、信噪比和
有效活动时长做质量判断，不使用语音识别模型。总旁路时请先关闭旁路再校准。

离线回归测试（不打开真实声卡）：

    cmake -B build-tests -DJUCE_DIR=D:/Dev/JUCE -DSSB_BUILD_TESTS=ON
    cmake --build build-tests --config Debug
    ctest --test-dir build-tests -C Debug --output-on-failure

测试覆盖多段分频合成响应、输出增益后限幅、旁路保护、校准失败和取消、
显示队列满载恢复和并发传递。Tests/DisplayQueueTests.cpp 也可以用 Clang
单独加 -fsanitize=thread 编译，检查显示数据传递中的数据竞争。

P2 修复：

- 噪声门使用短 Attack 打开、长 Release 关闭；简单模式使用有方向的宏映射，
  并以当前高级参数为锚点，避免轻微移动时大幅跳变。
- 音频处理使用预分配缓冲区；较大的回调分块处理，FFT 保留跨帧余数。
- 频率参数与旋钮范围随设备采样率调整；切换采样率后自动刷新控件。
- 设备设置应用失败时恢复之前的有效配置；成功后保存设备、采样率与缓冲区，
  下次启动恢复。保存设备不可用时显示原因并尝试默认设备。
- 预设名称、版本和参数类型经过校验；无版本的旧格式按版本 0 迁移。
  版本 0/1 的部分预设会用默认值补齐缺少的节点和参数；未知版本被拒绝。
  有限数值按当前参数范围钳制，以适应不同采样率。加载失败不修改当前参数。
- 删除当前预设会恢复默认参数并同步模块旁路图标；手动修改参数后显示未保存状态。
- 工具栏根据窗口宽度换行，设置和语言入口始终保留。

SSB_BUILD_TESTS=ON 时运行 display_queue、priority_fixes 和 p2_fixes 三组测试。
macOS 下 p2_fixes 会通过测试专用的 dyld 插桩统计回调内的实际堆内存操作；
此动态库只用于测试，不链接进应用。Windows 下跳过这项平台专用统计，其余
功能测试仍会执行。所有设备测试使用模拟设备，不打开真实声卡。
