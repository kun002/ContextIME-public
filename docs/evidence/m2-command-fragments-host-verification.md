# M2.4 命令片段宿主机验证证据

## 产品结果

ContextIME `0.2.3-preview` 在中文状态下支持以固定命令根开头的常用命令片段，并在命令上屏后继续输入简体中文和 Shift 英文：

```text
git status输入法abc
npm run build输入法abc
dotnet test --filter ContextIME.Tests输入法abc
cargo build --release输入法abc
cd Assets/Textures输入法abc
git status && npm test输入法abc
```

本轮默认命令根只有：

```text
git
npm
dotnet
cargo
cd
```

影响链路限定为：

```text
Key event
→ ContextIME TSF / IPC / Server
→ librime-lua command processor / translator
→ composition / command candidate
→ commit
```

没有修改 TSF、IPC、候选窗口或 C++ 按键热路径。命令状态完全由当前 composition 推导，没有可在 Escape、焦点变化或 composition 清除后残留的独立 mode flag。

## 固定构建身份

- 安装程序：`contextime-0.2.3-preview-installer.exe`
- 文件大小：`12,054,194` bytes
- SHA-256：`8831be3c3896e7092e599f3f102d61776b3170d5b4951cb1b45a09988051131d`
- Build commit：`9a1ace7fbf4284b1dd779b6c1e4e4337700897c7`
- CI run：`33389352096`
- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- Windows：Windows 10 Pro，DisplayVersion `25H2`，Build `26200.9168`
- Smoke schema：`contextime.tsf-smoke.v5`
- Authenticode：`NotSigned`

CI 原生构建、固定上游审计、版本化数据层、安装器生成、安装器内容解包审计和 artifact 上传均通过。测试发生在已登录、解锁的主开发机交互桌面 Session 1，不是 WinRM、SSH、独立 LAN 笔记本或 RDP Session。

## 0.2.2 拒绝与 0.2.3 最小修复

`0.2.2-preview` 的 CI 构建曾通过，但最终 NSIS 只打包 `data\*.yaml`、`*.txt` 和 `*.gram`，遗漏命令功能依赖的 `data\rime.lua`。真机安装后 schema 存在，Lua processor 和 translator 却为 `nil`，Server 日志记录：

```text
func type: nil
attempt to call a nil value
```

因此 `0.2.2-preview` 不属于可交付版本：

- 文件：`contextime-0.2.2-preview-installer.exe`
- 文件大小：`12,050,075` bytes
- SHA-256：`0312106b7733518cc2a59578249f6bbcd7daca8002a64f937c939ed6166f089c`
- 结论：`rejected`

`0.2.3-preview` 没有修改命令运行时逻辑，只做安装器和构建门禁修复：

- NSIS 使用必需项 `File "data\rime.lua"`，文件缺失时构建失败；
- CI 解包最终安装程序并确认 `data\rime.lua` 存在；
- CI 比较安装程序内 Lua 与已审计构建输出的 SHA-256；
- 保存不可变 `0.2.2` schema 快照，维持补丁层可追溯性。

最终安装目录中的 `data\rime.lua` SHA-256 为：

```text
A52531E916DC0F7ACF16AD2F61DB3BA07940103CC15BA469712830430FEA9EDE
```

它与仓库和 CI artifact 中的审计源文件一致。最终 Server 日志中的 Lua 错误数为 `0`。

## 配置与提交边界

命令功能由 schema 配置：

```text
contextime/command_fragments/enabled
contextime/command_fragments/roots
```

用户可在 `%APPDATA%\ContextIME\contextime_developer.custom.yaml` 中关闭命令规则或替换命令根，不需要重装输入法。最终验收前确认该 custom 文件不存在，结果来自固定安装包的默认配置，不是本机实验规则。

Enter 的产品边界是：

```text
提交当前命令 composition
≠
把 Enter 转发给目标终端执行命令
```

这样不会让输入法替用户执行具有外部副作用的命令。单独输入命令根而没有后续空格时不进入命令片段；不在配置根列表中的普通拼音继续走中文输入链路。

## 六条命令真机结果

每条均在真实 TSF 会话中执行：

```text
命令片段
→ Enter 原样上屏
→ shurufa
→ Space 上屏“输入法”
→ Shift
→ abc 英文直输
```

| 输入 | 命令 commit | 最终文本 | 候选变化像素 | 结果 |
|---|---|---|---:|---|
| `git status` | `git status` | `git status输入法abc` | `22,222` | `Passed: true` |
| `npm run build` | `npm run build` | `npm run build输入法abc` | `28,574` | `Passed: true` |
| `dotnet test --filter ContextIME.Tests` | 原样 | `dotnet test --filter ContextIME.Tests输入法abc` | `34,451` | `Passed: true` |
| `cargo build --release` | 原样 | `cargo build --release输入法abc` | `28,223` | `Passed: true` |
| `cd Assets/Textures` | 原样 | `cd Assets/Textures输入法abc` | `27,325` | `Passed: true` |
| `git status && npm test` | 原样 | `git status && npm test输入法abc` | `29,106` | `Passed: true` |

最终 JSON、trace 和候选截图位于：

```text
artifacts/native-evidence/m2-command-0.2.3-final/
```

所有命令用例均记录 `CommitMatched: true`、`FollowupMatched: true`、`EnglishModeMatched: true`、`CapsLockRestored: true` 和 `Passed: true`。

## 取消与残留状态门禁

两条恢复用例验证命令状态不会污染下一次中文输入：

| 操作 | 命令提交 | 后续输入 | 最终文本 | 结果 |
|---|---|---|---|---|
| 输入 `git status` 后 Escape | 空 | `shurufa` | `输入法abc` | `Passed: true` |
| 输入 `git x`，退格两次回到命令根，再 Escape | 空 | `shurufa` | `输入法abc` | `Passed: true` |

Lua 源码和 CI 静态门禁同时禁止 `contextime_command_mode` 这类 composition 外状态。

## 既有能力、并存与安装审计

| 能力 | 实际结果 | 结果 |
|---|---|---|
| 普通拼音 `run` | `润abc` | `Passed: true` |
| 普通拼音 `shi` | `是abc` | `Passed: true` |
| 技术标识符 `GameObject` | `GameObjectabc` | `Passed: true` |
| 路径 `Assets/Textures/UI` | `Assets/Textures/UIabc` | `Passed: true` |
| URL `https://github.com` | `https://github.comabc` | `Passed: true` |
| `shi` + 数字键 `2` | `式abc` | `Passed: true` |
| `shi` + `=` 翻页 + 数字键 `1` | `时abc`，翻页变化 `1,518` pixels | `Passed: true` |

官方小狼毫真实 TSF 回归得到 `輸入法abc`；ContextIME 随后得到 `输入法abc`。两者的 Server 和 Pipe 同时存在，最终活动 Profile 已切回 ContextIME。

最终原生安装审计确认 machine registry、uninstall registry、安装目录、TSF CLSID/TIP/Profile、系统 TSF 文件、Server、ContextIME Pipe 和用户目录全部存在，`Mismatches: 0`。

用户目录中以下实验覆盖均不存在：

```text
%APPDATA%\ContextIME\rime.lua
%APPDATA%\ContextIME\contextime_developer.schema.yaml
%APPDATA%\ContextIME\contextime_developer.custom.yaml
```

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| 固定命令根和 composition 内命令处理 | `implemented` | librime-lua；不修改 C++ 热路径 |
| 0.2.3 Preview 原生包 | `built` | CI run `33389352096` 成功 |
| 固定上游、Lua/schema、安装器内容和隔离项 | `statically_verified` | CI 解包并校验 Lua 哈希 |
| 安装目录、Server、Pipe、TSF Profile | `runtime_verified` | 主开发机交互 Session 1 |
| 六条命令、取消恢复和既有输入回归 | `real_machine_verified` | Windows Build `26200.9168` 主开发机 |
| 官方小狼毫并存 | `real_machine_verified` | 双 Server、双 Pipe、双 Profile 真实输入 |
| 用户关闭或替换命令根的配置路径 | `statically_verified` | 配置读取和 custom patch 已审计；本轮未逐项真机修改配置 |
| 重启后的 0.2.3 输入状态 | `real_machine_verified` | 2026-09-01 重启后原生审计和 M2 连续混输通过，见 [`m2-mixed-input-host-verification.md`](m2-mixed-input-host-verification.md) |
| 干净 Windows 10/11 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前主机有开发和安装历史 |
| 独立 LAN 笔记本 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未连接、未触发 |
| RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未执行 |
| 安装程序签名 | `unverified` | Preview 为 `NotSigned` |

## M2.4 结论与下一项

M2.4 只关闭固定命令根范围和命令取消恢复门禁。任意 shell 语法理解、命令补全、自动执行和未配置命令根均不在本轮范围；没有开始 Context Engine、编辑器 Adapter、项目词库或个性化。

下一项是 M2 中文状态下的标识符、路径、URL、命令连续混输真机验收。该验收关闭前不进入 M3 Context Engine。
