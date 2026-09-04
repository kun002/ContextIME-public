# ContextIME Context Service（M3.2）

## 当前范围

M3.2 建立独立的 Windows 本地 Context Service、版本化 Named Pipe 协议和故障 `KEEP` 客户端边界。它复用 M3.1 的 C++ Context Engine。后续 M3 bridge 已接入固定 Weasel TSF，`0.3.0-preview` 构建门禁已把服务加入安装器生命周期；主开发机已完成 `0.2.3 → 0.3.0` 覆盖升级、服务生命周期和服务停止时普通输入回退验证。

M3.4 增加保守的前台应用 Context Source。服务只保留 executable basename 和 window class，不读取标题；只有可靠独立终端 surface 填充 English，设计见 [`application-context.md`](application-context.md)。

M4.1 在同一 pipe 上增加 report-only editor context request、2 秒 TTL store 和真实 VS Code foreground 门禁。它没有改造旧 VS Code controller prototype，也不直接切换输入状态；设计与 wire layout 见 [`editor-context-protocol.md`](editor-context-protocol.md)。

M5.2 在同一个 companion process 中增加独立 Project Indexer pipe 和 dedicated worker thread。它接收 Adapter 已批准的有界 symbol batch，写入 ContextIME 自有项目词库目录；项目协议、线程和磁盘 I/O 不占用下方 Context decision loop。设计与 wire layout 见 [`project-dictionary-protocol.md`](project-dictionary-protocol.md)。

```text
background context refresh worker
              ↓
\\.\pipe\ContextIME.ContextService.v1
              ↓
Context Service → Context Engine → short decision
              ↓
cached decision (future IME state applier)
```

项目写入路径独立为：

```text
\\.\pipe\ContextIME.ProjectIndexer.v1
              ↓ dedicated project worker
%APPDATA%\ContextIME\project-dictionaries
```

Context Service 保持独立的 `contextime-context-service.exe`，不并入 Weasel Server，也不注册为 Windows SCM 服务。`0.3.0-preview` NSIS 已打包该 executable，通过 `ContextIMEContextService` autorun 启动，并在覆盖升级/卸载前调用有界 `--quit`。固定 Weasel TSF consumer 已构建；安装器已在主开发机真实桌面完成覆盖升级和安装后启动验证。

## 标识隔离

Context Service 使用：

```text
\\.\pipe\ContextIME.ContextService.v1
```

现有原生输入链路继续使用：

```text
\\.\pipe\ContextIMENamedPipe
```

两者职责和名称完全分离。Context Service 不复用 Weasel/ContextIME 输入引擎 IPC、Window Class、mutex 或 service name，也不覆盖现有端点。

Project Indexer 另外使用：

```text
\\.\pipe\ContextIME.ProjectIndexer.v1
```

它只接收 project dictionary upsert，不接受 Context Engine decision request，也不替代输入引擎 pipe。三条 pipe 均使用 ContextIME 独立标识。

Context Service 自有生命周期标识：

```text
mutex:      Local\ContextIME.ContextService.Singleton.v1
stop event: Local\ContextIME.ContextService.Stop.v1
autorun:    ContextIMEContextService
```

它是普通 companion process，不使用 Windows SCM。第二实例立即退出；`--quit` 设置 stop event、唤醒等待连接的 pipe，并等待 singleton mutex 被释放。该控制路径不在输入热路径。

服务端创建管道时：

- 设置 `PIPE_REJECT_REMOTE_CLIENTS`，拒绝网络客户端；
- DACL 只授权当前 Windows 用户和 `SYSTEM`；
- 使用 byte-mode、overlapped I/O；
- 每个连接只处理一个固定上限请求后断开；
- 客户端读写具有同一个总 deadline，不允许每一步重新获得完整超时预算。

Project Indexer pipe 使用相同的 local-only DACL、`PIPE_REJECT_REMOTE_CLIENTS`、byte-mode 和 bounded I/O 原则，但由独立线程串行处理固定 4096-byte request。默认目录来自当前用户 `%APPDATA%\ContextIME\project-dictionaries`；目录不可解析或 worker/store 初始化失败时，项目索引停用，Context decision 和普通 IME 输入继续运行。`--quit` 会同时唤醒 context pipe 与 project pipe，并 join project worker 后退出。

## 协议 v1

协议是固定长度、显式 little-endian 的二进制帧。不能直接序列化 C++ struct，因此不依赖编译器 padding、ABI 或 JSON 库。

通用 16-byte header：

| Offset | Size | 字段 |
|---:|---:|---|
| 0 | 4 | magic `CIME` |
| 4 | 2 | protocol version，当前为 `1` |
| 6 | 2 | message type：evaluate `1/2`，editor context `3/4` |
| 8 | 4 | payload size |
| 12 | 4 | request id |

`EvaluateRequest` 总长 48 bytes，payload 只包含归一化决策输入：

- 当前中文/英文状态；
- composition/candidate、automation/context available flags；
- explicit lock；
- user/project/syntax/surface/application 可选规则；
- 调用方提供的单调时间和 manual override deadline；
- 8 bytes 必须为零的 reserved 区域。

`EvaluateResponse` 总长 32 bytes，payload 包含：

- `OK / UNSUPPORTED_VERSION / MALFORMED_REQUEST / INTERNAL_ERROR`；
- `KEEP / CHINESE / ENGLISH`；
- target mode、decision source 和 `should_switch`；
- 11 bytes 必须为零的 reserved 区域。

客户端完整读到 response 后发送固定 1-byte ACK `0x06`。服务端在同一个有限 client-I/O deadline 内等待 ACK 后才断开，避免 `DisconnectNamedPipe` 抢在客户端消费已写 response 之前；不使用可能因恶意/故障客户端而无限等待的 `FlushFileBuffers`。

解码器拒绝错误 magic、未知版本、错误 message type/长度、非法 enum/flag 和非零 reserved bytes。response 必须回显 request id；客户端还会复核 decision 与原请求当前状态是否语义一致。

协议没有字段可承载完整输入正文、源码、路径内容、密码、Token、API Key 或环境变量值。

`EditorContextRequest/Response` 保持相同的 48/32-byte frame 上限，只承载 focused、surface、syntax 和最长 20-byte normalized language ID。document URI、project path 和 source text 不进入协议。服务仅在 update 新鲜、adapter 报 focused 且真实 foreground executable 为 VS Code 时把建议交给 Context Engine；stale/unknown/unfocused 时不提供规则。

## 故障回退

客户端返回 transport status 和 Context Engine decision。以下任一情况都不得产生中文/英文切换：

- 服务或 pipe 不存在；
- pipe busy 超时；
- 服务接受连接后不响应；
- 服务中途断开；
- response 损坏、版本不兼容、request id 不匹配；
- response decision 与请求状态矛盾。

统一回退为：

```text
desired_mode: KEEP
target_mode:  current mode
source:       CONTEXT_UNAVAILABLE
should_switch: false
```

默认 deadline 常量为 25 ms，但同步 Named Pipe API **只能由后台刷新 worker 调用**。client 会在同一个 deadline 内重试 service 串行处理请求时短暂出现的 Pipe 实例重建空窗；TSF key event、composition、candidate 和 commit 热路径不得直接等待该 API，IME state applier 只能读取已缓存且仍有效的短决策。

## 门禁

```text
tests/context-service/context_protocol_test.cpp
tests/context-service/editor_context_test.cpp
tests/context-service/context_service_win_test.cpp
scripts/run-context-service-tests.ps1
.github/workflows/context-service.yml
```

Ubuntu g++ 门禁严格编译并测试平台无关的 protocol codec。Windows MSVC 门禁另外：

- 构建独立 Context Service executable；
- 对真实本地 Named Pipe 执行正常 decision 往返；
- 验证 client 能在总 deadline 内连接延迟出现的下一个 Pipe 实例；
- 验证 composition protection 穿过协议后仍为 `KEEP`；
- 验证服务不存在、主动断开和接受后卡住的故障回退；
- 测量 25 ms deadline 不等待 250 ms 的假服务恢复；
- 验证 Context Engine 报告 context unavailable 时仍正常返回 `KEEP`。

## 验证边界

| 项目 | 状态 |
|---|---|
| protocol v1 codec | `implemented` / `built` / `statically_verified`；双平台 75 assertions |
| Windows Named Pipe client/server | `implemented` / `built` / `runtime_verified`；runner 67 assertions |
| 独立 Context Service executable | `implemented` / `built` |
| 服务不存在/断开/超时 `KEEP` | `runtime_verified`（固定 Windows runner） |
| Context Service singleton / `--quit` / restart | `implemented` / `built` / `real_machine_verified` |
| Context Service 安装器生命周期 | 覆盖升级与安装后启动 `real_machine_verified`；首次安装、重新登录 autorun 和卸载未验证 |
| 前台应用 source | `implemented` / `built` / `statically_verified`；见 [`evidence/m3-application-context-ci.md`](evidence/m3-application-context-ci.md) |
| 输入区域 source | terminal baseline `statically_verified`；generic surface `not_implemented` |
| 非阻塞 decision cache | `implemented` / `built` / `statically_verified`；双平台 73 assertions，见 [`decision-cache.md`](decision-cache.md) |
| TSF cache consumer / state applier | 固定 Weasel 接线 `implemented` / `built`；真实桌面未验证 |
| 服务停止时普通 TSF 输入回退 | `real_machine_verified`；同一六步 mixed-input fixture 通过 |
| 真机自动上下文切换 | `REAL_WINDOWS_VERIFICATION_REQUIRED` |
| editor context 原生接收端 | `implemented` / `built` / runner `runtime_verified`；见 [`editor-context-protocol.md`](editor-context-protocol.md) |
| report-only VS Code Adapter | `implemented` / `built` / `statically_verified`；真实桌面见 M4 验证边界 |
| Project Indexer protocol / Windows pipe | `implemented` / `built` / runner `runtime_verified`；固定帧到临时词库写入通过 |
| Project Indexer 独立 worker / lifecycle | `implemented` / `built` / runner `runtime_verified`；project pipe + singleton + `--quit` + restart 通过 |
| installed Adapter → `%APPDATA%` 项目词库 | `REAL_WINDOWS_VERIFICATION_REQUIRED` |
| 项目词进入 librime 候选 | `not_implemented` |

CI 本身只能证明构建、协议和 runner 上的 Named Pipe 集成测试；安装后进程生命周期和真实桌面输入回退由单独的主开发机证据证明，自动状态应用仍需专项真机验收。

协议与基础服务固定 CI 证据见 [`evidence/m3-context-service-ci.md`](evidence/m3-context-service-ci.md)，run `33466662527`。生命周期与 `0.3.0-preview` 安装器 CI 证据见 [`evidence/m3-context-service-lifecycle-ci.md`](evidence/m3-context-service-lifecycle-ci.md)，run `33491126742`；主开发机覆盖升级与故障回退证据见 [`evidence/m3-context-service-lifecycle-host-verification.md`](evidence/m3-context-service-lifecycle-host-verification.md)。

M4.1 editor context protocol/store 固定 CI 证据见 [`evidence/m4-editor-context-protocol-ci.md`](evidence/m4-editor-context-protocol-ci.md)，Context Service run `33628881889`。

M5.2 Language Server symbol ingestion 固定 CI 证据见 [`evidence/m5-language-server-symbol-ingestion-ci.md`](evidence/m5-language-server-symbol-ingestion-ci.md)，Project Dictionary run `33730311588`、Context Service run `33730311542`。
