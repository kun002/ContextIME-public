# ContextIME 项目词库（M5.3 Candidate Bridge / M5.4 Management）

## 当前范围

M5.1 建立可独立验证的本地项目词库 core。M5.2 增加 VS Code Language Server symbol 到 Context Service 的有界后台写入链路。M5.3 只把这些已经批准并持久化的 Language Server symbol 接入当前项目的 librime 候选，不增加新数据源。
M5.4 在相同 Project Indexer 后台 owner 上增加查看、启用、禁用和整库删除入口；
协议和 UI 边界见 [`project-dictionary-management.md`](project-dictionary-management.md)。
M5.5 增加用户手动批准的术语入口：管理窗口通过 `CIPM` 的
`UPSERT_TERM / REMOVE_ENTRY` 以 `term / manual` 固定来源写入选中项目或删除
单条记录，active-project snapshot 发布过滤器同步扩展到 `manual` 来源，
手动术语沿用 M5.3 候选桥接并标记 `〔项目·术语〕`。

```text
VS Code document symbol provider
        ↓ name / kind / children only
report-only Adapter
        ↓ fixed 4096-byte local batches
\\.\pipe\ContextIME.ProjectIndexer.v1
        ↓ dedicated Context Service worker
ProjectDictionaryStore
        ↓
%APPDATA%\ContextIME\project-dictionaries\<opaque-project-id>.dict
        ↓ Project Indexer owner thread: load/rank/bound
active-project immutable snapshot (256 entries / 24 KiB symbols)
        ↓ \\.\pipe\ContextIME.ProjectCandidate.v1 (background only)
Weasel Server immutable process snapshot
        ↓ librime session property (revision changes only)
lua_translator@contextime_project_translator
```

Context decision 继续使用 `\\.\pipe\ContextIME.ContextService.v1`。项目索引使用独立 pipe 和后台线程，磁盘 I/O 不占用 Context decision loop，更不会进入 TSF key-event、composition、candidate 或 commit 热路径。完整 wire layout 见 [`project-dictionary-protocol.md`](project-dictionary-protocol.md)。

当前 C++ core 位于 `src/project-indexer/`。它提供：

- 32 字符 lowercase hex 匿名 `project_id` 的严格校验；
- 按 `project_id` 分文件隔离；
- 以 `symbol + symbol_type + source` 为键的增量 upsert；
- `frequency` 和 `last_seen` 单调更新；
- 查看 snapshot、列出项目、启用/禁用和整库删除 API；
- 临时文件写入和同目录原子替换；
- 有界条目数、文件大小、symbol 大小及 UTF-8 校验；
- 损坏文件拒绝覆盖，保留现场供诊断。

这些 API 含文件 I/O，只能由 Context Service 的后台 Project Indexer 单 owner 调用。M5.3 的候选 transport 只读取 immutable memory snapshot；Weasel Server 的 worker 只在后台请求该 transport。按键路径不调用 Store、Pipe、Language Server、项目扫描或 Node。

active project 由 Adapter 在 VS Code 窗口聚焦且当前文档属于受支持 workspace 时显式声明，并每秒刷新一次 3 秒 lease。窗口失焦、切到非项目文档或关闭词库功能时显式清除；进程意外退出时 lease 自动过期。相同项目的 lease 刷新不重新读盘，项目切换或 active 项目的 upsert 才由 Project Indexer owner 重新加载并发布快照。

## Language Server symbol source

Adapter 只对当前 active editor 中属于 workspace 的本地文件请求 VS Code `vscode.executeDocumentSymbolProvider`。当前允许的 language ID：

```text
c cpp csharp
javascript javascriptreact
typescript typescriptreact
python
```

采集边界固定为：

- 只保留 `name`、`kind` 和递归 `children`；
- 只映射 class、method、property、enum、namespace 和 file 类型；
- 一个文档最多保留 256 个有效 symbol；
- 相同 `symbol_type + name` 在同次采集中合并 frequency；
- workspace 本地路径先规范化，再用域分隔 SHA-256 产生前 128-bit lowercase hex `project_id`；
- 不保留或传输源码、document URI、workspace 路径、range、detail、container name、字符串值、Token 或环境变量值。

采集只由 Adapter activate、active editor、document save、workspace folder/configuration 变化或用户执行 `ContextIME: Refresh Project Symbols` 触发。默认 debounce 为 500 ms，同一 document version 不重复发送；它不在每次按键时调用 Language Server。

Language Server 不存在、返回空结果或抛错时，本次采集静默跳过。Project Indexer pipe 不存在、超时、断开、拒绝或协议错误时只记录 Adapter 诊断，不影响普通拼音输入。

## 数据模型

一个逻辑记录只有：

```text
symbol
symbol_type
project_id
frequency
last_seen
source
```

首批 `symbol_type`：

```text
class method property enum namespace
file directory asset shader term
```

首批 `source`：

```text
language_server compilation_database project_file file_system manual
```

同一 symbol 可以有不同 type/source，便于后续候选层解释来源；不同项目中的记录和 frequency 完全独立。项目 ID 已由 Adapter 在本地对 workspace identity 做域分隔哈希后产生，真实路径不进入 wire frame 或词库文件。它用于本机项目隔离，不是跨设备稳定标识，也不可用来恢复原路径。

## 磁盘格式与隐私

每个项目使用 `<project_id>.dict`。文件头固定格式版本和 enabled 状态；记录使用稳定枚举名、frequency、last_seen 和 UTF-8 symbol 的 lowercase hex 表示。`project_id` 只出现在经过严格校验的文件名中。

core 不包含网络、源码解析或目录扫描能力。它拒绝：

- 非规范项目 ID 和目录穿越；
- 空 symbol、控制字符、换行、路径分隔符和盘符分隔符；
- 无效 UTF-8、零 frequency、未知 type/source；
- 超过 128 UTF-8 bytes 的 symbol；
- 超过 100,000 条或 32 MiB 的单项目词库。

这些校验不能从任意文本中猜测秘密，因此 source adapter 仍只使用 Language Server/编辑器提供的 symbol name，禁止读取字符串值、源码正文、密码、Token、API Key 和环境变量值。

## 故障与删除边界

- 不存在的项目读取为空、enabled 的默认 snapshot，不创建文件；
- disable 保留数据但不会被后续 upsert 隐式启用；
- delete 同时清理项目文件和同项目临时文件，并且幂等；
- 解析失败返回 `CORRUPT_DATA`，upsert 不覆盖损坏文件；
- I/O 或内存失败返回 `IO_ERROR`，不得影响普通输入。
- active project 加载失败时立即清除旧项目快照，不能跨项目保留候选；
- Candidate Pipe 缺失、超时、断开或协议错误时 Weasel worker 在 lease 到期后发布空 property；
- Lua property 为空或格式错误时 project translator 不产生候选，原 librime 拼音 translator 继续工作。

Store 仍按单 owner 设计，不提供跨线程锁。跨线程共享对象只有发布后不再修改的 `ActiveProjectSnapshot` 和 Weasel runtime snapshot，使用 atomic shared snapshot 交换。

## 门禁与未完成边界

`scripts/run-project-indexer-tests.ps1` 在 Windows/MSVC 和 Ubuntu/g++ 使用 C++17、warnings-as-errors 编译，并验证：

- ID、UTF-8、隐私字段和大小边界；
- 两个项目的持久化隔离；
- stale observation 不回退 frequency/last_seen；
- restart 后读取、列出、disable 和 delete；
- 损坏文件拒绝及不覆盖行为。

M5.2 另外验证：

- TypeScript/C++ request 和 response codec 的 canonical fixture 完全一致；
- 固定 4096-byte request、32-byte response、64 records/request 和严格 reserved bytes；
- symbol kind 映射、workspace ID 规范化、去重、UTF-8 与 256 symbols/document 上限；
- Adapter 的多批次总 deadline、ACK、拒绝、超时、断开和服务缺失诊断；
- Windows 真实 local Named Pipe 接收后写入临时 project dictionary；
- Context Service 的 project pipe、singleton、`--quit` 和 restart lifecycle；
- Windows/Ubuntu workspace 57 项 TypeScript 回归和 Windows VSIX package audit。

M5.3 增加的固定门禁：

- active/deactivate operation、3 秒 lease、项目切换、禁用和过期；
- 只选 Language Server source，按 frequency/last_seen 排序并去重；
- 256 candidates、24 KiB symbol payload 和 32 KiB response frame 上限；
- Candidate Pipe 的 DACL、`PIPE_REJECT_REMOTE_CLIENTS`、严格 codec 和 ACK；
- Weasel worker 的后台 fetch、atomic snapshot、失联清空和 composition 内不更新 property；
- `rime.lua` 只读 session property，最多产生 16 个大小写无关 prefix candidate，并显示 `〔项目·类型〕`。

M5.3 固定 CI 还验证了双平台 snapshot/candidate codec、Windows local Candidate Pipe、Weasel 后台 refresh worker、固定上游 x64/Win32 构建、安装器内容和 Lua 候选配置。分层结果见 [`evidence/m5-project-candidate-bridge-ci.md`](evidence/m5-project-candidate-bridge-ci.md)。

`0.5.0-preview` 已在登录且解锁的 Windows 11 Build 26200 交互桌面完成：

- VS Code 内置 TypeScript Language Service 产生 `PlayerController` 和 `spawnPlayer` 两个 symbol；
- Adapter 将两项写入 installed Context Service，`%APPDATA%\ContextIME\project-dictionaries\<project-id>.dict` 可见对应 `language_server` 记录；
- 输入 `player` 时真实候选窗口第一项为 `PlayerController 〔项目·类〕`，空格提交 `PlayerController`；
- 完全停止 Context Service、确认 Context/Indexer/Candidate 三个 Pipe 均消失后，Notepad 普通拼音 `nihao → 你好`、英文透传及中文恢复仍通过；
- 恢复服务后三个 Pipe 重新出现。

主机原始 JSON、候选截图、词库副本、Adapter 日志、资源快照和 SHA-256 见 [`evidence/m5-project-candidate-bridge-host-verification.md`](evidence/m5-project-candidate-bridge-host-verification.md)。当前状态为 `M5.3_PROJECT_CANDIDATE_BRIDGE = VERIFIED`。

M5.3 仍未验证或未完成的边界：其他 Language Server 的真实 symbol 行为、后续数据源、干净机、LAN 和 RDP。管理入口由下方 M5.4 单独关闭。M5.2 固定 CI 证据仍见 [`evidence/m5-language-server-symbol-ingestion-ci.md`](evidence/m5-language-server-symbol-ingestion-ci.md)。

## 大项目后台增量性能

Project Indexer 单 owner 现在只缓存最近访问项目的 immutable dictionary
snapshot。每个最多 64 条的请求先在有界 batch 内归一化，再与最多 100,000
条的已排序 snapshot 做线性 merge；持久化成功后，Server 直接发布 Store
返回的同一份 persisted snapshot，不再立即从磁盘完整解析一遍。

缓存命中前核对目标文件的存在状态、大小和最后写入时间。外部文件变化会
回退到真实 `Load`；损坏数据仍返回 `CORRUPT_DATA`、清空输出并保留现场。
disable/delete 也会更新或失效缓存。缓存仍只属于 Project Indexer 后台单
owner，没有增加锁、IPC 或按键热路径工作。

固定压力门禁包含 100,000 条持久化及重载、active-project 256 条有界快照、
四轮各 64 条增量更新，以及更新期间并发读取 immutable snapshot。Windows
和 Ubuntu CI 均通过 12/12；Windows CI 四轮更新为 2,592 ms，Windows 11
真实主机 fixture replay 为 2,779 ms。完整数据与分层状态见
[`evidence/m5-project-dictionary-performance-ci.md`](evidence/m5-project-dictionary-performance-ci.md)。

`0.5.1-preview` 随后在 Windows 11 登录且解锁的交互桌面完成安装版压力
验收：已安装 Context Service 通过真实 Project Indexer Pipe 对 100,000 条
词库执行 20 轮各 64 条更新，累计 14,358 ms、单轮最大 647 ms；同时
Notepad 的 `nihao → 你好` 候选与上屏、英文 `abc` 和中文恢复全部通过，
Context Service、ContextIME Server 与官方 Weasel 均未重启。固定测试词库
已精确清理，原有项目词库保留。主机证据见
[`evidence/m5-project-dictionary-performance-host-verification.md`](evidence/m5-project-dictionary-performance-host-verification.md)。

状态：`M5_PROJECT_DICTIONARY_LARGE_PROJECT = VERIFIED`。

## M5.4 管理入口

`contextime-project-dictionary-manager.exe` 是独立原生 Win32 管理窗口。它不
直接访问词库目录，而是使用 `CIPM` 固定有界帧复用
`\\.\pipe\ContextIME.ProjectIndexer.v1`，因此 list/view/set-enabled/remove
仍由同一个 Project Indexer 后台线程串行执行。disable 和 delete 会同步更新
active-project immutable snapshot；管理调用失败不影响普通拼音输入。

当前实现和协议门禁见
[`project-dictionary-management.md`](project-dictionary-management.md)。

`0.5.2-preview` 已在 Windows 11 登录且解锁的交互桌面完成安装版管理器验收：
10 万词库第一页、下一页和上一页正确；禁用/启用后的立即查看不再误报
`UNAVAILABLE`；过期 snapshot 不能被禁用项目 heartbeat 重新续租；启用后
候选恢复；固定测试词库经二次确认删除且真实词库哈希不变；Context Service
停止时普通拼音仍可用。证据见
[`evidence/m5-project-dictionary-management-host-verification.md`](evidence/m5-project-dictionary-management-host-verification.md)。

状态：`M5.4_PROJECT_DICTIONARY_MANAGEMENT = VERIFIED`。干净机、LAN 和 RDP
仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`。

## M5.5 用户批准术语

手动术语沿用 Project Dictionary 单 owner 与 immutable snapshot 模型：

- 来源固定 `manual`、类型固定 `term`，只接受既有 Store 校验（合法 UTF-8、
  无控制字符/路径分隔符、最多 128 bytes、单项目 100,000 条上限）；
- `UPSERT_TERM` 只作用于已存在的项目词库，重复添加为单调刷新；
- `REMOVE_ENTRY` 按 `symbol + symbol_type + source` 精确删除，幂等；
- active-project 发布过滤器扩展为 `language_server + manual`；发布、排序、
  256 条/24 KiB 上限和按 symbol 去重规则与 M5.3 完全一致；
- 手动术语不进入 Adapter 采集路径，不改变按键热路径；管理调用失败仍只
  显示诊断，普通输入不受影响。

边界：中文词语不会由拼音前缀命中（需后续 pinyin 标注）；频次在 M6 学习
接入前固定；安装版真机验收在 CI 与新预览包完成后单独关闭。
