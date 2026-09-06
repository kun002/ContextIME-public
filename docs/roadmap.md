# ContextIME Roadmap

## 当前路线

ContextIME 的正式产品是可安装、可分发、可配置的 Windows 开发者输入法。

当前 VSIX、输入状态控制和语法识别代码归类为原型与可复用研究资产。正式路线以原生 Windows 输入法为主线。

## M0：路线重置与上游审计

- [x] 明确产品不是 VS Code 插件或输入法切换器。
- [x] 编写产品意图、产品规划和技术路线。
- [x] 增加根目录 `AGENTS.md` 路线约束。
- [x] 将现有 VSIX 和 TypeScript 包标记为原型资产。
- [x] 固定 librime 1.13.1 与提交 `1c233581...`。
- [x] 固定 Weasel 0.17.4 与提交 `9cc96e20...`。
- [x] 完成 GPL-3.0、BSD-3-Clause、子模块和升级边界审计。
- [x] 建立 `native/upstream.lock.json`。
- [x] 在 Windows 2022 CI 复现上游安装程序构建。
- [x] 生成安装程序、提交报告、工具链报告和 SHA-256 证据。
- [x] 建立可审计的 ContextIME identity patch-stack。
- [ ] 将现有控制器原型迁入 `prototype/vscode-controller/`。

M0 CI 证据：

- 安装程序：`weasel-0.17.4.0-installer.exe`
- SHA-256：`bd2a07f851f43bbf333da4d03c69bf087dda50b46afd37898dddab1a7c06adeb`
- 证据文档：[`docs/evidence/m0-upstream-build.md`](evidence/m0-upstream-build.md)

上游基线只用于证明构建链成立。后续真机验收统一使用 ContextIME 身份隔离预览，不再把未修改的小狼毫安装包作为产品测试包。

## M1：原生 Windows 输入法基线

### M1.1 身份隔离预览

- [x] 建立 ContextIME 独立产品标识和品牌。
- [x] 与原版 Weasel 使用不同的 TSF Text Service、Profile 和辅助 GUID。
- [x] 使用独立注册表、卸载键和自启动项名称。
- [x] 使用独立安装目录、用户数据目录和日志目录。
- [x] 使用独立 IPC 窗口、命名管道、服务名和部署互斥锁。
- [x] 使用 `contextime.dll/.ime`，不覆盖系统目录中的 `weasel.dll/.ime`。
- [x] 禁用上游 Weasel 自动更新通道。
- [x] 生成 `contextime-0.1.0-preview-installer.exe`。
- [x] 将固定 identity、GPL 源代码 patch、提交和工具链证据随 artifact 输出。
- [x] 在装有官方小狼毫 0.17.4 的真实 Windows 桌面验证并存安装。
- [x] 验证卸载 ContextIME 后小狼毫系统文件、注册表、数据和输入保持完整。

M1.1 CI 证据：

- 安装程序：`contextime-0.1.0-preview-installer.exe`
- 大小：`12,048,357` bytes
- SHA-256：`1094513dfd2aac6c8d9d975483d6cb98bedaa1767bc62d55569c571c7f5ae902`
- 证据文档：[`docs/evidence/m1-identity-preview-build.md`](evidence/m1-identity-preview-build.md)

### M1 输入链路修复记录

- [x] 0.1.1 修复 TSF 残留注册表/互斥锁引用、默认中文会话和资源乱码。
- [x] 确认“装有 Weasel 时 ContextIME 只能英文透传”的根因是共享 Server 单实例互斥锁。
- [x] 0.1.2 使用独立 `(CONTEXTIME)NativeServer-<username>` 互斥锁。
- [x] 2026-08-30 从 main 重建 0.1.2 原生安装程序并核对 SHA-256。
- [x] 实现只读 Native 安装/TSF/Server/IPC/数据目录证据采集门禁。
- [x] 修正固定 identity 的 0.1.2 版本元数据并增加 CI 一致性断言。
- [x] 从包含 identity 一致性修复的 commit `a9b37f6` 完整重建。
- [x] Windows 11 真机安装并确认 TSF Profile、Server、Pipe 和独立用户数据。
- [x] 0.1.3 将默认方案切换为 librime 已打包的简体全拼 `luna_pinyin_simp`。
- [x] 实现原创 WinForms/TSF 冒烟宿主，自动验证组合、候选、上屏和中英文切换。
- [x] Windows Build 26200.9168 宿主机验证 `shurufa → 输入法` 与 Shift 后 `abc`。
- [x] 修复并验证 32/64 位 CLSID、HKLM/HKCU TIP 和 Windows 语言 Profile 卸载清理。
- [x] 验证 0.1.2 覆盖升级到最终 0.1.3、卸载保留用户库，以及卸载后重装。
- [x] 验证 ContextIME 与官方小狼毫 0.17.4 的双 TSF Profile、双 Server、双 Pipe 和双向卸载隔离。

0.1.2 CI 重建证据：

- 安装程序：`contextime-0.1.2-preview-installer.exe`
- 大小：`12,046,147` bytes
- SHA-256：`8cb4a9cdb29c3dcba1e615465315de4dfac40ffba8ed0b6309327656696175f9`
- 证据文档：[`docs/evidence/m1-native-preview-0.1.2-build.md`](evidence/m1-native-preview-0.1.2-build.md)

0.1.3 最终宿主机证据：

- 安装程序：`contextime-0.1.3-preview-installer.exe`
- 大小：`12,049,788` bytes
- SHA-256：`a1e02e04624fe9f11611dd06cb9300bdeb057886da32693b83a6a1ee10e40dd2`
- Build commit：`aa80d79a659511c0a00f534011bb39698ec84e9c`
- 证据文档：[`docs/evidence/m1-native-preview-0.1.3-host-verification.md`](evidence/m1-native-preview-0.1.3-host-verification.md)
- 并存证据：[`docs/evidence/m1-weasel-coexistence-host-verification.md`](evidence/m1-weasel-coexistence-host-verification.md)
- 数字选词与翻页证据：[`docs/evidence/m1-number-selection-and-paging-host-verification.md`](evidence/m1-number-selection-and-paging-host-verification.md)
- 五应用证据：[`docs/evidence/m1-five-application-host-verification.md`](evidence/m1-five-application-host-verification.md)
- 30 分钟稳定性与宿主机基线证据：[`docs/evidence/m1-30-minute-stability-host-verification.md`](evidence/m1-30-minute-stability-host-verification.md)

### M1.2 输入法真机基线

- [x] 安装后注册到 Windows 输入法列表（Windows 11 Build 26200）。
- [x] 与官方小狼毫 0.17.4 同时出现在输入法列表。
- [x] 在标准桌面 WinForms TextBox 激活 ContextIME 并完成自动交互输入验收。
- [x] 接入并验证 librime 简体全拼方案 `luna_pinyin_simp`。
- [x] 显示拼音组合文本。
- [x] 显示候选窗口并保存视觉证据。
- [x] 支持空格选择第一候选并上屏。
- [x] 验证数字选词和候选翻页。
- [x] 支持中文和英文模式，已验证 Shift 后英文直输。
- [ ] 验证用户词频和自定义用户词典行为；卸载保留 `luna_pinyin.userdb` 已验证。
- [x] 在记事本、浏览器、VS Code、Visual Studio、Terminal 验证输入。
- [x] 连续输入 30 分钟无崩溃，并完成 Server PID、私有内存和句柄审计。

主开发机状态：`M1_HOST_BASELINE = VERIFIED`。这不代表干净 Windows、独立 LAN 笔记本、RDP 或 Release Gate 已验证。

M1 验收：

> ContextIME 安装后出现在 Windows 输入法列表，能够独立完成拼音组合、候选显示、选词和文本上屏，且不会覆盖原版 Weasel。

M1 完成前，不开发新的编辑器专属高级功能。

## M2：开发者基础方案

- [x] 建立开发者默认输入方案（M2.1 `contextime_developer`，复用 `luna_pinyin_simp`）。
- [ ] 配置中文/英文标点。
- [x] 支持 PascalCase、lowerCamelCase、snake_case 和成员访问技术标识符。
- [x] 支持有限路径片段：盘符路径和以普通目录段开头的无空格相对路径。
- [ ] 支持点号相对路径、UNC、Unix 绝对路径和空格路径。
- [x] 支持有限 URL 片段：小写 HTTP/HTTPS、`www`、小写裸域名及路径/query/fragment。
- [ ] 支持无 scheme localhost/IP、uppercase scheme 和含空格 URL。
- [x] 支持固定命令根片段：`git`、`npm`、`dotnet`、`cargo`、`cd`。
- [x] 技术标识符上屏后，同一会话的后续拼音仍保持中文状态。
- [x] 在同一真实 TSF 会话连续输入简体中文、技术标识符、路径、URL、命令，再恢复简体中文和 Shift 英文。
- [x] 系统重启后复验 M2 连续混输、ContextIME 注册/Server/Pipe 和官方小狼毫进程并存。
- [ ] 支持项目术语手动词典。
- [ ] 建立设置界面。
- [ ] 支持配置导入、导出和迁移。
- [ ] 所有自动规则允许用户关闭；命令片段规则的关闭和根列表覆盖已实现并静态验证，其余规则尚未统一完成。

M2.1 主开发机证据：

- 安装程序：`contextime-0.2.0-preview-installer.exe`
- SHA-256：`0e33e09931e9fec8bc862c37145cfaab9b3dcc52d07f7ae5ce8adb9b9713f5af`
- Build commit：`e7161f1422af0fdba2b9ac67116e6d7718f866c5`
- 证据文档：[`docs/evidence/m2-technical-identifiers-host-verification.md`](evidence/m2-technical-identifiers-host-verification.md)

M2.1 只关闭技术标识符；路径、URL、命令和 M2 整体验收仍未完成。

M2.2 主开发机证据：

- 安装程序：`contextime-0.2.1-preview-installer.exe`
- SHA-256：`49b856a9c6ed5814e14c810d33ab70d67caea619bb359f1df89f8005b2a4f689`
- Build commit：`f82e0788854841df6ee1d34cce6835735ad34429`
- 证据文档：[`docs/evidence/m2-path-fragments-host-verification.md`](evidence/m2-path-fragments-host-verification.md)

M2.2 只关闭盘符路径和以普通目录段开头的无空格相对路径；点号相对路径、UNC、Unix 绝对路径、空格路径、URL、命令和 M2 整体验收仍未完成。

M2.3 主开发机证据：

- 复用 `contextime_developer` 继承的 librime URL recognizer，无产品代码修改；
- 已安装版本：`contextime-0.2.1-preview`；
- 证据文档：[`docs/evidence/m2-url-fragments-host-verification.md`](evidence/m2-url-fragments-host-verification.md)

M2.3 只关闭小写 HTTP/HTTPS、`www`、小写裸域名及其路径/query/fragment；无 scheme localhost/IP、uppercase scheme、含空格 URL、命令和 M2 整体验收仍未完成。

M2.4 主开发机证据：

- 安装程序：`contextime-0.2.3-preview-installer.exe`
- 文件大小：`12,054,194` bytes
- SHA-256：`8831be3c3896e7092e599f3f102d61776b3170d5b4951cb1b45a09988051131d`
- Build commit：`9a1ace7fbf4284b1dd779b6c1e4e4337700897c7`
- CI run：`33389352096`
- 证据文档：[`docs/evidence/m2-command-fragments-host-verification.md`](evidence/m2-command-fragments-host-verification.md)

`0.2.2-preview` 因安装器遗漏必需的 `data\rime.lua` 被真机拒绝，不作为可交付版本。`0.2.3-preview` 增加安装器内容和 Lua 哈希门禁后，固定命令根、取消恢复、既有输入能力及官方小狼毫并存回归通过。M2.4 不代表 M2 整体验收完成；下一项是标识符、路径、URL 和命令的连续混输真机验收。

M2 连续混输主开发机证据：

- 固定已安装版本：`contextime-0.2.3-preview`；
- 最终文本：`你好GameObjectAssets/Textures/UIhttps://github.comgit status && npm test输入法abc`；
- 重启前完整 sequence 三次通过，重启后再通过一次；
- 重启后原生安装审计：`Mismatches: 0`；
- 证据文档：[`docs/evidence/m2-mixed-input-host-verification.md`](evidence/m2-mixed-input-host-verification.md)

主开发机状态：`M2_MIXED_INPUT_HOST = VERIFIED`。这只关闭 M2.1 至 M2.4 已声明有限范围的连续混输，不把仍未完成的标点、设置、导入导出、全部规则开关、干净机、LAN 或 RDP 描述为已验证。

M2 验收：

- [x] 中文状态下可连续输入类名、路径、URL 和命令；
- [x] 四次完整 sequence 未出现模式抖动，命令后恢复简体中文；
- [ ] 修改配置不需要重装输入法；命令 custom 路径已静态验证，尚未完成统一设置真机验收。

## M3：Context Engine 与上下文服务

- [x] 定义平台无关的 C++ Context Engine 单一决策 owner 和 `CHINESE / ENGLISH / KEEP` 输出。
- [x] 固定 composition/candidate → lock → manual → user → project → syntax → surface → app → KEEP 优先级。
- [x] 实现 automation disabled、context unavailable 和未知上下文的 `KEEP` 故障回退。
- [x] Context Engine Windows/Ubuntu 严格编译和单元门禁通过（每个平台 61 assertions）。
- [x] 建立本地 Context Service，并将独立 executable 接入 `0.3.0-preview` 安装器和 companion-process 生命周期；真实安装仍待主机验证。
- [x] 定义版本化 Windows Named Pipe v1 协议（双平台 codec + Windows runner pipe integration）。
- [x] 识别前台应用（process basename/window class；不读取标题或保留完整路径）。
- [ ] 识别输入区域（独立终端 surface 已完成；editor/browser/document generic surface 仍保持 UNKNOWN）。
- [x] 服务缺失、失败或超时不进入按键路径并回退普通输入；fixture/静态门禁已通过，真实应用故障注入仍需真机验证。
- [ ] 将可复用 `policy-core` 迁入共享核心。
- [x] 将候选/composition 保护接入固定 Weasel TSF 实时状态；已构建，真实候选窗口验收仍未完成。
- [x] 将 lock/manual override 优先级接入固定 Weasel TSF 实时状态；5 秒保护和 1 秒自动 origin guard 已构建，真机验收仍未完成。

M3.1 Context Engine 核心设计：

- 代码：`src/context-engine/`；
- 测试：`tests/context-engine/`；
- 设计文档：[`docs/context-engine.md`](context-engine.md)；
- CI 证据：[`docs/evidence/m3-context-engine-core-ci.md`](evidence/m3-context-engine-core-ci.md)；
- 不依赖 Node.js、VS Code、Windows API、文件或网络；
- 尚未接入 Context Service、Named Pipe、TSF 或 Adapter，不改变当前原生输入行为。

M3.2 Context Service 当前实现：

- 代码：`src/context-service/`；
- 测试：`tests/context-service/`；
- 设计和协议：[`docs/context-service.md`](context-service.md)；
- CI 证据：[`docs/evidence/m3-context-service-ci.md`](evidence/m3-context-service-ci.md)；
- 使用独立 `\\.\pipe\ContextIME.ContextService.v1`，不复用原生输入引擎 `ContextIMENamedPipe`；
- 服务不存在、断开、超时或协议错误统一回退 `KEEP / CONTEXT_UNAVAILABLE`；
- Windows/Ubuntu 严格编译、46 项 protocol 断言和 42 项 Windows pipe 断言已通过；尚未安装、启动或接入 TSF/IME state applier。

M3.3 非阻塞 decision cache 当前实现：

- 代码：`src/context-service/include/contextime/decision_cache.h` 和 `src/context-service/src/decision_cache.cpp`；
- 测试：`tests/context-service/decision_cache_test.cpp`；
- 设计：[`docs/decision-cache.md`](decision-cache.md)；
- CI 证据：[`docs/evidence/m3-decision-cache-ci.md`](evidence/m3-decision-cache-ci.md)；
- 热路径只执行共享 Context Engine 和最多 3 次 lock-free atomic snapshot，不调用 IPC 或系统时钟；
- transport failure、竞争、空值或 TTL 到期立即 `KEEP / CONTEXT_UNAVAILABLE`；
- Windows/Ubuntu 严格编译和每个平台 65 项断言已通过；尚未接入真实 TSF state applier。

M3.4 前台应用 Context Source 当前实现：

- 代码：`src/context-service/include/contextime/application_context.h`、`application_context.cpp` 和 `foreground_application_win.cpp`；
- 测试：`tests/context-service/application_context_test.cpp` 和 Windows capture assertions；
- 设计：[`docs/application-context.md`](application-context.md)；
- CI 证据：[`docs/evidence/m3-application-context-ci.md`](evidence/m3-application-context-ci.md)；
- 只保留 process basename/window class，不读取或保存 window title/完整路径；
- 只有可靠独立终端 surface 建议 English；editor/browser/document surface 无法可靠判断时保持 `UNKNOWN/KEEP`；
- Windows/Ubuntu 每个平台 31 项分类断言、Windows 5 项 capture 边界增量和 47 项 pipe regression 已通过；尚未在用户交互桌面或真实 TSF state applier 中验证。

M3.5 IME Host Context Bridge core：

- 代码：`src/ime-host/`；
- 测试：`tests/ime-host/`；
- 设计：[`docs/ime-context-bridge.md`](ime-context-bridge.md)；
- CI 证据：[`docs/evidence/m3-ime-context-bridge-ci.md`](evidence/m3-ime-context-bridge-ci.md)，run `33477387139`；
- 后台 worker 只执行有界 Context Service IPC，并作为 Decision Cache 单 writer；
- focus generation、TTL 和 active guard 阻止旧应用 decision 在新焦点或失焦后落地；
- TSF 侧 state applier 接口只接受已经用最新 composition/candidate/manual 状态重评估的 decision；
- 该 core 的固定 CI 先验证 worker/cache/state-applier 边界；随后 M3 Weasel patch 已完成真实 TSF owner thread 源码接线。

M3 固定 Weasel TSF Context Bridge：

- patch：`native/scripts/apply-contextime-m3-ime-context-bridge.ps1` 和 `native/patches/m3-ime-context-bridge/ContextBridge.cpp`；
- CI 证据：[`docs/evidence/m3-weasel-context-bridge-ci.md`](evidence/m3-weasel-context-bridge-ci.md)，run `33480300841`；
- `ActivateEx / OnSetFocus` 管理 worker，worker callback 只 `PostMessageW`，TSF owner thread 才读取即时安全状态并应用 `ascii_mode`；
- Context Service IPC 不进入 `_ProcessKeyEvent`，服务不可用时保留普通 Weasel/librime 输入；
- composition/candidate、5 秒 manual override 和 1 秒 automatic-origin guard 已接入固定 Weasel 源码；
- 固定上游 x64/Win32 TSF DLL 已 `built / statically_verified`，canonical fixtures 为 runner `runtime_verified`；
- 该 compile-only bridge gate 没有生成安装器或改变已发布的 `0.2.3`；后续生命周期交付见 M3.6；
- 交互桌面自动切换和所有真实保护/故障场景仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`。

M3.6 Context Service 生命周期与 `0.3.0-preview`：

- 使用普通隐藏 companion process，不引入 Windows SCM 服务或额外 supervisor；
- 独立 mutex、stop event、Named Pipe 和 autorun 标识已固定；
- singleton、`--quit`、停止后重启在固定 Windows runner 上 `runtime_verified`；
- NSIS 首次安装、覆盖升级和卸载路径已 `implemented / built / statically_verified`；
- 安装程序：`contextime-0.3.0-preview-installer.exe`；
- 大小：`12,133,784` bytes；
- SHA-256：`5f70f757fa98c21251f416f0c2d18b975655911ceeb29c53324f16e50441daba`；
- Build commit：`697510714340a761631d4ec76b3261d181c97654`；
- CI 证据：[`docs/evidence/m3-context-service-lifecycle-ci.md`](evidence/m3-context-service-lifecycle-ci.md)，run `33491126742`；
- 主开发机已完成 `0.2.3 → 0.3.0` 覆盖升级、安装后启动、singleton、幂等 `--quit`、restart 和服务停止时普通 TSF 输入回退；
- 主机证据：[`docs/evidence/m3-context-service-lifecycle-host-verification.md`](evidence/m3-context-service-lifecycle-host-verification.md)；
- 首次安装、重新登录 autorun、真实 TSF 自动切换、composition/candidate/manual 保护和卸载清理尚未验证。

M3 验收：

- 无编辑器插件时输入法仍完整可用；
- Context Service 故障不阻塞按键和候选。

## M4：VS Code 可选 Adapter

- [x] M4.1 在现有 Context Service Pipe 上增加固定长度 editor context v1 message type `3/4`。
- [x] M4.1 建立 service-owned 2 秒 TTL store、真实前台 VS Code 校验和保守 Context Engine 映射。
- [x] M4.2 将旧 VS Code controller 改为 report-only Adapter。
- [x] 删除 Adapter 内的 Windows mode polling/switching、policy owner、habit learning、manual lock 和持久化状态。
- [x] 只上报 focused、surface、syntax 和最长 20-byte normalized language ID。
- [x] 本地 Tree-sitter 使用不透明 `document-N` cache ID，不上报源码、URI、项目路径或 selection。
- [x] 实现 debounce、1 秒 heartbeat、250 ms 总 deadline、严格 response/ACK 和失败诊断。
- [x] Adapter protocol、scheduler、成功/拒绝/超时/断开/服务缺失单测与 bundle capability audit 通过。
- [ ] 安装含 M4.1 的新版原生包，在真实交互桌面完成 VS Code code/comment/Markdown/terminal 端到端验收。

M4.2 设计：[`docs/vscode-adapter.md`](vscode-adapter.md)。验证证据：[`docs/evidence/m4-vscode-report-only-adapter-ci.md`](evidence/m4-vscode-report-only-adapter-ci.md)。M4.1 CI 证据：[`docs/evidence/m4-editor-context-protocol-ci.md`](evidence/m4-editor-context-protocol-ci.md)。Adapter 构建与 CI 不能替代 installed service/TSF 真机验收。

## M5：项目词库

- [x] 项目标识和词库隔离：workspace 本地路径域分隔哈希为 128-bit opaque ID，Store 按 ID 分文件。
- [x] M5.1 建立文件型词库 core：匿名项目 ID 校验、分项目持久化、增量 upsert、查看、禁用和整库删除 API。
- [x] M5.2 优先读取 VS Code Language Server document symbols，只保留 `name/kind/children`。
- [ ] 增加 compilation database、project file、file/directory/Unity asset 等后续有界 source。
- [x] M5.2 symbol source 允许 C#、TypeScript/JavaScript、C/C++、Python language ID。
- [ ] 增量更新资源名和用户批准的技术术语。
- [x] 提供词库查看、删除和禁用。
- [x] M5.3 在候选窗口以 `〔项目·类型〕` 标记项目候选来源。
- [x] M5.5 手动术语写入项目词库与管理入口（真机验证）。
- [x] M5.5 手动术语候选上屏安装版验收。
- [ ] 资源名等待后续有界 source。
- [x] M5.2 协议不承载或保存完整源码、URI、workspace 路径、range、detail、container、Token 或环境变量值。
- [x] 大项目索引不阻塞输入。

M5.1 当前边界：

- 代码：`src/project-indexer/`；
- 设计：[`docs/project-dictionary.md`](project-dictionary.md)；
- CI 证据：[`docs/evidence/m5-project-dictionary-foundation-ci.md`](evidence/m5-project-dictionary-foundation-ci.md)，run `33715022860`；
- Windows/MSVC 和 Ubuntu/g++ 均以 warnings-as-errors 编译，分别通过 38 项断言；
- Store 只允许后台单 owner 调用，不进入 TSF key-event 热路径；
- M5.1 证据只覆盖 storage core；workspace ID、Language Server ingestion 和 Context Service 接线由下方 M5.2 证据覆盖，librime 候选由 M5.3 覆盖，设置 UI 仍未完成。

M5.2 当前边界：

- Adapter 对 active workspace document 采集 Language Server symbol，每文档最多 256 条；
- workspace 路径只在本地生成 opaque project ID，不进入 pipe 或词库；
- `\\.\pipe\ContextIME.ProjectIndexer.v1` 使用固定 4096-byte request、每批最多 64 条；
- Context Service 使用独立 project worker 写入 `%APPDATA%\ContextIME\project-dictionaries`，不占用 decision loop 或 TSF key-event 热路径；
- 设计：[`docs/project-dictionary.md`](project-dictionary.md) 与 [`docs/project-dictionary-protocol.md`](project-dictionary-protocol.md)；
- CI 证据：[`docs/evidence/m5-language-server-symbol-ingestion-ci.md`](evidence/m5-language-server-symbol-ingestion-ci.md)，Project Dictionary run `33730311588`、Context Service run `33730311542`、General CI run `33730311569`；
- VS Code Adapter → installed service → `%APPDATA%` 已随 M5.3 在真实交互桌面完成端到端验证；
- 项目词候选、来源标记和 fail-open 由下方 M5.3 证据覆盖；后续 source 仍未完成，管理入口见 M5.4。

M5.3 当前边界：

- Project Indexer owner 在后台维护 active-project immutable snapshot；只选择 M5.2 的 `language_server` source，最多 256 条 / 24 KiB symbol payload；
- Adapter 使用显式 activate/deactivate 和 3 秒 lease，项目切换、禁用、加载失败或 lease 过期都会发布空快照，避免跨项目污染；
- `\\.\pipe\ContextIME.ProjectCandidate.v1` 只由 Weasel 后台 worker 访问；按键路径只 atomic-load 进程内 immutable snapshot，并且 composition 存在时不更新 librime property；
- Lua translator 只读 session property，最多生成 16 个大小写无关 prefix candidate；桥接任一层失败时不生成项目候选，普通 librime 拼音继续工作；
- CI 证据：[`docs/evidence/m5-project-candidate-bridge-ci.md`](evidence/m5-project-candidate-bridge-ci.md)，Native installer run `33745116216`、Project Dictionary run `33745116262`、Context Service run `33745116245`、IME Host run `33745116281`；
- Windows 11 Build 26200 真实 VS Code 验证：Language Server 上报 `PlayerController` / `spawnPlayer`，输入 `player` 后第一候选显示 `PlayerController 〔项目·类〕`，空格提交成功；
- Context Service 及三个 service Pipe 全部停止时，Notepad `nihao → 你好`、英文透传和中文恢复仍通过；
- 主机证据：[`docs/evidence/m5-project-candidate-bridge-host-verification.md`](evidence/m5-project-candidate-bridge-host-verification.md)；
- 状态：`M5.3_PROJECT_CANDIDATE_BRIDGE = VERIFIED`。其他 Language Server、干净机、LAN 和 RDP 不在本次已验证边界内；管理入口和大项目性能见下节。

大项目后台增量性能当前边界：

- Project Dictionary Store 只缓存最近访问项目的 immutable snapshot，每个最多 64 条的请求与已排序大词库做线性 merge；
- 成功 upsert 后直接把 exact persisted snapshot 交给 active-project publisher，不再重复完整读盘；
- 外部文件变化、disable 和 delete 均有缓存失效门禁，损坏文件仍拒绝覆盖并保留现场；
- 100,000 条固定 fixture 已通过 Ubuntu/g++、Windows/MSVC CI 和 Windows 11 主机 replay，四轮各 64 条增量在 Windows CI 为 2,592 ms、主机为 2,779 ms；
- 证据：[`docs/evidence/m5-project-dictionary-performance-ci.md`](evidence/m5-project-dictionary-performance-ci.md)，Project Dictionary run `33766211854`、Context Service run `33766211711`、IME Host run `33766211768`、General CI run `33766211840`；
- `0.5.1-preview` 已在 Windows 11 交互桌面通过已安装 Context Service 的真实 Project Indexer Pipe 对 100,000 条词库执行 20 轮各 64 条增量更新；累计 14,358 ms、单轮最大 647 ms；
- 同期 Notepad `nihao → 你好` 的 composition、候选、上屏、英文 `abc` 和中文恢复全部通过；Context Service、ContextIME Server 与官方 Weasel 均保持相同 PID/启动时间；
- 固定测试项目已精确删除，原有项目词库未被清理；主机证据：[`docs/evidence/m5-project-dictionary-performance-host-verification.md`](evidence/m5-project-dictionary-performance-host-verification.md)；
- 状态：`M5_PROJECT_DICTIONARY_LARGE_PROJECT = VERIFIED`。其他 Language Server、干净机、LAN 和 RDP 仍未验证或未完成。

M5.4 管理入口当前边界：

- 原生 Win32 管理器从安装后的开始菜单启动，只通过有界 `CIPM` 帧访问既有 Project Indexer 单 owner；
- 10 万词库的第一页 `1-100`、下一页 `101-200` 和返回上一页已在 Windows 11 交互桌面通过；
- 禁用和启用后的立即查看未再出现 `UNAVAILABLE`，active snapshot 为 `256 → 0 → 256`；
- 验收发现并修复过期 snapshot 可被禁用项目 heartbeat 重新续租的边界，修复后禁用 heartbeat 保持 0 个候选；
- 固定测试词库已通过二次确认删除，项目列表和 active snapshot 均清空，真实词库 SHA-256 不变；
- Context Service 三个 Pipe 全部停止时，Notepad `nihao → 你好`、英文 `abc` 和中文恢复仍通过；
- 安装程序：`contextime-0.5.2-preview-installer.exe`，`12,294,547` bytes，SHA-256 `c61356c4f19351041fb805d52a7463c8c4166378dc6653eca3b0391d4db2ec5c`；
- Build commit：`48740cfa6158d7334ef5d8c21e1f8613f59c704a`，公开 Preview run `33876802484`；
- 证据：[`docs/evidence/m5-project-dictionary-management-host-verification.md`](evidence/m5-project-dictionary-management-host-verification.md)；
- 状态：`M5.4_PROJECT_DICTIONARY_MANAGEMENT = VERIFIED`。干净机、LAN、RDP 和其他 Language Server 仍未验证。

M5.5 用户批准术语当前边界：

- `CIPM` 增加 `UPSERT_TERM / REMOVE_ENTRY`：管理窗口以 `term / manual` 固定来源写入选中既有项目，或按完整键删除单条词条；request 数据区承载 `symbol/type/source`，frequency 和 last_seen 由 server 拥有，重复添加为单调刷新；
- Store 增加 `RemoveEntry`：精确键删除、幂等、与 immutable 缓存一致；
- active-project snapshot 发布过滤器从仅 `language_server` 扩展为 `language_server + manual`，手动术语沿 M5.3 桥接进入候选并显示 `〔项目·术语〕`；
- 中文词语当前不会由拼音前缀命中（需后续 pinyin 标注）；`UPSERT_TERM` 不能创建不存在的项目词库；
- CI 证据：见 `docs/evidence/m5-approved-terms-ci.md`（私有仓库 Actions 因账单暂停，构建与测试由 ContextIME-public 镜像 CI 完成）；
- `0.5.4-preview` 已由用户完成覆盖安装并重启；登录桌面上已验证：真实 Adapter 采集进安装版服务（同一工作区路径得到同一匿名 ID）、管理器添加术语落盘 `term/manual`、管理器删除词条把被误加术语的真实词库恢复到字节级一致（SHA-256 前后相同）、整库删除回归通过；
- **安装版候选上屏验收已通过**：隔离 smoke 工作区由真实 Adapter 采集后，租约活跃期间经 CIPM `UPSERT_TERM` 注入 `CeShiShuYu`，app-smoke 在 TypeScript 注释行输入拼音，候选窗口第一项为 `CeShiShuYu 〔项目·术语〕`，空格上屏后编辑器以 `// CeShiShuYu` 结尾（91,899 像素候选窗变化 + 截图证据）；smoke 的词库等待谓词同步演进为同时接受 `language_server` 与 `manual` 来源；测试工作区与词库已全部经 CIPM `REMOVE_PROJECT` 清理；
- 会话中诊断出早前"无候选"观察的根因链：ContextIME TSF Server 被 smoke 超时逻辑反复误杀（`/nascii` 子进程在实例发现失败时会变成新 Server，被超时逻辑击杀）、UNICODE 直插字符天然绕过 IME 管线、M3 注释→中文自动切换仍是未验证欠账（本次通过 M5.3 时代的手动强制中文路径完成验收）；
- 证据：`docs/evidence/m5-approved-terms-host-verification.md`；状态：`M5.5_INSTALLED_CANDIDATE_ACCEPTANCE = VERIFIED`；
- 安装器强制重启问题（升级路径无条件 `SetRebootFlag true`）记录为 0.5.5 任务：改为显式提示并评估免重启升级。

## M6：个人习惯

- [ ] 候选词频学习。
- [ ] 应用、输入区域和项目偏好。
- [ ] 注释、字符串和提交信息偏好。
- [ ] 学习记录查看和单条删除。
- [ ] 清空、暂停和隐私模式。
- [ ] 配置备份与恢复。
- [ ] 学习数据不包含完整代码、密码和 Token。

## M7：Release Gate

- [ ] 签名安装程序。
- [ ] 自动升级、回滚和配置迁移。
- [ ] 崩溃日志与诊断包。
- [ ] 隐私说明和用户文档。
- [ ] 测试版与稳定版通道。
- [ ] 干净 Windows 安装、升级、卸载测试。
- [ ] 连续使用 8 小时无焦点丢失。
- [ ] RDP 基础兼容测试。
- [ ] 不影响微软拼音、微信输入法和其他输入法。

## 原型资产

已完成但不等于正式产品：

- [x] 上下文策略与固定优先级。
- [x] 个人纠正学习和输入区域记忆实验。
- [x] Windows 输入状态读取和切换实验。
- [x] Tree-sitter 多文档语法分类。
- [x] 旧 VS Code controller、状态栏和 Windows 切换实验。
- [x] Windows x64 VSIX 打包流程；当前已收缩为 report-only Adapter 包。

原型保留价值：

- 策略、防抖和手动覆盖经验；
- 代码、注释和字符串识别；
- VS Code 上下文采集；
- Windows 状态观察；
- CI、诊断和打包流程。

原型不能用于证明：

- 原生输入法已注册；
- 拼音组合与候选已完成；
- ContextIME 真机安装和卸载已通过；
- ContextIME 已成为可日常使用的输入法。

## 验收原则

- 真机 Windows 证据优先于单元测试。
- CI 编译通过不能替代输入法安装和输入测试。
- 按键热路径不得依赖网络、AI、项目扫描或 Node.js。
- Context Service 故障时必须保留普通输入能力。
- 候选组合期间禁止外部状态切换。
- 不上传输入内容和源码。
