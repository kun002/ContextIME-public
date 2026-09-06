# ContextIME 项目词库管理（M5.4）

## 目标与边界

M5.4 为已经由 Language Server 采集并持久化的项目词库提供本地查看、
启用、禁用和整库删除入口。它不增加 symbol 数据源，不扫描项目，也不在
TSF 按键、composition、candidate 或 commit 热路径读取磁盘或执行 IPC。

```text
contextime-project-dictionary-manager.exe
        ↓ bounded CIPM request/response
\\.\pipe\ContextIME.ProjectIndexer.v1
        ↓ existing Project Indexer background owner
ProjectDictionaryStore
        ↓ publish/clear after mutation
active-project immutable snapshot
```

管理程序不直接打开 `%APPDATA%\ContextIME\project-dictionaries`。所有文件
操作继续由 Context Service 内已有的 Project Indexer 后台线程串行执行，避免
UI、Adapter 和候选桥同时拥有 Store。

## 用户界面

原生 Win32 单窗口提供：

- 匿名 128-bit project ID 列表；
- 每页最多 100 条的词条查看；
- symbol、类型、来源、频次和最后更新时间；
- 启用或禁用选中词库；
- 带二次确认的整库删除；
- 项目刷新和词条翻页；
- 术语输入框和“添加术语”：以 `term / manual` 固定来源写入选中项目，
  长度上限 128 UTF-8 bytes，去重后由 server 单调刷新；
- “删除词条”：删除词条列表中选中的单条记录（任意来源），带二次确认。

项目路径从未写入词库或管理协议，所以管理程序只显示 opaque project ID，
不会尝试反推出 workspace 路径。当前没有增加项目别名或其他元数据。
手动术语只在用户选中的既有项目上添加；没有“新建项目”入口，因为匿名
project ID 只能由 Adapter 对真实 workspace 路径域分隔哈希产生。

禁用保留词库文件和记录，并立即为仍处于 active lease 的同项目发布空候选
snapshot。重新启用会从已持久化 snapshot 恢复项目候选。删除会移除项目文件
及临时文件，并立即清除匹配的 active snapshot。仍打开的编辑器在后续 symbol
采集事件中可以重新建立已删除词库；这是“删除当前索引数据”而不是永久屏蔽
该 workspace，永久停止候选应使用禁用。删除单条词条同样立即重发 active
snapshot；被删除的 Language Server 词条会在下一次符号采集事件中重新进入
词库，这是预期行为。

## CIPM v1

管理调用与 Adapter 的 `CIPD` upsert 共用同一条 Project Indexer Pipe 和同一
后台 owner，但使用独立 magic `CIPM`。Server 读到固定 4096-byte request 后
按 magic 分流：

- `CIPD` 保持既有 4096-byte request / 32-byte response，不改变 Adapter；
- `CIPM` 使用 4096-byte request / 32768-byte response；
- 两者都必须在完整 response 后发送 ACK `0x06`；
- list 每页最多 512 个 project ID；
- view 每页最多 128 个词条；
- client 的 open/write/read/ACK 共用一个 5 秒 UI deadline；
- Pipe 仍由 server 使用当前用户与 `SYSTEM` DACL 及
  `PIPE_REJECT_REMOTE_CLIENTS` 创建。

`CIPM` 只能承载 project ID、enabled、分页游标以及既有词条模型：

```text
symbol / symbol_type / source / frequency / last_seen
```

M5.5 的 `UPSERT_TERM / REMOVE_ENTRY` 在 request 数据区携带同模型中的
`symbol / symbol_type / source`；frequency 和 last_seen 仍由 server 拥有。
它没有 workspace path、URI、源码、range、detail、container、输入正文、密码、
Token、API Key 或环境变量字段。

## 故障回退

Context Service 不存在、Pipe busy、超时、断开、协议错误、词库损坏或 I/O 失败
时，管理窗口只显示诊断状态。失败不会修改 IME 状态，也不会进入输入线程。
项目候选桥失败时仍按 M5.3 规则清空项目 property；普通 librime 拼音 translator
继续工作。

## 验证分层

- `implemented`：CIPM codec、Windows client/server、Win32 UI、active snapshot
  mutation semantics 和安装器接线；
- `built`：必须通过 Windows MSVC `/W4 /WX` 和 Ubuntu g++
  `-Wall -Wextra -Werror -pedantic`；
- `CI`：固定 codec、真实 Windows local Named Pipe、列表/分页/禁用/启用/删除、
  active immutable snapshot 更新及安装包 manifest；
- `real_machine_verified`：必须在登录且解锁的 Windows 交互桌面从安装后的开始
  菜单入口完成查看、禁用、启用和测试词库删除，同时回归普通拼音 fail-open。

`0.5.2-preview` 已在登录且解锁的 Windows 11 交互桌面完成安装后开始菜单
入口、10 万词库分页、禁用、启用、过期 lease heartbeat、二次确认删除和普通
拼音 fail-open 验证。验收期间发现并修复“过期 snapshot 可被禁用项目 heartbeat
重新续租”的边界；修复后禁用状态保持 0 个候选，重新启用后恢复 256 个候选。
完整分层证据见
[`evidence/m5-project-dictionary-management-host-verification.md`](evidence/m5-project-dictionary-management-host-verification.md)。

状态：`M5.4_PROJECT_DICTIONARY_MANAGEMENT = VERIFIED`。干净机、LAN 和 RDP
仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`。

## M5.5 手动术语入口

M5.5 在不新增 pipe、不改变 `CIPD` Adapter 路径的前提下，把“用户批准的
技术术语”接入同一 Project Indexer owner：

- `CIPM` 增加 `UPSERT_TERM` 和 `REMOVE_ENTRY`，由管理窗口触发；
- Store 增加 `RemoveEntry`：按 `symbol + symbol_type + source` 精确删除，
  幂等且与 immutable 缓存一致；
- active-project snapshot 发布过滤器从仅 `language_server` 扩展为
  `language_server + manual`，手动术语随后续 M5.3 桥接进入 librime 候选，
  标记为 `〔项目·术语〕`；
- 手动术语的频次在 M6 学习接入前保持固定；重复添加只刷新 server 观察
  时间。

当前边界：

- 手动术语按原始 UTF-8 前缀匹配拼音输入串；英文标识符和术语可直接前缀
  命中，中文词语不会由拼音前缀命中，需要后续 pinyin 标注支持；
- `UPSERT_TERM` 不能创建不存在的项目词库；
- CI 编译/单测见 [`evidence/m5-approved-terms-ci.md`](evidence/m5-approved-terms-ci.md)；
- `0.5.4-preview` 暂存服务与管理器已在登录桌面完成真实 Adapter 采集、
  添加术语落盘、视图刷新、M5.4 整库删除回归和真实词库零扰动，见
  [`evidence/m5-approved-terms-host-verification.md`](evidence/m5-approved-terms-host-verification.md)；
  安装版候选上屏验收仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`。
