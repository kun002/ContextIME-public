# ContextIME 编辑器上下文协议（M4.1 / M4.2）

## 范围

M4.1 建立 Context Service 接收编辑器上下文的原生协议与短期存储；M4.2 将 VS Code 扩展改为 report-only Adapter。两者都不直接切换输入状态，也不修改 TSF、librime、composition、candidate 或 commit 链路。

```text
report-only VS Code Adapter
                ↓ normalized context only
\\.\pipe\ContextIME.ContextService.v1
                ↓
service-owned EditorContextStore (TTL 2 seconds)
                ↓ foreground Code.exe validation
Context Engine remains the single decision owner
```

`packages/vscode-adapter` 现在只负责归一化、调度和上报。旧 controller 的直接 mode switching、Windows mode polling、policy ownership、manual lock 和 habit learning 已从 Adapter 删除；Context Engine 继续是唯一决策 owner。

## 同一 Named Pipe 上的 v1 消息

M4.1 扩展现有版本 `1`，不创建第二个 pipe、线程或服务。request 仍固定为 48 bytes，response 仍固定为 32 bytes，因此服务端继续复用既有 DACL、`PIPE_REJECT_REMOTE_CLIENTS`、总 deadline、response 和 ACK 机制。

| Message type | 名称 | 固定长度 |
|---:|---|---:|
| 1 | `EvaluateRequest` | 48 bytes |
| 2 | `EvaluateResponse` | 32 bytes |
| 3 | `EditorContextRequest` | 48 bytes |
| 4 | `EditorContextResponse` | 32 bytes |

### EditorContextRequest payload

| Payload offset | Size | 字段 |
|---:|---:|---|
| 0 | 1 | flags；只有 bit 0 `window_focused` 有效 |
| 1 | 1 | surface：`UNKNOWN / EDITOR / INTEGRATED_TERMINAL` |
| 2 | 1 | syntax：`UNKNOWN / CODE / COMMENT / STRING / MARKDOWN_TEXT / MARKDOWN_CODE` |
| 3 | 1 | language ID byte length，范围 `0..20` |
| 4 | 20 | lowercase normalized ASCII language ID，未使用部分必须为零 |
| 24 | 8 | reserved，必须为零 |

language ID 只允许 `a-z`、`0-9`、`-`、`_`、`.`、`+`。它用于后续精确规则扩展；M4.1 不根据 language ID 猜测状态。

## M4.2 Adapter 上报语义

Adapter 在 VS Code extension host 内本地完成语法判断，并在这些事件后合并上报：

- active editor、selection 和 active document 变化；
- active terminal 与 terminal interaction；
- VS Code window focus；
- Adapter 配置变化；
- 1 秒 heartbeat，用于在 service 的 2 秒 TTL 内刷新仍有效的上下文。

高频事件默认使用 35 ms trailing debounce。若一个 report 尚未完成，新事件只保留最新 trigger，并在当前 report 结束后再发送。每次连接只发送一个固定 request、接收一个固定 response、发送 `0x06` ACK 后关闭；整个 connect/write/read/ACK 共用一个默认 250 ms deadline。

Adapter 只在有可靠事件证据时报告 `EDITOR` 或 `INTEGRATED_TERMINAL`。VS Code 未公开的 workbench focus（例如未知侧栏或面板）保持 `UNKNOWN`，不根据 active editor 或 active terminal 猜测。窗口失焦时报告 unfocused update。

禁用 Adapter 时发送一次 unfocused/unknown invalidation，之后停止 heartbeat。服务不存在、拒绝、超时、协议错误或中途断开只写 Adapter output diagnostics，不阻塞按键路径，也不触发本地替代策略。

### EditorContextResponse payload

| Payload offset | Size | 字段 |
|---:|---:|---|
| 0 | 1 | `OK / UNSUPPORTED_VERSION / MALFORMED_REQUEST / INTERNAL_ERROR` |
| 1 | 1 | `accepted`，只能为 `0` 或 `1` |
| 2 | 14 | reserved，必须为零 |

错误 editor frame 仍返回 type `4` 并回显 header 中的 request ID；解码错误最终让 server connection 报 `PROTOCOL_ERROR`。合法更新在 service 没有 store 时返回 `INTERNAL_ERROR / accepted=false`，transport 本身仍完成一次有界响应。

## Store 与决策边界

Context Service 在其单一 request loop 中拥有一个 `EditorContextStore`，所以 M4.1 不增加共享锁或输入热路径工作。每次合法 update 使用服务自己的 `GetTickCount64()` 记录 2 秒 TTL，不信任调用方时间。

update 只有同时满足以下条件才可贡献规则：

- adapter 报告其窗口仍 focused；
- update 未达到 TTL 边界；
- Context Service 捕获到的真实前台 executable 是 `code.exe` 或 `code - insiders.exe`；
- surface/syntax 是明确支持的归一化值。

映射保持保守：

| 上下文 | 建议 |
|---|---|
| editor code / Markdown code | syntax `ENGLISH` |
| editor comment / Markdown text | syntax `CHINESE` |
| editor string / unknown | 不提供建议 |
| integrated terminal | surface `ENGLISH` |
| stale / unfocused / 非 VS Code foreground | 不提供建议 |

调用方已经提供的 syntax 或 surface rule 不会被覆盖。最终仍由 Context Engine 按 composition protection、用户锁定、manual override、user/project rule、syntax、surface、application、`KEEP` 的固定优先级决策；adapter 和 store 都不直接改变 IME state。

## 隐私边界

wire struct 没有字段承载：

- source text、selection text 或完整用户输入；
- document URI、文件路径或项目路径；
- project/document identity；
- password、Token、API Key 或环境变量值。

M4.1 receiver 没有网络能力，也不读取编辑器文档。M4.2 只在 VS Code extension host 内读取当前文档供 Tree-sitter 分析，使用 `document-N` 不透明缓存 ID，不使用 URI；源码、selection、URI 和项目路径不会进入 wire frame、日志或持久化数据。真实 foreground 校验只复用既有的 lowercase executable basename。

## 门禁与当前验证状态

门禁覆盖：

- editor request/response round-trip、固定 wire size 和严格字段校验；
- TTL 边界、focus、真实 VS Code foreground、其他 editor 隔离；
- code/comment/Markdown/string/terminal 映射与 caller precedence；
- Named Pipe 成功 update、malformed update 拒绝和缺失 store 错误响应；
- Context Service executable 和 IME host worker 的链接输入完整性。

| 项目 | 状态 |
|---|---|
| protocol codec / store | `implemented` / `built` / `statically_verified`；双平台 75 + 21 assertions |
| Windows Named Pipe editor branch | `implemented` / `built` / runner `runtime_verified`；67 service assertions |
| installed Context Service | 当前 `0.3.1-preview` 不含 M4.1 |
| report-only VS Code Adapter | `implemented` / `built` / `statically_verified`；Windows/Ubuntu 43 workspace tests |
| VSIX Adapter package | Windows CI `built` / package-audited；不含 Koffi、Windows mode switching 或 policy owner |
| VS Code → service → Context Engine | `REAL_WINDOWS_VERIFICATION_REQUIRED` |
| TSF 自动状态应用 | M3 真机基线不因本改动重新计为 M4 验收 |

M4.1 固定 CI 证据见 [`evidence/m4-editor-context-protocol-ci.md`](evidence/m4-editor-context-protocol-ci.md)，M4.2 设计与验证见 [`vscode-adapter.md`](vscode-adapter.md) 和 [`evidence/m4-vscode-report-only-adapter-ci.md`](evidence/m4-vscode-report-only-adapter-ci.md)。只有安装含 M4 的新版、在已登录且解锁的真实 Windows 桌面验证 code/comment/terminal 和故障回退后，才能标记 `real_machine_verified`。
