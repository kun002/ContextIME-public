# Windows Runtime

## 目标

`@context-ime/windows-runtime` 负责低延迟读取和切换 Windows 中英文状态。它不解析代码、不保存输入正文，也不包含拼音词库。

## 复用与改进

运行时固定参考 `CI124/auto-ime@3873421e6ef2361f7f78324c02c378ed0e6ceca5` 的 `koffi + user32.dll + imm32.dll` 路线：

- 键盘布局切换使用 `PostMessageW(WM_INPUTLANGCHANGEREQUEST)`。
- 系统状态读取优先使用当前前台窗口和线程的键盘布局。
- 中文键盘布局内部的中英文状态通过 IMM32 Open Status 判断。
- 当前窗口没有直接输入上下文时，回退到默认 IME 窗口。

## 支持策略

### 双键盘布局

系统同时安装英语（美国）和中文键盘布局：

- 英文：切到英语布局。
- 中文：切到中文布局，并在布局到达后确认 IME 已打开。

### 单中文布局

系统只有中文键盘布局，但当前窗口可读取 IMM32 Open Status：

- 英文：关闭 IME。
- 中文：打开 IME。

该路径能识别微软拼音中通过 Shift 临时进入英文的状态。实际兼容性仍需要在目标 Windows、输入法版本和 RDP 客户端组合下验证。

## 稳定保护

- 自动切换后设置短暂抑制窗口，避免异步布局尚未生效时把旧状态误判为手动操作。
- 自动切换超时标记为 `unknown`，不会直接进入习惯学习。
- RDP 默认使用较低轮询频率，减少远程会话和 Electron 输入焦点压力。
- 原生运行时加载失败、布局不完整或当前输入上下文拒绝切换时，VS Code 自动回退到 `DryRunBridge`。

## 当前限制

- 尚未接入 TSF Composition 状态，因此不能可靠知道候选词是否正在组合；策略层已有保护接口，但真实信号仍待实现。
- 尚未接入上游 Tree-sitter AST，当前代码/注释检测仍采用保守回退。
- 当前开发构建通过 TypeScript workspace 运行；正式 VSIX 还需要验证 `koffi` 原生模块的打包和加载路径。
- Windows CI 只能验证 DLL 绑定和基础读取，不等同于真实微软拼音、VS Code、Codex 和 RDP 的长时间验收。

## 诊断

在 VS Code 命令面板运行：

```text
ContextIME: Show Runtime Status
```

输出包括：

- 运行时是否就绪。
- 双布局、单布局或不可用策略。
- 当前、英文、中文 Language ID。
- IME Open Status。
- 当前轮询间隔。
