# ContextIME

> 本仓库是从内部开发历史导出的公开源码快照。公开快照不包含本机用户名、
> 机器名、测试截图或本地构建产物；早期证据文档中的私有 CI 链接仅保留为
> 历史运行编号，新的公开构建从本仓库重新产生。

ContextIME 是一款面向程序员和中英文混合输入用户的 **Windows 开发者输入法**。

产品目标：

- 安装后出现在 Windows 输入法列表；
- 独立完成拼音组合、候选显示、选词和上屏；
- 不依赖编辑器插件也能在普通 Windows 软件中输入；
- 支持代码、注释、终端、文档、AI 对话和项目术语场景；
- 允许每个开发者配置自己的输入习惯；
- 本地学习，不上传源码和完整输入正文。

ContextIME 不是 VS Code 输入法切换插件，也不是只控制微软拼音或其他输入法的工具。

## 当前状态

已完成 **ContextIME 原生输入法、开发者混输和 Context Engine 主机基线**：

- 独立 TSF/Profile GUID、注册表、安装目录和用户数据目录；
- 独立 IPC Window、Named Pipe、服务名、部署互斥锁和 Server 单实例互斥锁；
- 主开发机已验证安装、TSF 注册、拼音组合、候选、数字选词、翻页、上屏和中英文切换；
- 已验证与官方小狼毫 0.17.4 并存，以及 ContextIME 覆盖升级、卸载和重装隔离；
- Notepad、Edge、VS Code、Visual Studio 和 Terminal smoke 全部通过；
- 30 分钟 Notepad 稳定性测试 `147 / 147` 通过，Server PID 不变、私有内存增量 `69,632` bytes、句柄增量 `0`。
- M2 技术标识符、路径、URL、命令与整套混输主机门禁通过；
- M3 Context Service、单一 Context Engine owner、TSF bridge、manual override、composition/candidate 保护和服务故障回退已完成主机收口。

M4.1 已在同一 Context Service Named Pipe 上加入版本化 editor context 协议、2 秒 TTL store 和真实前台 VS Code 校验。当前 M4.2 源码已将旧 VS Code controller 改为 report-only Adapter：只上报 focused/surface/syntax/language，不再读取或切换系统 IME，也不拥有策略和学习状态。

已安装的 `0.3.1-preview` 不含 M4.1/M4.2。VS Code → 已安装 Context Service → Context Engine → TSF 的完整交互桌面链路仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`，不能由 VSIX 构建或 CI 替代。

最新状态与证据见 [`docs/roadmap.md`](docs/roadmap.md)、[`docs/editor-context-protocol.md`](docs/editor-context-protocol.md)、[`docs/evidence/m4-editor-context-protocol-ci.md`](docs/evidence/m4-editor-context-protocol-ci.md) 和 [`docs/evidence/m4-vscode-report-only-adapter-ci.md`](docs/evidence/m4-vscode-report-only-adapter-ci.md)。

## 当前主线

执行顺序固定为：

```text
M1_HOST_BASELINE = VERIFIED
→ M2_MIXED_INPUT = VERIFIED
→ M3_CONTEXT_ENGINE = HOST_VERIFIED
→ M4.1 editor context receiver = MERGED
→ M4.2 report-only VS Code Adapter
→ M4 installed end-to-end verification
```

M4 只增加可选的高精度编辑器上下文来源。没有 Adapter 时，ContextIME 仍是完整可用的原生输入法；Adapter 失效时，Context Engine 保守回退 `KEEP`。

## 上游基线

固定版本在 [`native/upstream.lock.json`](native/upstream.lock.json)。

构建说明和许可证边界：

- [`docs/m0-upstream-baseline.md`](docs/m0-upstream-baseline.md)
- [`docs/evidence/m0-upstream-build.md`](docs/evidence/m0-upstream-build.md)

## 产品与技术文档

- [`docs/product-intent.md`](docs/product-intent.md)：产品身份与路线判断；
- [`docs/product-plan.md`](docs/product-plan.md)：功能范围、里程碑和验收；
- [`docs/technical-route.md`](docs/technical-route.md)：底座、架构、模块边界和实施顺序；
- [`docs/roadmap.md`](docs/roadmap.md)：当前执行进度；
- [`AGENTS.md`](AGENTS.md)：Codex/Agent 必须遵守的项目约束。

## 目标架构

```text
ContextIME Windows IME
├─ Windows TSF 前端
├─ librime 拼音与候选引擎
├─ Developer Context Engine
├─ Context Service
├─ 项目词库与个人学习
├─ 设置、安装和升级
└─ 可选编辑器适配器
   ├─ VS Code
   ├─ Visual Studio
   └─ JetBrains
```

## 原型资产

当前 TypeScript 资产保留在：

```text
packages/
  policy-core/
  windows-runtime/
  syntax-runtime/
  vscode-adapter/
```

`policy-core` 和 `windows-runtime` 继续作为研究原型；`syntax-runtime` 提供本地语法分析；`vscode-adapter` 已改为可选的最小上下文上报器。它们都不是原生输入法产品入口。

原型验证：

```bash
npm install --ignore-scripts
npm run check
```

VSIX 仅安装可选 Adapter，不包含 TSF、librime、Context Service 或输入法注册能力，也不会自行切换系统输入法。安装 VSIX 不代表原生输入法或 M4 真机链路完成。

## 首个正式交付物

> ContextIME Windows 安装程序：安装后出现在输入法列表，能够独立完成拼音组合、候选显示、选词和文本上屏，并可与原版 Weasel 并存。
