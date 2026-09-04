# M1 数字选词与候选翻页宿主机验证证据

## 产品结果

本轮只推进 ContextIME Native IME Baseline 的候选交互验收：在真实 Windows TSF 输入链路中验证数字选词、候选翻页、翻页后数字选词和文本上屏。

影响链路：

```text
Key Event
→ TSF Client
→ ContextIME IPC
→ ContextIME Server
→ librime Candidate Selection / Paging
→ Commit
```

没有修改 ContextIME、Weasel 或 librime 的输入热路径；实现改动仅扩展宿主机 TSF smoke 工具和 PowerShell 入口。默认空格提交行为保持不变。

## 固定运行身份

- ContextIME 安装包：`contextime-0.1.3-preview-installer.exe`
- 安装包大小：`12,049,788` bytes
- 安装包 SHA-256：`a1e02e04624fe9f11611dd06cb9300bdeb057886da32693b83a6a1ee10e40dd2`
- ContextIME build commit：`aa80d79a659511c0a00f534011bb39698ec84e9c`
- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- TSF service：`9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9`
- TSF profile：`A200BA94-B1A7-4668-A22B-CA61EC1E79F7`

本轮测试发生在 Windows x64 宿主机，DisplayVersion `25H2`，Build `26200.9168`。该环境不是干净 VM。

## 工具扩展

`native/tests/tsf-smoke/ContextIMETsfSmoke.cs` 与 `native/scripts/run-contextime-tsf-smoke.ps1` 新增：

- `SelectionKey` / `--selection-key`：支持 `space` 或数字键 `1` 至 `9`；
- `PageDownCount` / `--page-down-count`：使用已部署方案绑定的 `=` 键执行候选翻页；
- 翻页前与翻页后的独立 PNG；
- 两页候选区域的像素差与门禁结果；
- 选择动作后的前台窗口和输入焦点断言；
- JSON schema `contextime.tsf-smoke.v2`。

工具继续使用普通 WinForms `TextBox`、真实 TSF Profile 和系统键盘事件，不直接调用 librime API。

## Fixture 边界

最初使用 `shurufa` 的第 2 候选“输入”做数字选词 fixture。真实截图表明该候选只覆盖 `shuru`，选择后 `fa` 仍在 composition 中，因此不能将当时尚未产生完整 commit 误判成数字键失效。

最终改用单音节 `shi`，保证当前页候选覆盖完整 composition。librime 会根据用户选择实时调整候选排序，所以自动门禁不固定断言某个汉字；证据由以下三项共同组成：

1. JSON 明确记录实际 `SelectionKey` 和非空 `CommittedText`；
2. 同一次运行在提交前保存候选 PNG；
3. 人工核对截图中的候选编号与实际 commit 一致。

翻页门禁还要求第一页与第二页截图均落盘，且候选区域变化不少于 `1500` 像素。

## 数字选词结果

证据目录：`artifacts/native-evidence/m1-number-selection-shi-discovery/`。

- 输入：`shi`；
- 选择键：数字键 `2`；
- 提交前截图第 2 候选：`时`；
- 实际 commit：`时`；
- Shift 后最终文本：`时abc`；
- 候选检测：`screen-pixel-delta`；
- 候选区域变化：`43,783` pixels；
- 选择后测试窗仍为前台：`true`；
- 选择后 TextBox 仍有焦点：`true`；
- `CommitMatched: true`；
- `EnglishModeMatched: true`；
- `Passed: true`。

结论：数字键 `2` 在真实 TSF composition 中选择了截图所示的第二候选并完成上屏。

## 翻页与翻页后数字选词结果

证据目录：`artifacts/native-evidence/m1-candidate-paging-number-key-host-verified/`。

- 输入：`shi`；
- 翻页键：`=`，次数 `1`；
- 第一页截图首项：`是`；
- 第二页截图首项：`市`；
- 两页候选区域变化：`3,435` pixels；
- 翻页后选择键：数字键 `1`；
- 实际 commit：`市`；
- Shift 后最终文本：`市abc`；
- `CandidatePageChanged: true`；
- `CommitMatched: true`；
- `EnglishModeMatched: true`；
- `Passed: true`。

结论：`=` 触发了候选翻页，第二页候选与第一页不同；数字键 `1` 选择第二页首项并完成上屏。

## 默认路径回归

证据目录：`artifacts/native-evidence/m1-space-regression-after-selection-paging/`。

- 输入：`shurufa`；
- 选择键：`space`；
- 实际 commit：`输入法`；
- Shift 后最终文本：`输入法abc`；
- `CommitMatched: true`；
- `EnglishModeMatched: true`；
- `Passed: true`。

这证明参数化选择与翻页扩展没有破坏原有第一候选空格上屏和中英文切换回归。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| 数字键选择非首候选 | `real_machine_verified` | 普通 WinForms TextBox、真实 TSF Profile、候选 PNG、数字键 `2` 与 commit 一致 |
| `=` 候选翻页 | `real_machine_verified` | 第一页/第二页 PNG，变化 `3,435` pixels |
| 翻页后数字键上屏 | `real_machine_verified` | 第二页数字键 `1`，commit 为截图首项“市” |
| 默认空格上屏 | `real_machine_verified` | `shurufa → 输入法` |
| Shift 英文输入 | `real_machine_verified` | 三条通过路径均以 `abc` 直输结束 |
| 5 应用 smoke | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚未开始 |
| 连续 30 分钟稳定性 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚未开始 |
| 干净 Windows 10/11 与 RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前只在已有开发历史的宿主机验证 |

因此本轮只关闭“数字选词 / 翻页验收”；`M1_HOST_BASELINE` 仍未标记为 `VERIFIED`。

## 复验命令

候选排序可能受用户学习影响，复验时应以同次运行保存的截图和 `CommittedText` 为准：

```powershell
./native/scripts/run-contextime-tsf-smoke.ps1 `
  -InputText shi `
  -SelectionKey 2

./native/scripts/run-contextime-tsf-smoke.ps1 `
  -InputText shi `
  -SelectionKey 1 `
  -PageDownCount 1

./native/scripts/run-contextime-tsf-smoke.ps1 `
  -InputText shurufa `
  -ExpectedText 输入法 `
  -ExpectedEnglish abc
```

## 下一阻塞

按既定顺序，下一项是记事本、浏览器、VS Code、Visual Studio 和 Terminal 的 5 应用 smoke。该项通过后才能开始 30 分钟稳定性验收。
