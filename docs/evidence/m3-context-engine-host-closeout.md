# M3 Context Engine 主机收口

## 结论

ContextIME `0.3.1-preview` 已在主开发机完成 Context Engine 原生运行时闭环：

```text
Foreground application source
→ Context Service / versioned local pipe
→ single Context Engine decision
→ background refresh worker
→ lock-free decision cache
→ live composition / candidate / manual safety recheck
→ TSF-owner-thread IME state applier
→ Weasel ascii_mode
```

服务正常时，真实 Windows Terminal 自动进入英文；用户手动切回中文后，manual override 和候选组合保护生效，提交简体“你好”后自动英文恢复。服务完全停止时，Notepad 的普通拼音、候选、上屏和中英文切换继续工作；服务恢复后 Terminal 自动决策重新生效。

本轮状态：

```text
M3_CONTEXT_ENGINE = VERIFIED
```

这里的 M3 只指统一决策 owner、原生 App/Terminal source、服务/IPC/cache/TSF bridge、保护优先级和失败 `KEEP`。VS Code 的语言/语法精确信号属于 M4 Adapter；项目规则和用户规则分别属于 M5/M6。本轮没有为未知编辑区域增加猜测逻辑。

## 固定产品对象

```text
installed product: ContextIME 0.3.1 Preview
build commit: 18016fd5fbc004dfe2ec42133b9ed171e5f0c152
installer CI run: 33606161035
M3 Weasel bridge CI run: 33606160973
installer: contextime-0.3.1-preview-installer.exe
installer bytes: 12,129,246
installer SHA-256: 69bf4c609650132c4e9c2c2298be11dbe62e76839e832b3b8bf26aaf44223401
Windows: Windows 10 Pro 25H2, build 26200.9168
desktop session: 1
```

安装包与固定上游仍为：

```text
Weasel 0.17.4
9cc96e20dc71b80876b12f689bb5863c76c2a7ed

librime 1.13.1
1c23358157934bd6e6d6981f0c0164f05393b497
```

## 统一 owner 与优先级

平台无关 `ContextEngine::Evaluate()` 是自动状态决策的唯一 owner，固定顺序为：

```text
composition / candidate protection
→ explicit user lock
→ recent manual override
→ automation disabled
→ context unavailable
→ user rule
→ project rule
→ syntax context
→ surface context
→ application default
→ KEEP
```

当前 `0.3.1` 原生 source 只对可靠 Terminal app/surface 提供 `ENGLISH`。VS Code、Visual Studio、浏览器和文档应用只分类 application kind，不根据 top-level app 猜输入区域，surface 保持 `UNKNOWN`，没有规则时输出 `KEEP`。

自动状态的唯一应用路径是 `ContextRefreshWorker → DecisionCache::Read → ImeStateApplier → WeaselTSF::_ApplyContextMode()`。worker callback 只向 ContextIME 自有 message-only window 投递消息；COM、TSF、candidate 和 Weasel client 操作都回到 TSF owner thread。输入按键路径不调用 Named Pipe、文件、PowerShell、Node.js、Git、网络或 AI。

## 固定 CI 证据

既有阶段门禁覆盖：

| 组件 | 固定门禁 |
|---|---|
| Context Engine | Windows/Ubuntu C++17，61 assertions |
| protocol v1 | Windows/Ubuntu，46 assertions |
| Decision Cache | Windows/Ubuntu，65 assertions；10 万次并发 publish/read |
| Application Context | Windows/Ubuntu，31 assertions |
| Context Service Windows | Named Pipe integration，47 assertions |
| IME State Applier | Windows/Ubuntu，11 assertions |
| Context Refresh Worker | Windows，31 assertions |
| Weasel TSF bridge | 固定 patch-stack，Release x64 + Win32 |

`0.3.1` build commit 的 installer run `33606161035` 再次成功执行：

- `Build and exercise Context Service lifecycle`；
- `Re-run canonical IME bridge fixtures`；
- patched native frontend build；
- Context Service staging；
- installer manifest 和 native package audit。

同一 commit 的 M3 bridge run `33606160973` 再次通过线程/故障静态审计、canonical fixtures 和 x64/Win32 Weasel TSF 构建。

本地主机装有 Visual Studio 2022 Developer PowerShell `17.14.22`，但未安装 C++ `cl.exe` 组件，因此本轮没有虚报本地 MSVC fixture 通过，也没有为重复 CI 临时安装大型工具链。上述编译/单元结论来自固定 CI；下述输入结论来自已安装 `0.3.1` 的真实交互桌面。

## Context Service 停止时回退

安装后的 Context Service 初始 PID 为 `21396`。使用已安装 executable 的 `--quit` 正常停止后：

```text
quit exit code: 0
contextime-context-service process count: 0
ContextIME.ContextService.v1 pipe: absent
ContextIME WeaselServer: running
official WeaselServer: running
```

在服务和 pipe 全程缺失时运行真实 Notepad app smoke：

```text
candidate detected: true
candidate changed pixels: 51,811
committed: 输入法
final text: 输入法abc
commit matched: true
English mode matched: true
Chinese mode restored: true
profile restored: true
probe closed: true
passed: true
errors: []
```

测试结束后再次确认 Context Service process count 为 `0`，service pipe 仍不存在。这证明 Context Service 失败不会破坏 TSF → Weasel/librime 的普通输入链。

## Context Service 恢复后自动决策

重新启动同一安装目录的 Context Service：

```text
PID: 6344
process count: 1
path: C:\Program Files\ContextIME\contextime-0.3.1-preview\contextime-context-service.exe
ContextIME.ContextService.v1 pipe: present
```

随后真实 Windows Terminal `1.24.2607.10001` 完成完整场景：

| 门禁 | 结果 |
|---|---|
| Terminal 自动英文 | `AutomaticEnglishObserved: true` |
| Shift 手动中文保护 | `ManualOverrideProtected: true`，2,088 ms |
| composition/candidate 超过 5 秒保护 | `CandidateProtectedPastManualWindow: true`，5,627 ms |
| 简体“你好”提交 | `ProtectedCommitMatched: true` |
| 提交后恢复英文 | `AutomaticEnglishResumed: true`，末尾 `你好nihao` |
| Profile 与窗口清理 | `ProfileRestored: true`，`ProbeClosed: true` |
| 最终结果 | `Passed: true`，`Errors: []` |

该结果同时证明 live safety recheck 没有被后台缓存绕过：manual 和 composition/candidate 是 TSF owner thread 在应用 decision 前读取的即时状态。

## 安装与并存复核

服务恢复后原生安装审计：

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

官方小狼毫 `0.17.4` 的 Server、注册表、用户目录和 `WeaselNamedPipe` 继续独立存在。

## 原始主机证据

证据根目录：

```text
artifacts/native-evidence/m3-context-engine-0.3.1-closeout-20260902T113658Z/
```

| 文件 | Bytes | SHA-256 |
|---|---:|---|
| `service-stopped-notepad.json` | 2,843 | `2bba121613bed075072ab0852882bb3a5be3d32e3019f0a9894ad08759460bcc` |
| `service-stopped-notepad.json.candidate.png` | 11,901 | `f88766c0f58c911358dc2a1ede839413e103fcaa8b2a9d0cce83f879d78479ee` |
| `service-restored-terminal.json` | 4,091 | `a93fe2e476673f1e065f5d7052ec956d47ae8665f5231acd1df82ec1cf00bb3e` |
| `service-restored-terminal.json.manual-candidate.png` | 13,890 | `2d05c1a015cd4bc2124042bc889569a8631bcfc8018c02a44fa6d458ab84f8c2` |
| `service-restored-terminal.json.protected-candidate.png` | 13,958 | `417dfd75bcbd91a8925d81376b9485e34f75f7b462c0c091859820ffacbc7c41` |
| `installed-state/20260902T113834Z/contextime-native-evidence.json` | 10,867 | `92b7949e15e4007e555e066174904158d75a6df23a46fdc1d574b6e4d88ee0fb` |

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| Context Engine 单一 owner 与优先级 | `implemented` / `statically_verified` | 纯 C++ Engine 和固定 assertions |
| Context Service protocol/IPC/timeout fallback | runner `runtime_verified` | 固定 Windows Named Pipe fixtures |
| 非阻塞 Decision Cache | runner `runtime_verified` | lock-free reader 和并发 fixture |
| 固定 Weasel TSF bridge x64/Win32 | `built` / `statically_verified` | 固定上游和 patch-stack |
| 真实 Terminal application source 与状态应用 | `real_machine_verified` | 已安装 `0.3.1`，交互桌面 |
| manual/composition/candidate live protection | `real_machine_verified` | 2,088 ms / 5,627 ms 和候选截图 |
| Context Service 缺失时普通输入回退 | `real_machine_verified` | 服务与 pipe 全程不存在的 Notepad smoke |
| 服务恢复后自动决策恢复 | `real_machine_verified` | 新服务 PID/Pipe 和 Terminal 完整场景 |
| 官方小狼毫并存 | `runtime_verified` | 双 Server、独立 pipe/registry/data |
| M3 Context Engine 阶段门禁 | `VERIFIED` | `M3_CONTEXT_ENGINE = VERIFIED` |
| VS Code language/syntax context | `not_started` | M4 Adapter，不属于本门禁 |
| Project rule source | `not_started` | M5 Project Dictionary |
| User rule/configuration source | `not_started` | M6 Personalization |
| `0.3.1` 安装后重启复验 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 最近重启发生在安装本版本之前 |
| 当前版本卸载、干净机、LAN、RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | Release Gate 保留 |

## 下一项

M3 不再扩展新的猜测式 Windows surface detector，也不把 Context Service改造成 Windows SCM 服务。按既定产品顺序，下一项进入 M4 VS Code Adapter：只上报版本化、最小化的 editor/language/syntax context，由现有 Context Engine 继续作为唯一 decision owner；Adapter 不得直接切换输入法状态。
