# M1.1 原生身份隔离

## 产品结果

生成第一个真正显示为 `ContextIME` 的 Windows 输入法安装包，并保证它不会覆盖已安装的小狼毫系统文件、TSF 标识、注册表、用户数据和 IPC 通道。

该阶段只解决产品身份和并存安全，不接入代码/注释规则、项目词库或 VS Code 上下文。

## 固定上游

- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`

## 必须隔离的资源

| 资源 | 小狼毫上游 | ContextIME Preview |
|---|---|---|
| TSF Text Service CLSID | `A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A` | `9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9` |
| TSF Profile GUID | `3D02CAB6-2B8E-4781-BA20-1C9267529467` | `A200BA94-B1A7-4668-A22B-CA61EC1E79F7` |
| 语言栏 GUID | 上游 GUID | 独立 GUID |
| 显示属性 GUID | 上游 GUID | 独立 GUID |
| 系统 TSF DLL | `weasel.dll` / `weaselx64.dll` | `contextime.dll` / `contextimex64.dll` |
| 系统兼容 IME | `weasel.ime` / `weaselx64.ime` | `contextime.ime` / `contextimex64.ime` |
| 机器注册表 | `Software\Rime\Weasel` | `Software\ContextIME` |
| 卸载注册表 | `...\Uninstall\Weasel` | `...\Uninstall\ContextIME` |
| 安装目录 | `Program Files\Rime` | `Program Files\ContextIME` |
| 用户数据 | `%APPDATA%\Rime` | `%APPDATA%\ContextIME` |
| 日志 | `%TEMP%\rime.weasel` | `%TEMP%\contextime` |
| IPC 窗口与管道 | Weasel 名称 | ContextIME 独立名称 |
| 部署互斥锁 | Weasel 名称 | ContextIME 独立名称 |
| 服务名 | `WeaselInputService` | `ContextIMEInputService` |
| 自启动项名称 | `WeaselServer` | `ContextIMEServer` |

## 实施方式

不在 ContextIME 仓库复制整个 Weasel 源码。

构建流程：

```text
读取固定 identity
→ checkout 精确 Weasel 提交和子模块
→ 应用可审计的 PowerShell identity patch
→ 静态检查旧 GUID、注册表、系统文件名和 IPC 名称
→ 编译 Weasel 派生前端
→ 将系统 DLL/IME 重命名为 contextime*
→ 生成 ContextIME NSIS 安装包
→ 输出 patch、identity、哈希和工具链证据
```

这样可以保持上游升级路径清晰：每次升级先切换固定上游，再重新应用和审计小型 patch stack。

## 第一预览版保留的内部名称

为了降低第一步改动范围，以下内部辅助程序暂时保留上游文件名：

- `WeaselServer.exe`
- `WeaselSetup.exe`
- `WeaselDeployer.exe`

它们位于 ContextIME 独立安装目录，使用独立 IPC、服务名、注册表和数据目录，不应与小狼毫实例通信。

后续可以在不影响 TSF 基线的独立 PR 中重命名这些内部可执行文件。内部文件名未改不代表产品仍是小狼毫。

## CI 验收

- 精确验证 Weasel 与 librime 提交；
- identity patch 可重复应用；
- `git diff --check` 通过；
- 新旧 TSF GUID 不相同；
- 安装脚本不包含小狼毫注册表、卸载键或 Program Files 路径；
- 生成 `contextime.dll/.ime` 与 x64 文件；
- 生成 `contextime-0.1.0-preview-installer.exe`；
- 输出安装包 SHA-256、完整 identity 和源代码 patch。

## 真机验收

CI 通过后仍必须在装有小狼毫的 Windows 桌面验证：

1. 安装 ContextIME 时不要求卸载小狼毫；
2. Windows 输入法列表同时出现小狼毫和 ContextIME；
3. 两者分别能够激活；
4. ContextIME 拼音组合、候选和上屏正常；
5. 两个服务能够同时运行，不串候选、不共享用户词频；
6. 卸载 ContextIME 后小狼毫仍能使用；
7. `%APPDATA%\Rime`、小狼毫注册表和系统文件未被删除或覆盖。

在真机证据完成前，只能称为“身份隔离预览包”，不能宣称并存验收完成。
