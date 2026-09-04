# ContextIME VS Code Report-Only Adapter（M4.2）

## 定位

VS Code Adapter 是 ContextIME 的可选高精度上下文来源，不是输入法主体，也不是第二个策略引擎。

```text
VS Code events
  → local syntax classification
  → normalized editor context
  → bounded local Named Pipe report
  → Context Service TTL/foreground validation
  → Context Engine single decision
  → existing IME state bridge
```

Adapter 不注册输入法、不生成拼音候选、不读取系统 IME mode，也不调用任何 Windows mode switching API。没有安装 Adapter、Adapter 被禁用或 Context Service 不可用时，ContextIME 的普通输入链路保持独立工作。

## 上报字段

Editor Context v1 request 只包含：

- VS Code window 是否 focused；
- `UNKNOWN / EDITOR / INTEGRATED_TERMINAL` surface；
- `UNKNOWN / CODE / COMMENT / STRING / MARKDOWN_TEXT / MARKDOWN_CODE` syntax；
- 最长 20 bytes 的 normalized lowercase ASCII language ID。

Editor Context 协议不包含 document/project identity。详细 wire layout 见 [`editor-context-protocol.md`](editor-context-protocol.md)。M5.2 项目 symbol 使用职责和端点完全分离的 Project Dictionary Protocol，不扩大每秒 heartbeat 上下文消息。

## M5.2 项目 symbol 上报

Adapter 可在后台调用 VS Code document symbol provider，把当前 workspace active document 的结构化 symbol name 交给 Context Service Project Indexer：

```text
vscode.executeDocumentSymbolProvider
  → name / kind / children only
  → 128-bit opaque workspace ID
  → bounded local batches
  → \\.\pipe\ContextIME.ProjectIndexer.v1
```

当前支持 C、C++、C#、JavaScript/TypeScript（含 React）和 Python language ID。只映射 class、method、property、enum、namespace 和 file；每文档最多 256 个有效 symbol，每 request 最多 64 条且 frame 固定为 4096 bytes。

采集发生在 Adapter activate、active editor、save、workspace/config 变化和手动 `ContextIME: Refresh Project Symbols`，默认 500 ms debounce；相同 document version 不重复 publish。它不由键盘输入事件直接触发，也不扫描 workspace 或读取源码。

workspace path 只在 extension host 内用于域分隔哈希，不进入 pipe、日志或磁盘词库。Adapter 不保留/上报 URI、路径、range、detail、container、字符串值、Token 和环境变量值。完整边界见 [`project-dictionary.md`](project-dictionary.md) 和 [`project-dictionary-protocol.md`](project-dictionary-protocol.md)。

## 上下文采集

明确的 editor selection/activation 事件将 surface 标记为 `EDITOR`；明确的 active terminal interaction 将其标记为 `INTEGRATED_TERMINAL`。窗口重新获得焦点时，由于 VS Code API 不能可靠说明当前 workbench control，surface 先回到 `UNKNOWN`，等待新的可靠交互事件。

支持的 TypeScript/JavaScript/C# 语法在 extension host 内通过固定 Tree-sitter WASM 分析。Markdown 只区分正文与 fenced code。未支持语言或本地 parser 故障回退 `UNKNOWN` 或有限的保守行级判断，不向 service 发送源码。

Tree cache 使用 WeakMap 分配 `document-N` 不透明 ID。Adapter 不使用或上报 `document.uri`、workspace path、project ID、selection text、Token、环境变量值或完整输入历史。

## 调度与故障边界

- 默认 35 ms trailing debounce，合并高频 editor 事件；
- report 进行中只保留最新 pending trigger；
- 默认 1 秒 heartbeat，低于 service 的 2 秒 TTL；
- 每个 request 使用独立连接和默认 250 ms 总 deadline；
- service 串行请求之间若短暂没有可连接的 Pipe 实例，只在同一个总 deadline 内重试；
- response framing、request ID、status、accepted 和 reserved bytes 全部严格校验；
- 成功接收固定 response 后发送 `0x06` ACK；
- timeout、disconnect、service unavailable、application rejection、protocol error 和 I/O error 只进入诊断，不产生本地 mode 决策。

## 配置与诊断

Editor Context reporter 暴露四项有界配置：

- `context-ime.adapter.enabled`；
- `context-ime.adapter.debounceMs`；
- `context-ime.adapter.heartbeatMs`；
- `context-ime.adapter.timeoutMs`。

Project Dictionary reporter 另有四项独立配置：

- `context-ime.projectDictionary.enabled`；
- `context-ime.projectDictionary.debounceMs`；
- `context-ime.projectDictionary.timeoutMs`；
- `context-ime.projectDictionary.maximumSymbolsPerDocument`。

命令 `ContextIME: Report Editor Context Now` 触发一次立即 context report；`ContextIME: Refresh Project Symbols` 强制刷新当前文档 symbol；`ContextIME: Show Adapter Status` 显示最后上下文、project publish、transport 结果、surface hint、syntax cache 和隐私边界。这些命令都不会切换系统输入法。

## 验证边界

TypeScript 类型检查、协议/transport/scheduler 单测和 VSIX bundle 审计只能证明 `implemented / built / statically_verified`。完整 code/comment/Markdown/terminal 决策以及 Language Server → installed Context Service → `%APPDATA%` 词库写入必须在已登录且解锁的真实 Windows 交互桌面验证；在此之前保持 `REAL_WINDOWS_VERIFICATION_REQUIRED`。项目 symbol 进入 librime 候选尚未实现。
