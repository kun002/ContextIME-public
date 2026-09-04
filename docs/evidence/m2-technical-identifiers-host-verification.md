# M2.1 技术标识符宿主机验证证据

## 产品结果

本轮为 ContextIME 增加自有 `contextime_developer` 默认方案，在中文状态下复用 librime 的 raw-code recognizer，使常见技术标识符可以原样上屏，并在同一输入会话中继续正常输入中文：

```text
GameObject输入法abc
Vector3.Lerp输入法abc
playerController输入法abc
player_controller输入法abc
```

影响链路：

```text
Key event
→ ContextIME TSF client / IPC / Server
→ librime contextime_developer schema
→ recognizer raw-code candidate
→ commit
```

没有修改 TSF、IPC、候选窗口或 Weasel/librime 产品热路径。`contextime_developer` 通过 `__include: luna_pinyin_simp.schema:/` 复用成熟的简体全拼、候选、词典和用户学习，只增加 ContextIME 自有 schema 和 recognizer patch。

## 固定构建身份

- 安装程序：`contextime-0.2.0-preview-installer.exe`
- 文件大小：`12,050,665` bytes
- SHA-256：`0e33e09931e9fec8bc862c37145cfaab9b3dcc52d07f7ae5ce8adb9b9713f5af`
- Build commit：`e7161f1422af0fdba2b9ac67116e6d7718f866c5`
- CI run：`33346387776`
- CI URL：`https://github.com/kun002/ContextIME/actions/runs/33346387776`
- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- 最终翻页 smoke harness commit：`39d8896ebf2c536d5302ce686ad706ffbe595a7c`
- Windows：Windows 10 Pro，DisplayVersion `25H2`，Build `26200.9168`
- Authenticode：`NotSigned`

测试发生在已登录、解锁的 Windows x64 主开发机交互桌面，不是干净 VM、独立 LAN 笔记本或 RDP Session。

## 实现与 lowerCamelCase 根因

最终 recognizer pattern：

```yaml
([A-Z][-_+.'0-9A-Za-z]*|[a-z][0-9a-z]*[A-Z][0-9A-Za-z]*|[a-z][0-9A-Za-z]*_[0-9A-Za-z_-]*)$
```

三条分支分别覆盖：

- 大写开头的 PascalCase、成员访问和技术片段；
- 小写开头、后续包含大写字母的 lowerCamelCase；
- 包含下划线的 snake_case。

首版 0.2.0 只覆盖大写开头和下划线路径，`playerController` 的小写前缀被拼音引擎解释，实际提交为：

```text
普拉亚二Controller输入法abc
```

失败证据：`artifacts/native-evidence/m2-0.2.0-packaged-lower-camel/`。根因是 recognizer 缺少 lowerCamelCase 分支，不是 TSF、IPC 或候选提交故障。提交 `e7161f1` 增加小写开头且后续含大写字母的分支后通过。

最终安装和测试前已删除实验用 `contextime_developer.custom.yaml` 并重新部署。验证时：

- `%APPDATA%\ContextIME\contextime_developer.custom.yaml` 不存在；
- `%APPDATA%\ContextIME\default.custom.yaml` 为 `0` bytes；
- 用户编译产物包含最终 pattern；
- `luna_pinyin.userdb` 在覆盖安装后仍存在。

因此最终结果来自安装包内 schema，而不是本机实验 custom。

## 构建、覆盖安装与静态门禁

CI 原生构建 `33346387776` 的编译、固定上游 SHA、identity patch、原生前端、安装程序、包审计和 artifact 上传全部通过。

最终包通过 UAC 覆盖安装，installer exit code 为 `0`。安装后 ContextIME Server PID 为 `15176`，官方小狼毫 Server PID 为 `21692`，官方小狼毫未被停止。

只读安装门禁：

- 证据目录：`artifacts/native-evidence/m2-0.2.0-final-installed/20260831T014948Z/`
- `ExpectedState: Installed`
- machine registry、uninstall registry、安装目录、TSF CLSID/TIP、语言 Profile、系统 TSF 文件、Server、IPC Pipe 和用户数据目录全部存在；
- `Mismatches: 0`。

## 四条技术标识符结果

每条均在同一个真实 TSF 会话内执行：

```text
技术标识符
→ Space 原样上屏
→ shurufa
→ Space 上屏“输入法”
→ Shift
→ abc 英文直输
```

| 输入 | 标识符 commit | 后续中文 commit | 最终文本 | 候选变化像素 | 结果 |
|---|---|---|---|---:|---|
| `GameObject` | `GameObject` | `GameObject输入法` | `GameObject输入法abc` | `4,618` | `Passed: true` |
| `Vector3.Lerp` | `Vector3.Lerp` | `Vector3.Lerp输入法` | `Vector3.Lerp输入法abc` | `4,690` | `Passed: true` |
| `playerController` | `playerController` | `playerController输入法` | `playerController输入法abc` | `5,806` | `Passed: true` |
| `player_controller` | `player_controller` | `player_controller输入法` | `player_controller输入法abc` | `5,986` | `Passed: true` |

最终证据目录：

- `artifacts/native-evidence/m2-0.2.0-final-game-object-retry/`
- `artifacts/native-evidence/m2-0.2.0-final-vector3-lerp/`
- `artifacts/native-evidence/m2-0.2.0-final-player-controller/`
- `artifacts/native-evidence/m2-0.2.0-final-player-controller-snake/`

最终安装后的第一次 `GameObject` 自动注入曾只提交空格；同一会话后续 `输入法abc` 正常，原样立即重试及此前相同用例均通过。该次失败证据保留在 `artifacts/native-evidence/m2-0.2.0-final-game-object/`，没有删除或改写。本轮没有把单次自动注入抖动归因于产品运行时，也没有用它证明冷启动已在干净机器通过。

## M1 最小回归与 smoke 门禁修正

最终安装包的 M1 最小回归：

| 用例 | 实际结果 | 结果 |
|---|---|---|
| `shi` + 数字键 `2` | 提交“式”，最终 `式abc` | `Passed: true` |
| `shi` + `=` 翻页一次 + 数字键 `1` | 第二页截图落盘，提交“时”，最终 `时abc` | `Passed: true` |
| `shurufa` + Space | 提交“输入法”，最终 `输入法abc` | `Passed: true` |

翻页首次复验已经生成不同的第一页和第二页截图，第二页数字键 `1` 也提交了第二页首项“事”，但两页文字区域只变化 `892` pixels，低于旧 smoke 共用的 `1500` 门槛，因此 JSON 判为失败。候选窗口从无到有与同一候选窗口内翻页的像素量级不同；测试工具现将两项门槛分开：

- 候选窗口出现：仍为 `1500` pixels；
- 候选页文字变化：`500` pixels。

该修正只影响原创 TSF smoke 的视觉判定，不修改 ContextIME 产品运行时或安装包。重新编译 smoke 后，翻页复验记录 `CandidatePageChangedPixels: 851`、`CandidatePageChanged: true` 和 `Passed: true`。

## 与官方小狼毫并存回归

最终包运行时同时观察到：

```text
\\.\pipe\<user-scope>\ContextIMENamedPipe
\\.\pipe\<user-scope>\WeaselNamedPipe
```

两套 Profile 独立：

- ContextIME Profile：`A200BA94-B1A7-4668-A22B-CA61EC1E79F7`
- 官方小狼毫 Profile：`3D02CAB6-2B8E-4781-BA20-1C9267529467`

官方小狼毫 Profile 的真实 TSF 回归提交 `輸入法abc`，`Passed: true`；ContextIME 的简体回归提交 `输入法abc`，`Passed: true`。证据目录为 `artifacts/native-evidence/m2-0.2.0-final-weasel-coexistence/`。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| ContextIME developer schema 和 recognizer | `implemented` | 自有 schema，复用 `luna_pinyin_simp` |
| 0.2.0 Preview 原生包 | `built` | CI run `33346387776` 成功 |
| 最终 schema、固定上游和安装隔离 | `statically_verified` | 包审计及安装后只读门禁 0 mismatch |
| 最终包覆盖安装、Server 和双 Pipe | `runtime_verified` | 主开发机交互 Session |
| 四条技术标识符、后续中文和 Shift 英文 | `real_machine_verified` | Windows Build `26200.9168` 主开发机 |
| M1 数字选词、翻页、简体和小狼毫并存回归 | `real_machine_verified` | 最终安装包与重新编译的 smoke harness |
| 干净 Windows 10/11 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前主机有开发和安装历史 |
| 独立 LAN 笔记本 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未连接、未触发 |
| RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未执行 |
| 安装程序签名 | `unverified` | Preview 为 `NotSigned` |

## M2.1 结论与下一项

当前只关闭 M2.1 技术标识符：PascalCase、lowerCamelCase、snake_case 和成员访问片段可在中文状态原样上屏，并在同一会话继续中文输入。没有把路径、URL、命令、Context Engine、编辑器 Adapter、项目词库或个性化描述为已完成。

下一项阻塞是 M2.2 路径识别。进入前先收口 PR #13；干净 Windows、LAN、RDP 和 Release Gate 继续保留 `REAL_WINDOWS_VERIFICATION_REQUIRED`。
