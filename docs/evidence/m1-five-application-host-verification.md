# M1 五应用宿主机 Smoke 验证证据

## 产品结果

本轮只推进 ContextIME Native IME Baseline 的应用兼容性验收：在真实 Windows 宿主机和真实 TSF 输入链路中，分别验证记事本、Microsoft Edge、Visual Studio Code、Visual Studio 2022 与 Windows Terminal 的拼音组合、候选显示、空格上屏和中英文切换。

影响链路：

```text
Physical key event
→ target application TSF client
→ ContextIME IPC
→ ContextIME Server
→ librime composition / candidates
→ application text commit
```

没有修改 ContextIME、Weasel 或 librime 的输入热路径；实现改动仅新增宿主机 smoke 工具和 PowerShell 入口。路线分类为 `native IME verification`，不是编辑器 Adapter 或原型功能。

## 固定运行身份

- ContextIME 安装包：`contextime-0.1.3-preview-installer.exe`
- 安装包大小：`12,049,788` bytes
- 安装包 SHA-256：`a1e02e04624fe9f11611dd06cb9300bdeb057886da32693b83a6a1ee10e40dd2`
- ContextIME runtime build commit：`aa80d79a659511c0a00f534011bb39698ec84e9c`
- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- TSF service：`9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9`
- TSF profile：`A200BA94-B1A7-4668-A22B-CA61EC1E79F7`

测试发生在 Windows x64 宿主机，DisplayVersion `25H2`，Build `26200.9168`。该环境不是干净 VM。

## Smoke 工具边界

新增工具：

- `native/tests/app-smoke/ContextIMEAppSmoke.cs`
- `native/scripts/run-contextime-app-smoke.ps1`

工具通过 `ITfInputProcessorProfileMgr::ActivateProfile` 和 `TF_IPPMF_FORSESSION` 激活 ContextIME Profile，测试结束后恢复原活动 Profile。输入使用系统键盘事件进入每个应用自己的 TSF 链路，不直接调用 librime API，也不修改产品 composition 状态。

证据与输入安全门禁包括：

- 每次运行前删除同一 evidence 前缀下的旧 JSON、PNG 和 trace，禁止借用旧成功证据；
- 测试前后调用已安装 ContextIME Server 的 `/nascii`，为新 session 设置中文基线；
- 获得目标前台后先发送 Escape，清除旧 composition；若调用窗口在正式输入前抢回前台，最多安全重获 3 次；
- 在拼音、空格、Shift 和 `abc` 每段按键发送前分别验证目标窗口仍为前台；
- 候选截图前后分别验证目标窗口句柄，只有两次都匹配才保存 PNG；
- Shift 英文直输验证后，在同一目标 session 再次 Shift 恢复中文；
- 退出前重新获得目标前台并发送 Escape，再关闭隔离窗口；
- 记录 Profile 激活与恢复、目标/前台句柄、焦点元素、候选像素差、commit、清理和错误数组。

正式拼音开始后若外部窗口抢占前台，工具会立即失败，不保存错误候选图，也不继续发送下一段按键。仅初始 Escape 阶段允许在尚未发送 fixture 时安全重获前台。

## 应用 Fixture

| 应用 | 隔离方式 | 文本读取 | 特殊边界 |
|---|---|---|---|
| Notepad | `%TEMP%` 独立 UTF-8 文件和对应标签页 | UI Automation `TextPattern` | 清理时重新选择自己的标签页 |
| Edge | 本地离线 HTML、InPrivate、独立 `--user-data-dir` | textarea `ValuePattern` 与隔离页标题 | 截图严格夹在目标窗口内部 |
| VS Code | 独立用户目录、独立扩展目录、禁用扩展、临时文本文件 | 保存后读取 UTF-8 文件 | 不依赖仓库 VS Code Adapter |
| Visual Studio 2022 | 带 UTF-8 BOM 的临时文本文件 | 保存后读取 UTF-8 文件 | 先插入 8 个空行下移光标，断言时只忽略这些前导换行，使完整候选窗留在隔离窗口内 |
| Windows Terminal | 唯一窗口标题的新 PowerShell 标签页 | Terminal `TextPattern` | 只输入文本，不发送 Enter，不执行命令 |

## 最终五应用结果

证据目录：`artifacts/native-evidence/m1-five-app-host-verified-20260830/`。该目录按仓库规则被忽略，不进入 Git；以下摘要、哈希和门禁结果进入版本控制。

所有应用使用：

```text
shurufa
→ 候选首项“输入法”
→ Space
→ 输入法
→ Shift
→ 输入法abc
→ Shift 恢复中文
```

| 应用版本 | 中文提交 | Shift 后文本 | 候选变化像素 | 结果 |
|---|---|---|---:|---|
| Notepad `11.2606.15.0` | `输入法` | `输入法abc` | `51,787` | `Passed: true` |
| Edge `152.0.4191.53` | `输入法` | `输入法abc` | `51,706` | `Passed: true` |
| VS Code `1.135.0` | `输入法` | `输入法abc` | `171,713` | `Passed: true` |
| Visual Studio `17.14.36804.6` | `输入法`（忽略 fixture 的 8 个前导空行） | `输入法abc` | `51,851` | `Passed: true` |
| Windows Terminal `1.24.2607.10001` | 缓冲末尾 `输入法` | 缓冲末尾 `输入法abc` | `50,808` | `Passed: true` |

每份最终 JSON 的以下字段均为 `true`，且 `Errors` 为空：

```text
ProfileActivated
ProfileRestored
ForegroundAcquired
InitialCompositionDismissed
FixturePrepared
ForegroundBeforePinyinInput
ForegroundBeforeCandidateCapture
ForegroundAfterCandidateCapture
ForegroundBeforeCommit
ForegroundBeforeEnglishSwitch
ForegroundBeforeEnglishInput
ForegroundAfterInput
CandidateWindowDetected
CommitMatched
EnglishModeMatched
ChineseModeRestored
CleanupForegroundAcquired
CompositionDismissedBeforeClose
ProbeClosed
Passed
```

五张最终 PNG 已逐张目视核对：全部包含 `shu ru fa`、1–5 中文候选和对应隔离应用内容，没有其他用户窗口内容。

## 证据哈希

| 应用 | JSON SHA-256 | PNG SHA-256 |
|---|---|---|
| Notepad | `e43582fab93d024055dc3d8f5df6da183a01c30ba4952de0feac2b71f69ba1ee` | `e86ad163c90347a95e7e4124875e38852cf847a82da1e586b808a22aeeb14f80` |
| Edge | `b88ec7f966be5fa6b0e46e96e8b550f973d5fd6fcffa0ba7329f992edbe3bece` | `47db77964e44cadc8880132e5b7d8eea47e52857bc58fdf0faf8ad184d8a6750` |
| VS Code | `dea5a5c966a9b5754f4efcf7c8c4864ca5023c438820aebffa99cedaf915f633` | `71c83bbf7f962de6860adbbd40180ec1f41621d139ebb99207fdc0d6f6d82afc` |
| Visual Studio | `f378eee4ad3b89835496d36050c5053fa379cce186a25ed2947fdd7a38c0571d` | `51f7a00df2be84daff6fb7cf1a6445865a0778971454c7fd2189430ba33efae1` |
| Terminal | `9689dc0b9f6cdbc9e4e7e776ae4fdd5f70bea2683767f654f5113f53b9d11e99` | `cb458e64e2713e209ace87e8cbcd5cc0faaf6238008deeb6abfb1844b0d4268f8` |

## 失败门禁与修复证据

开发验收中出现过三类非通过结果，均未计入最终表：

1. 外部窗口抢占前台。旧工具可能截到其他窗口；两张含外部内容的失败 PNG 已精确删除，未提交、未保留。最终工具在候选截图前后和每段按键前校验目标句柄，抢占时安全中止。
2. 新窗口继承旧 composition，实际组合曾出现 `ou shurufa` 或 `ishurufa`。最终工具在正式 fixture 前发送 Escape，并在退出前再次 Escape，避免跨测试残留。
3. Windows Terminal 复用同一宿主进程和 TSF session，单独调用 `/nascii` 不能重置既有 session 的英文态。最终工具在验证 `abc` 后向同一目标窗口再次发送 Shift；连续两个 Terminal 窗口均随后从中文候选开始并通过。

Visual Studio 还发生过一次截图前焦点抢占，门禁在保存 PNG 和发送空格之前终止该轮；受控复跑通过。其候选窗在第 1 行时会越过窗口顶边，最终 fixture 通过 8 个前导空行下移光标，既保存完整候选又不扩大到外部桌面。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| Notepad 拼音、候选、上屏、中英文 | `real_machine_verified` | 隔离临时文件、真实 TSF、完整候选 PNG、精确文本匹配 |
| Edge 拼音、候选、上屏、中英文 | `real_machine_verified` | 离线隔离页面、真实 TSF、完整候选 PNG、精确 textarea 值 |
| VS Code 拼音、候选、上屏、中英文 | `real_machine_verified` | 禁用扩展的隔离实例，不依赖 Adapter |
| Visual Studio 拼音、候选、上屏、中英文 | `real_machine_verified` | UTF-8 BOM 临时文件、完整候选、保存后文件匹配 |
| Windows Terminal 拼音、候选、上屏、中英文 | `real_machine_verified` | Terminal 缓冲后缀匹配，没有发送 Enter |
| Profile 与输入状态恢复 | `real_machine_verified` | 五轮 `ProfileRestored`、`ChineseModeRestored` 和清理门禁均为 true |
| 连续 30 分钟稳定性 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚未开始 |
| 干净 Windows 10/11 VM | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前为已有开发历史的 Windows 25H2 宿主机 |
| RDP 输入稳定性 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未执行 |

因此本轮只关闭“5 应用 smoke”。`M1_HOST_BASELINE` 仍未标记为 `VERIFIED`，也没有进入 M2。

## 复验命令

运行期间不要操作物理键盘和鼠标。脚本会短暂切换前台应用、发送测试按键，并在结束后把 ContextIME 恢复为中文基线：

```powershell
./native/scripts/run-contextime-app-smoke.ps1 -Application notepad
./native/scripts/run-contextime-app-smoke.ps1 -Application browser
./native/scripts/run-contextime-app-smoke.ps1 -Application vscode
./native/scripts/run-contextime-app-smoke.ps1 -Application visualstudio -TimeoutSeconds 60
./native/scripts/run-contextime-app-smoke.ps1 -Application terminal -TimeoutSeconds 40
```

若 ContextIME 未安装在注册表记录的 `WeaselRoot`，可显式传入：

```powershell
-ServerPath 'C:\Program Files\ContextIME\contextime-0.1.3-preview\WeaselServer.exe'
```

## 下一阻塞

按既定顺序，下一项是连续 30 分钟稳定性验收。该项通过前禁止标记 `M1_HOST_BASELINE = VERIFIED`，禁止进入 M2.1 技术英文片段。
