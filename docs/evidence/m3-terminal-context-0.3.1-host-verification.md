# M3 Terminal 上下文 0.3.1 主机验证

## 结论

`contextime-0.3.1-preview-installer.exe` 已在主开发机完成 `0.3.0-preview → 0.3.1-preview` 覆盖升级。升级后的真实 Windows Terminal 交互桌面测试已完整证明：

```text
Terminal 获得焦点
→ 自动英文
→ Shift 手动切回中文
→ 候选和 composition 超过 5 秒仍受保护
→ 空格提交简体“你好”
→ composition 结束后自动英文恢复
```

本轮结果：

```text
M3_TERMINAL_CONTEXT_HOST = VERIFIED
```

该结论只关闭 Terminal 应用默认决策、manual override、composition/candidate 保护和提交后恢复门禁，不表示整个 M3 Context Engine、编辑器语法上下文或 VS Code Adapter 已完成。

## 验证对象

```text
installer: contextime-0.3.1-preview-installer.exe
installer bytes: 12,129,246
installer SHA-256: 69bf4c609650132c4e9c2c2298be11dbe62e76839e832b3b8bf26aaf44223401
build commit: 18016fd5fbc004dfe2ec42133b9ed171e5f0c152
installer CI run: 33606161035
product fix PR #26 merge: 17a9ba801334788f01517f4940b95f1a8de3b42f
installed version: 0.3.1-preview
Windows: Windows 10 Pro 25H2
Windows build: 26200.9168
desktop session: 1
Authenticode: NotSigned
```

安装包的本机短路径副本为 `F:\ContextIME-0.3.1-preview.exe`，其大小和 SHA-256 与 CI artifact 完全一致。

## 覆盖升级与安装态

安装完成后的只读收敛结果：

- `C:\Program Files\ContextIME\contextime-0.3.1-preview` 存在；
- 旧 `contextime-0.3.0-preview` 目录已移除；
- uninstall registry 的 `DisplayVersion` 为 `0.3.1-preview`；
- System32 TSF DLL 与安装目录 x64 DLL 哈希一致；
- ContextIME Server 和 Context Service 均从 `0.3.1-preview` 目录运行；
- `\\.\pipe\<user-scope>\ContextIMENamedPipe` 与 `\\.\pipe\ContextIME.ContextService.v1` 均存在；
- 官方小狼毫 `0.17.4` Server、`WeaselNamedPipe` 和独立卸载项继续存在。

关键已安装文件：

| 文件 | Bytes | SHA-256 |
|---|---:|---|
| `C:\Windows\System32\contextime.dll` | 1,073,664 | `90d5a8c6fc10e95f29af9ea3477144cb7e0e93fa74e3fcde4b1b254010be65f9` |
| `C:\Program Files\ContextIME\contextime-0.3.1-preview\contextimex64.dll` | 1,073,664 | `90d5a8c6fc10e95f29af9ea3477144cb7e0e93fa74e3fcde4b1b254010be65f9` |
| `C:\Program Files\ContextIME\contextime-0.3.1-preview\WeaselServer.exe` | 2,372,096 | `94cd4c9d7126c1438f3a32ab9941f54064eb12378edbd611c49d598eeea76330` |
| `C:\Program Files\ContextIME\contextime-0.3.1-preview\contextime-context-service.exe` | 318,976 | `45bc041a89ebb98e0b54ad3bb935c71a3a4eebf1ad242f84769f48f977908579` |

原生安装态审计：

```text
Expected state: Installed
machine registry: true
uninstall registry: true
install directory: true
TSF CLSID/TIP/Profile: true
system TSF DLL: true
server process: true
IPC pipe: true
user data directory: true
Mismatches: 0
```

审计 JSON：

```text
artifacts/native-evidence/0.3.1-installed-state/20260902T084538Z/contextime-native-evidence.json
bytes: 11,013
SHA-256: f00db6371c9ecec93fc51102a47df7e9132f30e654123c67538235cd0f07784d
```

## Notepad 基线回归

安装后先在真实 Notepad `11.2607.14.0` 复用 app smoke：

```text
input: shurufa
candidate detected: true
candidate changed pixels: 33,542
committed: 输入法
final text: 输入法abc
commit matched: true
English mode matched: true
Chinese mode restored: true
profile restored: true
passed: true
errors: []
```

证据：

| 文件 | Bytes | SHA-256 |
|---|---:|---|
| `notepad-baseline.json` | 2,826 | `ee2b465231246b08434cf8025460dce72201b213a4e15e0631a586769367ba3f` |
| `notepad-baseline.json.candidate.png` | 7,873 | `af0bea7e7a1d7fde41334394cfb38de79e6b6d45a9fe8c3d166d5b04861ce5d3` |

证据根目录：

```text
artifacts/native-evidence/0.3.1-post-install-20260902T082926Z/
```

## Terminal 最终结果

真实 Windows Terminal `1.24.2607.10001` 在交互桌面 Session 1 中完成固定 `terminal-context` 场景：

| 门禁 | 观测结果 | 状态 |
|---|---|---|
| 应用默认英文 | 首次输入末尾为 `nihao` | `AutomaticEnglishObserved: true` |
| 手动中文不被抢回 | 2,114 ms 时候选变化 `33,254` pixels | `ManualOverrideProtected: true` |
| composition/candidate 保护 | 5,615 ms 时仍有候选，变化 `33,272` pixels | `CandidateProtectedPastManualWindow: true` |
| 简体中文提交 | 提交文本末尾为 `你好` | `ProtectedCommitMatched: true` |
| 提交后恢复英文 | 最终文本末尾为 `你好nihao` | `AutomaticEnglishResumed: true` |

完整收口字段：

```text
ProfileActivated: true
ForegroundAcquired: true
FixturePrepared: true
CandidateWindowDetected: true
CommitMatched: true
EnglishModeMatched: true
ForegroundAfterInput: true
ProfileRestored: true
ProbeClosed: true
Passed: true
Errors: []
```

证据：

| 文件 | Bytes | SHA-256 |
|---|---:|---|
| `terminal-context-harness-fix.json` | 4,056 | `b0b23d3a1b89cb9ea37e9e21d21e97cbe5b1d4dfeffd915cba2439788d6a943b` |
| `terminal-context-harness-fix.json.manual-candidate.png` | 10,624 | `963d3ac0414b689c4ed158fbe6b3e8af774c2697965f95bcd685849be6c24482` |
| `terminal-context-harness-fix.json.protected-candidate.png` | 10,685 | `9d8b8257669d7594434b51705d3898e44116a1da125b592be76e906f670ba165` |

## 测试框架边界修复

安装后的前两次 Terminal 自动化没有生成业务 JSON，而是在外层 70 秒门禁超时。保留的 trace 和窗口截图证明：

- 专用 Windows Terminal 已正常启动，停在空 PowerShell 提示符；
- harness 尚未注入第一个 `nihao`；
- Context Service、ContextIME Server 和两条 ContextIME pipe 同时健康；
- 卡点是 `WaitForDesktopWindow()` 通过 UI Automation 桌面根枚举管理员 Terminal 窗口。

Codex 当前运行于高完整性交互 Terminal，测试窗口因此显示为 `管理员: ContextIMETerminalSmoke...`。`Process.MainWindowHandle` 已能可靠发现该窗口，但 UI Automation 桌面根枚举未返回它。

最小修复是让 `TerminalProbe.Open()` 复用 Edge、VS Code 和 Visual Studio 已使用的 `WaitForProcessWindow()`：先按唯一标题找到真实进程窗口句柄，再用 `AutomationElement.FromHandle()` 获取 UIA 根。没有修改 ContextIME、Weasel、librime、Context Service、IPC 或输入策略。强制重编 harness 后，同一已安装产品在 16.2 秒内完整通过。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| PR #26 候选提交后 UI 状态修复 | `implemented` | `UI::Destroy()` 清除遗留 shown 状态 |
| 0.3.1 Preview 安装包 | `built` | 固定 build commit、CI run、大小和 SHA-256 |
| `0.3.0 → 0.3.1` 覆盖升级 | `real_machine_verified` | 目录、注册表、文件哈希、进程和 pipe |
| Notepad 基础输入 | `real_machine_verified` | 真实 TSF、候选截图、`输入法abc` |
| Terminal 自动英文与手动中文保护 | `real_machine_verified` | 真实 Windows Terminal 交互桌面 |
| 候选超过 5 秒保护 | `real_machine_verified` | 5,615 ms 截图和像素门禁 |
| 简体“你好”提交 | `real_machine_verified` | TextPattern 精确末尾匹配 |
| 提交后自动英文恢复 | `real_machine_verified` | 最终末尾 `你好nihao` |
| 官方小狼毫并存 | `runtime_verified` | 双 Server、独立 pipe、注册表和卸载项 |
| 0.3.1 安装后的系统重启复验 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮重启发生在安装 0.3.1 之前 |
| 0.3.1 卸载清理 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮没有卸载当前版本 |
| 干净 Windows 10/11 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前为有历史安装的开发主机 |
| LAN 笔记本与 RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未连接远端节点 |
| 安装程序签名 | `unverified` | Preview 为 `NotSigned` |

## 下一项

先提交 Terminal harness 的窗口发现修复和本主机证据。该门禁收口后，不再扩展 Terminal 测试框架；下一项按产品顺序继续 M3 的剩余 Context Engine 边界，而不是进入 VS Code Adapter、项目词库或个性化。
