# ContextIME Project Dictionary Protocol v1

## 职责

Project Dictionary Protocol 只负责把 Adapter 已批准的项目 symbol 批量交给 Context Service 后台 Project Indexer。它不承担 context decision、输入状态切换、候选生成或源码索引。

```text
VS Code Adapter
  → \\.\pipe\ContextIME.ProjectIndexer.v1
  → Context Service dedicated project worker
  → ProjectDictionaryStore
```

Context decision 使用另一个端点 `\\.\pipe\ContextIME.ContextService.v1`。两个 pipe 的协议、线程和故障边界独立，项目写盘不能占用输入决策循环。

## Transport 与安全

- Windows local Named Pipe：`\\.\pipe\ContextIME.ProjectIndexer.v1`；
- byte mode、overlapped I/O；
- `PIPE_REJECT_REMOTE_CLIENTS` 禁止网络客户端；
- DACL 只允许当前 Windows 用户和 `SYSTEM`；
- 每个连接只处理一个 request/response/ACK exchange；
- connect、write、read 和 ACK 共用调用方的总 deadline；
- response 完整校验后，client 发送单字节 ACK `0x06`。

默认 Adapter 对一次文档 publish 使用 1000 ms 总 deadline。它会在同一 deadline 内短暂重试 service 串行重建 Pipe 实例造成的 unavailable；每个 batch 不会重新获得完整预算。

## 通用 header

所有整数显式使用 little-endian。不能直接序列化 TypeScript/C++ object 或 struct。

| Offset | Size | 字段 |
|---:|---:|---|
| 0 | 4 | magic `CIPD` |
| 4 | 2 | protocol version，当前 `1` |
| 6 | 2 | message type：upsert request `1` / response `2` |
| 8 | 4 | payload size |
| 12 | 4 | request ID |

request ID 必须由 response 原样回显。未知 magic、版本、message type、错误长度和非零 reserved bytes 一律拒绝。

## ProjectRequest

request 固定为 4096 bytes，payload size 固定为 4080。固定区：

| Offset | Size | 字段 |
|---:|---:|---|
| 16 | 1 | operation：`UPSERT = 1` / `ACTIVATE_PROJECT = 2` / `DEACTIVATE_PROJECT = 3` |
| 17 | 1 | record count；upsert 为 `1..64`，其余为 `0` |
| 18 | 2 | reserved，必须全零 |
| 20 | 16 | 128-bit opaque project ID；deactivate 时必须全零 |
| 36 | 12 | reserved，必须全零 |
| 48 | variable | packed records |
| record end | remaining | reserved，必须全零 |

`project_id` 在 wire 上是 16 个 raw bytes，落盘前转为 32 字符 lowercase hex。Adapter 通过下列域分隔输入生成它：

```text
SHA-256(
  "ContextIME.Project.v1\0"
  + platform-domain
  + normalized-local-workspace-path
)[0..15]
```

Windows workspace path 在本地执行 Win32 normalize、slash 统一、case fold 和多余尾部分隔符删除。路径本身不会进入 request 或 dictionary。

每个 record 使用 8-byte header 加 UTF-8 symbol：

| Relative offset | Size | 字段 |
|---:|---:|---|
| 0 | 1 | symbol type |
| 1 | 1 | source |
| 2 | 2 | symbol UTF-8 byte length |
| 4 | 4 | frequency，范围 `1..UINT32_MAX` |
| 8 | variable | symbol UTF-8 bytes，最多 128 bytes |

symbol type：

```text
0 class       1 method      2 property    3 enum
4 namespace   5 file        6 directory   7 asset
8 shader      9 term
```

source：

```text
0 language_server
1 compilation_database
2 project_file
3 file_system
4 manual
```

M5.2 Adapter 只发送 `language_server`，其他值为后续有界 source 保留。decoder 还复用 Store 对 UTF-8、控制字符、路径分隔符、盘符分隔符和空 symbol 的严格校验。

client 按 `64 records` 与 4096-byte frame 两个上限拆批。一个文档最多采集 256 个 symbol，因此最坏不超过四个 count-limited batch；长 UTF-8 name 可能进一步拆批，但仍受同一 publish deadline 约束。

`ACTIVATE_PROJECT` 只刷新当前项目 3 秒 lease；相同项目不会重新读盘。首次激活或切换项目时，Project Indexer owner 从 Store 加载并发布新的 immutable snapshot。`DEACTIVATE_PROJECT` 立即发布空 snapshot。Adapter 只在 VS Code 聚焦时刷新 lease，进程异常退出也会通过 lease 超时自动失效。

## UpsertResponse

response 固定为 32 bytes，payload size 固定为 16：

| Offset | Size | 字段 |
|---:|---:|---|
| 16 | 1 | status |
| 17 | 1 | accepted，必须为 `0/1` |
| 18 | 2 | accepted count |
| 20 | 12 | reserved，必须全零 |

status：

```text
0 OK
1 MALFORMED_REQUEST
2 STORE_ERROR
3 INTERNAL_ERROR
```

只有 `OK` 允许 `accepted = 1`，且 `accepted_count` 必须与当前 request 的 record count 完全相等；activate/deactivate 成功时该值为 `0`。所有失败必须返回 `accepted = 0` 和 `accepted_count = 0`。Adapter 对不一致的 response、错误 request ID、超长 response 或非零 reserved bytes 报 `protocolError`。

## Server 持久化语义

server 解码成功后使用本机 Unix epoch 毫秒填充本批记录的 `last_seen`，再调用单 owner `ProjectDictionaryStore::Upsert`。Adapter 不提供 wall-clock timestamp，避免外部 caller 回退或伪造持久化时间。

Store 继续使用 `%APPDATA%\ContextIME\project-dictionaries\<project_id>.dict`，以 `symbol + symbol_type + source` 为记录键，并保证 frequency/last_seen 单调更新。

## Project Candidate Snapshot Protocol v1

Context Service 使用独立的 `\\.\pipe\ContextIME.ProjectCandidate.v1` 向 Weasel Server 的后台 worker 提供 active-project immutable snapshot。它同样是 local-only、当前用户/SYSTEM DACL、byte mode、overlapped I/O 和 `PIPE_REJECT_REMOTE_CLIENTS`；不复用 Native Server Pipe、Context decision Pipe 或 Project Indexer Pipe。

query 固定 32 bytes，response 固定 32768 bytes，magic 为 `CIPC`，协议版本为 `1`。response 固定区包含：

```text
generation
active
record_count
remaining_ttl_ms
128-bit opaque project ID
```

每条 candidate 只包含 `symbol_type / frequency / UTF-8 symbol`。最多 256 条、symbol 总计最多 24576 bytes；排序已由 Project Indexer owner 在发布前完成。transport 不携带 workspace path、源码、URI、range、detail、container 或输入正文。response 完整校验后 client 回传 ACK `0x06`。

Weasel worker 每 500 ms 在后台获取一次 snapshot，并提前编码为 librime session property。`ProcessKeyEvent` 只 atomic-load 进程内 immutable snapshot，在 revision 变化且当前没有 composition 时调用一次 `set_property`，随后继续原有 `process_key`。Lua translator 只解析该内存 property，不执行文件或 IPC 操作。

## Project Management Protocol v1

M5.4 管理程序在同一 Project Indexer Pipe 上使用独立 `CIPM` magic。固定
4096-byte request 支持 `LIST_PROJECTS / VIEW_PROJECT / SET_ENABLED /
REMOVE_PROJECT`，固定 32768-byte response 支持最多 512 个 project ID 或
128 个完整词条的分页结果。既有 `CIPD` request/response 布局保持不变。

Server 按 magic 分流后仍在 Project Indexer 单 owner 线程执行 Store 操作。
禁用 active project 会发布无候选的 immutable snapshot；重新启用会发布恢复后
snapshot；删除 active project 会立即清空 snapshot。详细布局、UI 和故障语义见
[`project-dictionary-management.md`](project-dictionary-management.md)。

## 故障边界

以下状态只终止或拒绝当前后台 publish，不影响普通输入：

- Project Indexer pipe 不存在或 busy；
- client/server timeout 或中途 disconnect；
- request/response framing 错误；
- Store I/O、损坏数据或 allocation failure；
- Language Server 不可用或返回空结果。
- Candidate Pipe 不存在、busy、超时、断开或 frame 损坏；
- Weasel candidate worker 启动或刷新失败；
- librime property 为空或 Lua 解析失败。

Project worker 使用 Context Service 内的独立线程。线程初始化或 store 失败不会进入 TSF key event；Context Service decision pipe 仍可运行。`--quit` 同时唤醒两个 pipe，等待 project worker 退出后才释放 singleton。

## 隐私边界

协议能承载的业务数据只有：

```text
opaque project ID
symbol
symbol type
source
frequency
```

它没有字段承载源码、document URI、workspace path、range、detail、container、字符串值、密码、Token、API Key 或环境变量值。协议校验不能识别任意 symbol name 是否含秘密，因此 Adapter 必须继续只读取 Language Server 的结构化 `name/kind/children`。

## 验证状态

TypeScript/C++ canonical fixtures、双平台 Store/snapshot/codec、Windows local Project Indexer/Candidate Pipe、Context Service lifecycle、Weasel 后台 refresh worker 和固定上游 x64/Win32 构建均已由 CI 验证。固定 run 与分层状态见 [`evidence/m5-project-candidate-bridge-ci.md`](evidence/m5-project-candidate-bridge-ci.md)。

`0.5.0-preview` 已在 Windows 11 Build 26200 登录且解锁的交互桌面完成真实 VS Code Adapter → installed Context Service → `%APPDATA%` dictionary → Candidate Pipe → Weasel/librime 候选端到端验证。输入 `player` 时第一候选显示 `PlayerController 〔项目·类〕` 并可由空格提交；完全停止 Context Service 和三个 service Pipe 后，普通拼音 `nihao → 你好` 仍通过。主机证据见 [`evidence/m5-project-candidate-bridge-host-verification.md`](evidence/m5-project-candidate-bridge-host-verification.md)。

大项目性能、其他 Language Server、干净机、LAN 和 RDP 仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`，不属于本次 M5.3 已验证范围。
