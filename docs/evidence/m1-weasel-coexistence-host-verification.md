# M1 ContextIME 与小狼毫宿主机并存证据

## 产品结果

ContextIME 0.1.3 Preview 与官方小狼毫 0.17.4 已在同一 Windows 宿主机完成真实并存、分别输入和双向卸载隔离验证。

该结果只推进 Native IME Baseline。没有增加 Context Engine、编辑器 Adapter、项目词库、AI 或云能力。

## 固定安装包

ContextIME：

- 文件：`contextime-0.1.3-preview-installer.exe`
- 大小：`12,049,788` bytes
- SHA-256：`a1e02e04624fe9f11611dd06cb9300bdeb057886da32693b83a6a1ee10e40dd2`
- Build commit：`aa80d79a659511c0a00f534011bb39698ec84e9c`
- Authenticode：`NotSigned`

小狼毫：

- GitHub Release：`rime/weasel` tag `0.17.4`
- annotated tag 解引用 commit：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- 文件：`weasel-0.17.4.0-installer.exe`
- 大小：`12,431,118` bytes
- GitHub Release 声明 SHA-256：`cf509534a8f5f8af9c98ed7cbb8f135439f145a8cbe7e50ede42bb5b5ab45c29`
- 本机重新计算 SHA-256：`cf509534a8f5f8af9c98ed7cbb8f135439f145a8cbe7e50ede42bb5b5ab45c29`
- Authenticode：`NotSigned`

仓库早期 M0 CI artifact 已超过 GitHub 保留期。重新触发旧 `Native Upstream Baseline` 工作流时，当前 `get-rime.ps1` 没有提供 `rime_api.h`，上游构建未产生安装器，因此没有把失败产物用于真机测试。宿主机最终使用 tag、commit 和 Release digest 均可核验的官方固定 Release，不使用 nightly。

## 测试环境

- Windows x64；
- DisplayVersion `25H2`；
- Build `26200.9168`；
- 同一开发宿主机，不是干净 VM；
- 测试后最终状态为 ContextIME 与小狼毫均已安装。

## 并存身份

Windows 当前用户语言列表同时包含：

- ContextIME：`0804:{9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9}{A200BA94-B1A7-4668-A22B-CA61EC1E79F7}`；
- 小狼毫：`0804:{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}{3D02CAB6-2B8E-4781-BA20-1C9267529467}`。

运行时同时存在两个 `WeaselServer.exe` 进程，依靠独立单实例标识并存。IPC 分别为：

```text
\\.\pipe\<username>\ContextIMENamedPipe
\\.\pipe\<username>\WeaselNamedPipe
```

安装和用户目录分别为：

```text
C:\Program Files\ContextIME\contextime-0.1.3-preview
C:\Program Files\Rime\weasel-0.17.4
%APPDATA%\ContextIME
%APPDATA%\Rime
```

ContextIME 的只读安装状态门禁在小狼毫运行时为 0 mismatch。

## 双 Profile 输入证据

原创 WinForms/TSF 工具已扩展为可显式选择 Text Service、Profile 和 language ID。默认目标仍为 ContextIME。

最终并存状态下，工具分别激活两套真实 TSF Profile：

| 目标 | 输入 | 中文上屏 | Shift 后最终文本 | 候选变化像素 | 结果 |
|---|---|---|---|---:|---|
| ContextIME | `shurufa` | `输入法` | `输入法abc` | `49,970` | `Passed: true` |
| 小狼毫 | `shurufa` | `輸入法` | `輸入法abc` | `103,408` | `Passed: true` |

两份 JSON 都记录了正确活动 Profile、候选屏幕截图、两个 Server 进程和两个独立 IPC Window。简繁差异符合各自默认方案，不是两套运行时串线。

开发验证中有一次 ContextIME 参数化回归收到测试窗口之外的物理按键，截图中的 composition 为额外的 `gao...`，上屏为“告诉如法”。同一二进制全新 session 立即复跑以及最终并存复跑均严格得到 `shurufa → 输入法`。失败证据保留，不计为产品通过证据。

## 卸载 ContextIME 保护小狼毫

在两个 Server 和两个 Pipe 同时运行时卸载 ContextIME：

- ContextIME CLSID、HKLM/HKCU TIP、语言 Profile、系统 DLL、Server、Pipe 和程序目录全部消失；
- 小狼毫 32/64 位系统 DLL 的路径、大小、时间和 SHA-256 前后相同；
- 小狼毫机器注册表与卸载项前后相同；
- `%APPDATA%\Rime` 的 36 个既有文件没有被 ContextIME 卸载删除；
- 小狼毫原 Server PID 与 `WeaselNamedPipe` 在 ContextIME 卸载后仍存在；
- 小狼毫随后再次通过 `shurufa → 輸入法abc`、候选和上屏测试。

参考报告首次比较曾把相同 `systemFiles` 判为变化。根因是 PowerShell 把参考 JSON 的 ISO 时间解析为 `DateTime` 后使用不同文本精度重新序列化；逐字段与原始 JSON 比较确认文件完全相同。证据收集器现会在比较前规范化 ISO 时间，并增加同报告参考比较 CI 回归。修正后严格门禁为 0 mismatch。

## 卸载小狼毫保护 ContextIME

重装 ContextIME 和小狼毫后，再卸载小狼毫：

- ContextIME 安装、TSF、Server 和 IPC 门禁为 0 mismatch；
- `%APPDATA%\ContextIME` 的 35 个既有文件没有被小狼毫卸载删除；
- ContextIME 用户库仍存在；
- 小狼毫自己的 36 个用户文件按其默认保留策略仍存在；
- 只剩 `ContextIMENamedPipe`；
- ContextIME 随后再次通过 `shurufa → 输入法abc`、候选和上屏测试。

最后重新安装固定小狼毫安装包，两套进程、Pipe、Profile 和输入均再次通过。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| 同机安装与输入法列表 | `runtime_verified` | 两套安装目录与 TSF Profile 同时存在 |
| Server/IPC 并存 | `runtime_verified` | 两个进程、两个独立 Pipe/IPC Window |
| ContextIME 简体输入 | `runtime_verified` | `输入法abc`，候选截图与 Profile 匹配 |
| 小狼毫独立输入 | `runtime_verified` | `輸入法abc`，候选截图与 Profile 匹配 |
| 卸载 ContextIME 保护小狼毫 | `runtime_verified` | 注册、系统 DLL、用户文件和卸载后 TSF 输入验证 |
| 卸载小狼毫保护 ContextIME | `runtime_verified` | ContextIME 状态 0 mismatch，用户文件与 TSF 输入验证 |
| 干净 Windows 10/11 VM | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前为已有开发历史的宿主机 |
| RDP、数字选词、翻页、应用矩阵、30 分钟输入 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未执行 |
| 安装包签名 | `unverified` | 两个测试安装包均未签名 |

## 下一阻塞

1. 在干净 Windows 10/11 VM 重复双安装、双 Profile 输入和双向卸载；
2. 验证数字选词、候选翻页和常用应用；
3. 验证 RDP 与连续 30 分钟输入；
4. 正式发布前增加 Authenticode 签名。
