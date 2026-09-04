# M5.3 Active-project Candidate Bridge

## 目标与边界

M5.3 只桥接 M5.2 已采集并持久化的 Language Server symbol：

```text
VS Code active workspace
→ explicit 3-second project lease
→ Project Indexer owner loads persisted dictionary
→ bounded immutable active-project snapshot
→ background-only local Candidate Pipe
→ Weasel Server immutable snapshot
→ librime session property
→ project Lua translator
```

不增加 compilation database、project file、文件系统、Unity asset 或手工词条来源。候选快照只接受 `language_server` source。

## 输入路径

Project Dictionary Store 的磁盘 I/O 只在 Context Service Project Indexer thread。Candidate Pipe server 只读 immutable memory snapshot。Weasel worker 在后台执行 Pipe I/O，并提前生成 property string。

真实按键路径只做：

```text
atomic_load immutable runtime snapshot
→ revision unchanged: no-op
→ revision changed and no composition: librime set_property
→ original librime process_key
```

它不读磁盘、不执行 IPC、不扫描项目、不运行 Node/PowerShell/Git，也不等待 Context Service。

## Active project 与隔离

- VS Code 窗口聚焦、active document 属于受支持 workspace 时，每秒刷新项目 lease；
- 窗口失焦、切到不受支持文档或禁用项目词库时发送 deactivate；
- Adapter 崩溃或无法清理时，3 秒 lease 自动过期；
- 同项目 heartbeat 只替换 lease snapshot，不加载 Store、不改变 candidate generation；
- 项目切换先以新项目替换 owner，加载失败则发布空 snapshot，绝不保留旧项目候选；
- active 项目 upsert 成功后才重新加载并发布 candidate generation。

## 排序与限额

Service 按 `frequency desc → last_seen desc → symbol → type` 排序，按完整 symbol 去重。每个 active snapshot 最多 256 条、symbol UTF-8 总计最多 24576 bytes；wire response 固定 32768 bytes。Lua 对至少两个 ASCII identifier 字符执行大小写无关 prefix match，每次最多 yield 16 个候选。

候选保留原始符号大小写，例如输入 `player` 可产生：

```text
PlayerController 〔项目·类〕
```

## Fail-open

任何 storage、snapshot、Pipe、worker、property 或 Lua 错误都只移除项目候选。`luna_pinyin_simp` 和既有 ContextIME developer translator 仍继续处理普通拼音、技术片段和命令。

## 验证分层

- `implemented`：上述 owner、protocol、worker、Weasel property 与 Lua translator 已接线；
- `built`：0.5.0-preview 固定 Weasel/librime 和安装程序构建通过；
- `CI`：双平台 codec/snapshot、Windows Pipe/worker、Adapter 与完整原生构建门禁通过；
- `real_machine_verified`：必须在已登录且解锁的真实 Windows 桌面完成 `VS Code symbol → persisted project dictionary → ContextIME candidate`，并同时回归普通拼音和桥接故障 fail-open。
