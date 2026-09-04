# M1 ContextIME 0.1.2 原生预览证据

## 产品结果

ContextIME 0.1.2 只修复 Native IME Baseline 的输入服务并存链路：小狼毫已经运行时，ContextIME Server 不再因为共享进程级互斥锁而立即退出。

本轮影响链路：

```text
TSF Client
→ ContextIME IPC
→ ContextIME Server singleton
→ librime
```

不包含 Context Engine、编辑器 Adapter 或其他高级功能。

## 固定上游

- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- ContextIME build commit：`a9b37f62d51318d200dbce0748224bee019dab56`

## 已确认根因和修复

0.1.1 仍沿用了 Weasel Server 的用户级单实例互斥锁：

```text
(WEASEL)Furandōru-Sukāretto-<username>
```

当 Weasel 已运行时，ContextIME Server 会在 `Start()` 中退出，TSF Profile 虽然可激活，但按键只能英文透传。

0.1.2 使用独立互斥锁：

```text
(CONTEXTIME)NativeServer-<username>
```

CI 静态门禁同时确认 ContextIME 派生源码不再包含上游 Server 互斥锁。

## 2026-08-30 CI 重建

- Workflow：`ContextIME Native Preview 0.1.2`
- Run ID：`33289352050`
- 结果：`success`
- Runner：`windows-2022` / image `20260824.284.2`
- Build commit：`a9b37f62d51318d200dbce0748224bee019dab56`

通过步骤：

1. 校验固定 Weasel/librime commit；
2. 应用 identity、0.1.1 和 0.1.2 patch stack；
3. 构建 Rime 数据和原生前端；
4. 生成隔离命名的 TSF DLL/IME 文件；
5. 生成 NSIS 安装程序；
6. 审计 Server 互斥锁和安装器隔离项；
7. 上传安装器、GPL patch 和工具链证据。

## 安装程序

- 文件：`contextime-0.1.2-preview-installer.exe`
- 大小：`12,046,147` bytes
- SHA-256：`8cb4a9cdb29c3dcba1e615465315de4dfac40ffba8ed0b6309327656696175f9`
- Authenticode：`NotSigned`

该文件是未签名 Preview，不是正式稳定版。

## 本机证据

测试环境：

- Windows 11 专业版 x64；
- Version `10.0.26200`，Build `26200`；
- 普通用户 PowerShell，安装需要 UAC。

已验证：

- 从 Run `33289352050` 下载的 `.exe` 大小和 SHA-256 与 CI 报告一致；
- 安装前没有 ContextIME 机器/用户注册表、卸载项、TSF CLSID/Profile、系统 DLL、Server、Named Pipe 或用户数据残留；
- UAC 提升后安装程序成功写入 `C:\Program Files\ContextIME\contextime-0.1.2-preview`；
- TSF CLSID、Profile 和 `0804:{9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9}{A200BA94-B1A7-4668-A22B-CA61EC1E79F7}` 已出现在当前用户输入法列表；
- `contextime.dll` 已进入系统目录，ContextIME Server 进程与用户级 `ContextIMENamedPipe` 同时存在；
- `%APPDATA%\ContextIME` 已生成独立部署数据，没有写入 `%APPDATA%\Rime`；
- librime 已注册核心、词典、用户词典、拼音处理和候选相关组件；尚未创建输入 session；
- Weasel 受保护注册表、系统文件和用户数据存在性与安装前基线一致；
- `collect-contextime-native-evidence.ps1` 已通过 `Uninstalled` 和 `Installed` 两种状态断言。

本机没有安装 Weasel，因此上述结果不能证明真实并存。

部署日志记录了 `missing input schema: quick5`：上游 `default.yaml` 列出了 `quick5`，但安装包没有对应 schema。默认 `luna_pinyin` 的 schema、prism、reverse 和 table 已成功生成，因此该告警暂不阻塞全拼冒烟测试；仍需在交互测试后决定是否从首版 schema 列表移除无效项。

## 构建证据中发现的问题

早期 Run `33286862743` 的安装器和哈希报告均为 `0.1.2-preview`，但随包 identity report 仍来自旧的 `0.1.0-preview` 元数据。

仓库已将固定 identity 更新为：

- version：`0.1.2-preview`；
- numeric version：`0.1.2.0`；
- install leaf：`contextime-0.1.2-preview`。

当前 0.1.2 工作流增加了构建前和生成报告后的版本一致性门禁。Run `33289352050` 已从 commit `a9b37f6` 完整重建，并确认随包 identity report、安装器文件名和安装目录均为 `0.1.2-preview`。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| Server 互斥锁隔离 | `implemented` / `built` / `statically_verified` | CI 源码门禁和原生构建通过 |
| 0.1.2 安装器 | `built` / `hash_verified` | 文件、大小和 SHA-256 已核对 |
| identity 版本一致性修复 | `implemented` / `built` / `statically_verified` | Run `33289352050` 通过一致性门禁 |
| Windows 安装与 TSF 注册 | `runtime_verified` | Windows 11 Build 26200 已安装并查询到 Profile；仍需干净 Win10/11 覆盖 |
| Server 和 IPC | `runtime_verified` | ContextIME 专属 Pipe 与 Server 进程同时存在 |
| librime 初始化 | `runtime_verified` | 核心组件已初始化；输入 session 仍需实际按键触发 |
| 拼音组合、候选、选词和上屏 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 必须在桌面文本框人工验证 |
| 与 Weasel 并存 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前机器未安装 Weasel |
| 升级、卸载和重装 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚未执行 |

## 下一阻塞

1. 在普通桌面文本框人工验证 `nihao` 的组合、候选、选词和上屏；
2. 验证中英文切换；
3. 执行卸载、残留检查和重装；
4. 在装有 Weasel 的环境重复安装、并存和卸载保护测试。
