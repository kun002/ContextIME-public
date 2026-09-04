# M2.2 常用路径片段宿主机验证证据

## 产品结果

ContextIME `0.2.1-preview` 在中文状态下可以连续输入三类常用路径片段，并在路径上屏后继续输入简体中文和 Shift 英文：

```text
Assets/Textures/UI输入法abc
C:\Projects\ContextIME输入法abc
src/components/App.tsx输入法abc
```

影响链路限定为：

```text
librime recognizer
→ 复用 uppercase raw-code tag
→ composition / candidate
→ commit
```

没有修改 TSF、IPC、ContextIME Server、候选窗口或 librime 产品热路径，也没有增加新的 translator owner。路径规则继续复用 M2.1 已验证的 raw-code translator，因此路径结束后仍由原开发者方案处理后续拼音。

## 固定构建身份

- 安装程序：`contextime-0.2.1-preview-installer.exe`
- 文件大小：`12,050,362` bytes
- SHA-256：`49b856a9c6ed5814e14c810d33ab70d67caea619bb359f1df89f8005b2a4f689`
- Build commit：`f82e0788854841df6ee1d34cce6835735ad34429`
- CI run：`33364340995`
- CI URL：`https://github.com/kun002/ContextIME/actions/runs/33364340995`
- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- Windows：Windows 10 Pro，DisplayVersion `25H2`，Build `26200.9168`
- Authenticode：`NotSigned`

CI 的原生前端构建、固定上游审计、0.2.0 到 0.2.1 数据分层、安装器生成、包内 schema 审计和 artifact 上传全部通过。测试发生在已登录、解锁的主开发机交互桌面 Session 1，不是 WinRM、SSH、独立 LAN 笔记本或 RDP Session。

## 根因和最小修复

`0.2.0-preview` 中三条路径的实际故障为：

```text
Assets/Textures/UI
→ Assets、Textures、UI

C:\Projects\ContextIME
→ C：、Projects、ContextIME
```

故障范围收窄到 `librime recognizer → punctuator`：`/`、`:` 和 `\` 被中文标点处理，小写目录段被当作拼音。TSF、IPC、Server、后续中文和 Shift 英文均正常。

最终 recognizer pattern 为：

```yaml
([A-Z][-_+.'0-9A-Za-z]*|[a-z][0-9a-z]*[A-Z][0-9A-Za-z]*|[a-z][0-9A-Za-z]*_[0-9A-Za-z_-]*|^[A-Za-z]:(?:[\\/][0-9A-Za-z_.+\\/-]*)?|^(?:[0-9A-Za-z_+-][0-9A-Za-z_.+-]*[\\/])+[0-9A-Za-z_.+\\/-]*)$
```

新分支只覆盖：

- Windows 盘符路径片段，例如 `C:\Projects\ContextIME`；
- 以普通目录段开头、至少包含一个 `/` 或 `\` 的相对路径片段；
- 字母、数字、下划线、连字符、点号和加号组成的无空格段。

曾实验新增独立 `path` tag，但没有 translator owner，导致路径可以直出却没有候选，后续拼音也会变成英文，因此放弃。最终只扩展已有 `uppercase` tag，避免引入第二套提交行为。

## 明确边界

本轮不支持：

```text
.\src\main.cpp
..\Assets\Textures
./src/main.cpp
../src/main.cpp
\\server\share
/usr/local/bin
带空格路径
URL
命令
```

首字符 `.` 在当前中文方案中会先被 punctuator 提交为中文句号，schema 无法回滚。可逆实验中 `.\src\main.cpp` 实际得到 `。、src\main.cpp输入法abc`。本轮不为支持点号相对路径而破坏普通中文句号，也不把 URL 或命令描述为已完成。

## 覆盖安装和 custom 隔离

最终安装前，实验文件：

```text
%APPDATA%\ContextIME\contextime_developer.custom.yaml
```

已移出用户数据目录并备份到：

```text
artifacts/native-evidence/m2-0.2.1-final-host-verification/preinstall-backup/
```

随后重新部署 `0.2.0-preview`，用户编译 schema 回到 `version: 0.2.0`，不再包含实验路径规则。再使用固定 CI artifact 覆盖安装：

- `C:\Program Files\ContextIME\contextime-0.2.0-preview` 已移除；
- `C:\Program Files\ContextIME\contextime-0.2.1-preview` 已存在；
- 安装目录源 schema 为 `version: 0.2.1` 并包含最终路径 pattern；
- `%APPDATA%\ContextIME\build\contextime_developer.schema.yaml` 为 `version: 0.2.1` 并包含同一 pattern；
- 实验 custom 仍不存在。

安装包装器使用 PowerShell `Start-Process -Wait` 时会继续等待安装器启动的常驻 ContextIME Server；确认安装器主体已经退出、0.2.1 已完整落盘且 Server 正常运行后，只中断了等待用 PowerShell，没有终止安装器或输入法服务。因此本轮没有记录可引用的 installer exit code，以安装后文件、Profile 激活和真实输入结果证明覆盖安装完成。

## 三条路径真机结果

每条均在同一个真实 TSF 会话中执行：

```text
路径
→ Space 原样上屏
→ shurufa
→ Space 上屏“输入法”
→ Shift
→ abc 英文直输
```

| 输入 | 路径 commit | 后续中文 commit | 最终文本 | 候选变化像素 | 结果 |
|---|---|---|---|---:|---|
| `Assets/Textures/UI` | `Assets/Textures/UI` | `Assets/Textures/UI输入法` | `Assets/Textures/UI输入法abc` | `6,634` | `Passed: true` |
| `C:\Projects\ContextIME` | `C:\Projects\ContextIME` | `C:\Projects\ContextIME输入法` | `C:\Projects\ContextIME输入法abc` | `8,074` | `Passed: true` |
| `src/components/App.tsx` | `src/components/App.tsx` | `src/components/App.tsx输入法` | `src/components/App.tsx输入法abc` | `8,506` | `Passed: true` |

最终 JSON 和候选截图位于：

```text
artifacts/native-evidence/m2-0.2.1-final-host-verification/
```

Smoke schema 为 `contextime.tsf-smoke.v4`。全部用例记录 `CapsLockNormalized: true` 和 `CapsLockRestored: true`，测试不会再因用户初始 Caps Lock 状态改变 ASCII fixture，也会在成功或失败后恢复原状态。

## M1、M2.1 和并存回归

| 用例 | 实际结果 | 结果 |
|---|---|---|
| `GameObject`，随后中文和 Shift 英文 | `GameObject输入法abc` | `Passed: true` |
| `playerController`，随后中文和 Shift 英文 | `playerController输入法abc` | `Passed: true` |
| `shurufa` + Space | `输入法abc` | `Passed: true` |
| `shi` + 数字键 `2` | `式abc` | `Passed: true` |
| `shi` + `=` 翻页一次 + 数字键 `1` | `使abc`，翻页变化 `919` pixels | `Passed: true` |

覆盖安装后同时观察到：

```text
\\.\pipe\<user-scope>\ContextIMENamedPipe
\\.\pipe\<user-scope>\WeaselNamedPipe
```

官方小狼毫 Profile `3D02CAB6-2B8E-4781-BA20-1C9267529467` 的真实 TSF 回归提交 `輸入法abc`；ContextIME Profile `A200BA94-B1A7-4668-A22B-CA61EC1E79F7` 随后提交简体 `输入法abc`。两条均为 `Passed: true`，最终活动 Profile 已切回 ContextIME。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| 0.2.1 路径 recognizer 和版本化数据层 | `implemented` | 复用 librime raw-code tag，不改 TSF/IPC 热路径 |
| 0.2.1 Preview 原生包 | `built` | CI run `33364340995` 成功 |
| 固定上游、schema、安装器和隔离项 | `statically_verified` | CI 包审计通过，下载后 SHA 与 CI 记录一致 |
| 0.2.0 到 0.2.1 覆盖安装、Server 和双 Pipe | `runtime_verified` | 主开发机交互 Session |
| 三条有限路径、后续中文和 Shift 英文 | `real_machine_verified` | Windows Build `26200.9168` 主开发机 |
| M1、M2.1 和官方小狼毫并存回归 | `real_machine_verified` | 最终 0.2.1 包与 Smoke v4 |
| 干净 Windows 10/11 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前主机有开发和安装历史 |
| 独立 LAN 笔记本 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未连接、未触发 |
| RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未执行 |
| 安装程序签名 | `unverified` | Preview 为 `NotSigned` |

## M2.2 结论与下一项

M2.2 只关闭上述有限路径片段。点号相对路径、UNC、Unix 绝对路径、空格路径、URL 和命令仍未完成；Context Engine、编辑器 Adapter、项目词库和个性化也未开始。

下一项是 M2.3 URL 识别。进入前先收口 PR #14；干净 Windows、LAN、RDP 和 Release Gate 继续保留 `REAL_WINDOWS_VERIFICATION_REQUIRED`。
