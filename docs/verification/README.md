# 本机验证记录

这些文件来自 2026-10-17 的 macOS arm64 本机验证，目录路径已替换为 `$WORKSPACE`。

- 最新结果：`p2-build-final.log`、`p2-test-results-final.log`。
- `build/p2-ui-zh.png`、`build/p2-ui-en.png` 为最终界面截图。
- 其余日志、补丁和截图为分批修复的历史记录。
- `run_checks.py` 仅重现初审问题，需要本机原始 `SSBVoiceProcessor.rar`；该压缩包未上传。当前回归请运行项目的 CTest。
- 源码修复完成后，Windows/MSVC、真实 ASIO/WASAPI 和长时间小缓冲验证仍待执行。
