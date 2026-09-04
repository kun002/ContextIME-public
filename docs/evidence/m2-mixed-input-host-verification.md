# M2 连续混输宿主机验证证据

## 产品结果

ContextIME `0.2.3-preview` 已在同一个真实 WinForms TextBox、同一个 TSF Profile 和同一次焦点生命周期中连续完成：

```text
nihao
→ 你好
→ GameObject
→ Assets/Textures/UI
→ https://github.com
→ git status && npm test
→ shurufa
→ 输入法
→ Shift
→ abc
```

最终文本为：

```text
你好GameObjectAssets/Textures/UIhttps://github.comgit status && npm test输入法abc
```

这证明 M2.1 至 M2.4 的有限输入片段可以在中文会话中连续组合使用，命令上屏后仍恢复简体中文，随后 Shift 英文直输正常。`nihao` 的首候选和提交均为简体“你好”，不是官方小狼毫的繁体输出。

主开发机状态：

```text
M2_MIXED_INPUT_HOST = VERIFIED
```

该结论只覆盖已记录的有限标识符、路径、URL 和命令范围，不表示 M2 的设置界面、标点、配置导入导出或所有规则开关已经完成。

## 固定产品身份

- 已安装产品：`ContextIME 0.2.3 Preview`
- 安装程序：`contextime-0.2.3-preview-installer.exe`
- 文件大小：`12,054,194` bytes
- SHA-256：`8831be3c3896e7092e599f3f102d61776b3170d5b4951cb1b45a09988051131d`
- Build commit：`9a1ace7fbf4284b1dd779b6c1e4e4337700897c7`
- 原安装包 CI run：`33389352096`
- PR #16 squash commit：`61734d10a6b93e0776d44d19ade75bb81112217f`
- Windows：Windows 10 Pro，DisplayVersion `25H2`，Build `26200.9168`
- TSF smoke schema：`contextime.tsf-smoke.v6`
- Authenticode：`NotSigned`

最终用户目录中以下实验覆盖均不存在：

```text
%APPDATA%\ContextIME\rime.lua
%APPDATA%\ContextIME\contextime_developer.schema.yaml
%APPDATA%\ContextIME\contextime_developer.custom.yaml
```

结果来自已安装的固定 `0.2.3-preview` 包，不是临时用户 schema 或 Lua。

## 同会话 sequence 门禁

原有 TSF smoke 只能表达“一段输入 + 一次中文恢复”。本轮在测试工具中增加可选 JSON sequence，复用同一套：

- `ActivateLanguageProfile`；
- WinForms TextBox；
- 前台焦点获取和保持检查；
- ASCII 按键注入；
- 候选像素差检测和截图；
- commit 累计文本检查；
- Caps Lock 归一化和恢复；
- Server 窗口与进程证据。

sequence 不关闭窗口、不重新激活 Profile、不清空 TextBox。每一步保存 `TextBefore`、`TextAfter`、提交键、候选变化像素、候选截图、焦点状态和 `Passed`。fixture 位于：

```text
native/tests/tsf-smoke/fixtures/m2-mixed-input.json
```

这只是测试能力扩展，没有修改 ContextIME、Weasel、librime、TSF、IPC、schema 或产品 Lua 运行时。

## 重启后最终真机结果

主机于 `2026-09-01T08:54:02.5000000+08:00` 启动。ContextIME Server `PID 20172` 于 `08:54:44` 从以下固定安装目录启动：

```text
C:\Program Files\ContextIME\contextime-0.2.3-preview\WeaselServer.exe
```

重启后的连续混输从 `2026-09-01T01:38:17Z` 开始，六步结果如下：

| 步骤 | 输入与提交键 | 累计提交结果 | 候选变化像素 | 焦点保持 | 结果 |
|---|---|---|---:|---|---|
| 简体中文前缀 | `nihao` + Space | `你好` | `9,438` | `true` | `Passed: true` |
| 技术标识符 | `GameObject` + Space | `你好GameObject` | `4,618` | `true` | `Passed: true` |
| 路径 | `Assets/Textures/UI` + Space | `你好GameObjectAssets/Textures/UI` | `6,634` | `true` | `Passed: true` |
| URL | `https://github.com` + Space | 继续原样累计 | `6,502` | `true` | `Passed: true` |
| 命令 | `git status && npm test` + Enter | 继续原样累计 | `21,916` | `true` | `Passed: true` |
| 简体中文恢复 | `shurufa` + Space | 末尾追加 `输入法` | `10,588` | `true` | `Passed: true` |

随后 Shift + `abc` 得到完整最终文本。JSON 记录：

```text
SequenceMatched: true
EnglishModeMatched: true
ProfileActivated: true
CapsLockRestored: true
Passed: true
Errors: []
```

证据位于：

```text
artifacts/native-evidence/m2-mixed-input-0.2.3-final/post-reboot/
```

其中包含 sequence JSON、trace、六张逐步候选截图和原生安装审计。

## 重启前重复结果

在重启前，同一 fixture 已连续独立运行三次，三次均得到完全相同的最终文本，且每次：

```text
Steps: 6
AllStepsPassed: true
EnglishModeMatched: true
Passed: true
```

证据根目录：

```text
artifacts/native-evidence/m2-mixed-input-0.2.3-final/
```

因此总计四次完整 sequence 通过，其中一次明确发生在系统重启后。

## 截图人工核对

逐步截图已人工检查：

- `nihao` 显示首候选 `1. 你好`；
- 技术标识符和路径显示原样 raw composition；
- URL 显示原样 URL composition；
- 命令显示 `git status && npm test` 和 `〔命令·Enter 上屏〕`；
- 命令提交后的 `shurufa` 显示首候选 `1. 输入法`。

截图不包含源码、密码、Token、环境变量值或其他私有输入正文。

## 重启后安装与并存审计

只读原生审计在重启后确认：

```text
machine registry: true
uninstall registry: true
install directory: true
TSF CLSID/TIP/Profile: true
system TSF DLL: true
ContextIME Server: true
ContextIME IPC Pipe: true
user data directory: true
Mismatches: 0
```

sequence 同时观察到：

```text
C:\Program Files\ContextIME\contextime-0.2.3-preview\WeaselServer.exe
C:\Program Files\Rime\weasel-0.17.4\WeaselServer.exe
```

以及独立 `ContextIMEIPCWindow_1.0` 与 `WeaselIPCWindow_1.0`。当前 ContextIME 进程只有 INFO 日志文件，没有对应 WARNING/ERROR 日志；Lua `func type: nil` 和 `attempt to call a nil value` 匹配数为 `0`。

## 测试框架边界修复记录

两次未通过探测均保留，没有删除或改写：

1. 第一轮沿用单段测试的总进程超时，在第三步后被包装器终止。产品前三步均已提交正确；修复为根据 sequence 步数计算测试进程总超时。
2. 第二轮 URL 已正确累计提交，但长文本把候选窗口移动到 x=1600，旧的 480 px 截图区域到 x=1438 为止，因此误报候选未检测。修复为采集测试窗口完整 680 px 客户区宽度；同一产品包随即通过 URL 和全部后续步骤。

失败证据分别位于：

```text
artifacts/native-evidence/m2-mixed-input-0.2.3-final/wrapper-timeout/
artifacts/native-evidence/m2-mixed-input-0.2.3-final/candidate-capture-boundary/
```

这两项修复只调整测试超时和截图范围，没有为通过测试而修改产品行为。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| 多步骤 TSF smoke sequence | `implemented` | 测试工具和固定 JSON fixture，不进入产品运行时 |
| 测试工具编译和单段兼容回归 | `statically_verified` | Windows PowerShell `Add-Type` 编译通过；旧单段 `输入法abc` 通过 |
| 重启前连续混输三次 | `real_machine_verified` | 主开发机交互桌面，同一 TSF 会话内六步 |
| 重启后安装、Server、Pipe、Profile | `runtime_verified` | 原生审计 `Mismatches: 0` |
| 重启后连续混输和简体恢复 | `real_machine_verified` | 主开发机交互桌面，最终 `Passed: true` |
| 与官方小狼毫进程和 IPC 并存 | `runtime_verified` | 重启后双 Server、双 IPC Window；双向真实输入已在 M2.4 验证 |
| M2 有限片段连续混输主机门禁 | `VERIFIED` | `M2_MIXED_INPUT_HOST = VERIFIED` |
| 修改命令规则无需重装 | `statically_verified` | custom 配置路径已实现，本轮未修改用户配置做真机热部署 |
| 干净 Windows 10/11 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前主机有开发和安装历史 |
| 独立 LAN 笔记本 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 未连接、未触发 |
| RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 未执行 |
| 安装程序签名 | `unverified` | Preview 为 `NotSigned` |

## 结论与下一项

M2.1 技术标识符、M2.2 有限路径、M2.3 有限 URL 和 M2.4 固定命令根已经通过同一真实 TSF 会话的连续混输，并在重启后复验。`M2_MIXED_INPUT_HOST` 可以关闭。

本轮没有扩大各片段 recognizer 的已知边界，也没有开始 Context Engine、VS Code Adapter、项目词库或个性化。下一项先收口本测试门禁的 PR；合并后再按既定执行顺序进入 M3 Context Engine。
