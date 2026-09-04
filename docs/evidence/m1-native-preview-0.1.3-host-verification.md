# M1 ContextIME 0.1.3 宿主机验证证据

## 产品结果

ContextIME 0.1.3 只推进 Native IME Baseline：默认输出简体中文，并让安装、0.1.2 覆盖升级、卸载和重装形成可重复闭环。

本轮影响链路：

```text
Key Event
→ TSF Client
→ ContextIME IPC
→ ContextIME Server
→ librime luna_pinyin_simp
→ Composition / Candidate
→ Commit
```

不包含 Context Engine、编辑器 Adapter、项目词库或其他高级功能。

## 固定上游与构建身份

- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- ContextIME build commit：`aa80d79a659511c0a00f534011bb39698ec84e9c`
- Workflow：`ContextIME Native Preview 0.1.3`
- Run ID：`33293955774`
- CI 结果：`success`

CI 同时通过 identity patch、native evidence 和 TypeScript 回归工作流。`npm run check` 通过 33/33 测试。

## 安装程序

- 文件：`contextime-0.1.3-preview-installer.exe`
- 大小：`12,049,788` bytes
- SHA-256：`a1e02e04624fe9f11611dd06cb9300bdeb057886da32693b83a6a1ee10e40dd2`
- 产品：`ContextIME`
- 版本：`0.1.3-preview`
- Authenticode：`NotSigned`

该文件是用户安装用 `.exe`，不是 CI artifact ZIP。它仍是未签名 Preview，不是正式稳定版。

## 根因与最小修复

默认输出繁体的直接原因是 `default.yaml` 把 `luna_pinyin` 放在首个方案。0.1.3 不重新实现拼音算法，而是把已随包构建的 librime `luna_pinyin_simp` 设为首个默认方案。安装后用户 build 的 `default.yaml` 首项已实际查询为 `luna_pinyin_simp`。

卸载残留由四个独立问题构成：

1. 32 位 `WeaselSetup` 在关闭 WOW64 重定向前重复注销 32 位 DLL，没有执行 64 位注销；
2. 上游 TIP 路径拼成 `Microsft`，并使用了错误的 `HKEY_CLASSES_ROOT`；
3. 语言 Profile 卸载依赖 ContextIME 不拥有的 Weasel `Hant` 注册值；
4. 激活 TSF 后，当前用户 `HKCU\Software\Microsoft\CTF\TIP\{ContextIME CLSID}` 下仍有 `LanguageProfile/Enable` 状态。

最终修复在正确的 32/64 位时机注销 DLL，无条件移除 ContextIME 简/繁两个语言 Profile，并只删除 ContextIME 自有 GUID 对应的 HKLM 与 HKCU TIP 树。输入热路径和 librime 候选逻辑没有改动。

## 原创 TSF 宿主机工具

仓库中的以下工具不依赖人工输入或编辑器插件：

- `native/tests/tsf-smoke/ContextIMETsfSmoke.cs`
- `native/scripts/run-contextime-tsf-smoke.ps1`

它会：

1. 创建普通 WinForms `TextBox`；
2. 通过 `ITfInputProcessorProfiles` 激活 ContextIME Profile；
3. 自动输入 `shurufa`；
4. 在上屏前用屏幕像素差检测候选 UI 并保存 PNG；
5. 按空格选择第一候选；
6. 按 Shift 切换英文并输入 `abc`；
7. 输出包含 Profile、composition/candidate 视觉、commit 和进程窗口信息的 JSON。

这是真实 Windows TSF/桌面输入测试，不是对 librime 的直接函数调用。它不能替代 RDP、数字选词、翻页或多应用人工兼容性测试。

## 宿主机运行证据

环境：Windows x64，DisplayVersion `25H2`，Build `26200.9168`。不是干净 VM。

最终安装状态断言全部通过：

- ContextIME 机器注册表与卸载项存在；
- 32/64 位 TSF CLSID 与 TIP 存在；
- Windows 当前用户语言列表包含 ContextIME Profile；
- `contextime.dll`、Server 和 `ContextIMENamedPipe` 同时存在；
- `%APPDATA%\ContextIME` 独立用户目录存在；
- 卸载项实际显示 `ContextIME输入法` / `0.1.3-preview`。

从 0.1.2 覆盖升级到最终 0.1.3 后，TSF 冒烟结果：

- 请求与活动 Profile：`A200BA94-B1A7-4668-A22B-CA61EC1E79F7`；
- 输入：`shurufa`；
- 上屏：`输入法`；
- 英文切换后最终文本：`输入法abc`；
- 候选检测：`screen-pixel-delta`；
- 候选区域变化像素：`114,639`；
- `CommitMatched: true`；
- `EnglishModeMatched: true`；
- `Passed: true`。

同一最终包在卸载后重装，也再次通过 `shurufa → 输入法`、候选截图、空格上屏和 Shift 英文输入。

开发验证期间曾有一次非通过结果 `没输入法abc`。对应候选截图清楚显示 composition 前多出意外的 `m`；同一二进制立即复跑以及后续卸载重装、0.1.2 覆盖升级后的测试均通过。该失败没有被计入通过证据，也没有通过改动 librime 或候选层来掩盖。

## 卸载、数据保留与升级

最终包卸载后的只读证据：

```json
{
  "machineRegistry": false,
  "uninstallRegistry": false,
  "installDirectory": false,
  "tsfClsid": false,
  "tsfTip": false,
  "languageProfile": false,
  "systemTsfDll": false,
  "serverProcess": false,
  "ipcPipe": false,
  "userDataDirectory": true
}
```

门禁结果为 0 个 mismatch。卸载前存在的 `%APPDATA%\ContextIME` 36 个文件在卸载后均仍存在，`luna_pinyin.userdb` 在重装和覆盖升级后仍存在。测试机原本没有 `%APPDATA%\Rime`，卸载前后均未创建该目录。

覆盖升级使用 SHA-256 为 `8cb4a9cdb29c3dcba1e615465315de4dfac40ffba8ed0b6309327656696175f9` 的 0.1.2 安装包作为基线。最终 0.1.3 覆盖后，旧 `contextime-0.1.2-preview` 目录消失，安装状态断言和简体 TSF 冒烟均通过。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| 简体默认方案 | `implemented` / `built` / `runtime_verified` | 用户 build 首项为 `luna_pinyin_simp`；真实 commit 为 `输入法` |
| 组合、候选、空格上屏 | `runtime_verified` | 普通 WinForms TextBox 与真实 TSF Profile；保存候选 PNG/JSON |
| 中文/英文状态 | `runtime_verified` | Shift 后 `abc` 直输匹配 |
| 安装、卸载、重装 | `runtime_verified` | 宿主机多轮执行，安装/卸载状态均 0 mismatch |
| 0.1.2 → 0.1.3 覆盖升级 | `runtime_verified` | 旧目录删除，最终注册、Server、Pipe 与简体输入通过 |
| 用户库保留 | `runtime_verified` | 卸载前文件均保留；`luna_pinyin.userdb` 升级后存在 |
| 与小狼毫并存 | `runtime_verified` | 后续已在同一宿主安装官方 0.17.4 并完成双 Profile 输入与双向卸载隔离；见独立并存证据 |
| 干净 Windows 10/11 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前是已有开发历史的宿主机 |
| 数字选词、翻页和应用矩阵 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前自动工具只验证第一候选空格上屏 |
| RDP 与连续 30 分钟输入 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚未执行 |
| Authenticode | `unverified` | Preview 未签名 |

## 真机复验步骤

1. 核对安装器大小与 SHA-256 后运行 `.exe`；
2. 在 Windows 输入法列表确认 `ContextIME输入法`；
3. 运行 `native/scripts/collect-contextime-native-evidence.ps1 -ExpectedState Installed -FailOnMismatch`；
4. 运行 `native/scripts/run-contextime-tsf-smoke.ps1 -InputText shurufa -ExpectedText 输入法 -ExpectedEnglish abc`；
5. 在记事本、浏览器、VS Code 和 Terminal 人工验证数字选词、翻页、焦点和中英文状态；
6. 卸载 ContextIME；
7. 运行 `native/scripts/collect-contextime-native-evidence.ps1 -ExpectedState Uninstalled -FailOnMismatch`；
8. 确认 `%APPDATA%\ContextIME` 的保留策略符合预期，并确认小狼毫文件、注册表与 `%APPDATA%\Rime` 未变化；
9. 重装并重复第 3、4 步。

## 下一阻塞

1. 在干净 Windows 10/11 VM 重复已通过的宿主机并存、安装、升级、卸载和重装；
2. 补数字选词、翻页、常用应用、RDP 与 30 分钟连续输入证据；
3. 正式发布前增加 Authenticode 签名。
