# ContextIME 非阻塞 Decision Cache（M3.3）

## 目的

Context Service 的同步 Named Pipe client 只能在后台线程运行。TSF key event、composition、candidate 和 commit 热路径不能因为服务、pipe、调度或进程故障而等待。

M3.3 在服务与未来 IME state applier 之间建立单向短缓存：

```text
background refresh owner
  → bounded Context Service IPC
  → Publish / Invalidate
  → lock-free Decision Cache
  → bounded Read(current IME safety state)
  → future state applier
```

本阶段只建立共享 C++ cache 和门禁，不接入 TSF 或安装程序。

## 单一决策 owner

cache 不复制 Context Engine 的优先级。每次 `Read()` 先用当前 IME 状态构造 safety snapshot，并调用同一个 `ContextEngine::Evaluate`：

```text
composition/candidate
→ explicit lock
→ recent manual override
→ automation disabled
→ CONTEXT_UNAVAILABLE sentinel
```

只有 Engine 返回 `CONTEXT_UNAVAILABLE`（表示没有更高的当前安全边界触发）时，reader 才考虑缓存的稳定自动规则。这样 composition、candidate、用户锁和刚刚发生的手动切换不依赖可能过时的服务 response。

cache 只接受：

- `USER_RULE`；
- `PROJECT_RULE`；
- `SYNTAX_CONTEXT`；
- `SURFACE_CONTEXT`；
- `APPLICATION_DEFAULT`；
- `FALLBACK_KEEP`。

composition/lock/manual/automation/context-unavailable response 都是瞬时状态，禁止缓存。非法或不一致 decision 会原子失效旧值，避免坏 refresh 留下此前的切换请求。

## 热路径保证

`DecisionCache::Read`：

- `noexcept`；
- 不调用 IPC、Win32、文件、注册表、网络、AI 或系统时钟；
- 不分配内存；
- 不获取 mutex；
- 只读取调用方提供的单调时间；
- 最多尝试 3 次原子快照；
- 与 writer 冲突、没有值、到期或已失效时立即 `KEEP / CONTEXT_UNAVAILABLE`。

cache 使用三个 lock-free 64-bit atomic：sequence、packed decision、expiry。单个后台 owner 执行 Publish/Invalidate；多个 reader 可以并发读取。sequence 为奇数表示更新中，reader 不会读取一半新 decision、一半旧 expiry。

M3.5 增加 `EvaluateDecisionCacheSafety`，供 focus generation 尚未发布新 cache 时复用同一个 Context Engine 执行本地 composition/lock/manual/automation 门禁。它不会让上一 generation 的自动规则重新可见。IME Host worker 和 TSF state-applier 边界见 [`ime-context-bridge.md`](ime-context-bridge.md)。

缓存 decision 在读取时根据当前中文/英文状态重新计算 target 和 `should_switch`，不会沿用 refresh 时已经过期的 switch flag。

## 生命周期规则

后台 refresh owner 必须：

1. 只在 transport status 为 `OK` 且 decision 为稳定自动规则时 Publish；
2. 为每个 decision 提供明确 `expires_at_ms`；
3. 服务不存在、timeout、disconnect、protocol/system error 时立即 Invalidate；
4. 不允许多 writer 并发发布；
5. 不把缓存永久有效当成服务健康证明。

## 门禁

`tests/context-service/decision_cache_test.cpp` 覆盖：

- 空 cache、失效和 TTL 到期 `KEEP`；
- current mode 变化后重新计算 `should_switch`；
- composition/candidate 覆盖 cache 和 explicit lock；
- lock 覆盖 cache，且 automation disabled 时仍有效；
- automation disabled 和 manual override 覆盖 cache；
- manual deadline 边界；
- 拒绝瞬时/畸形 decision 并清除旧值；
- 10 万次单 writer publish 与并发 read 不产生 torn decision；
- Publish/Read 保持 `noexcept`。

Windows MSVC 与 Ubuntu g++ 已执行同一门禁，每个平台均为 65 assertions。固定证据见 [`evidence/m3-decision-cache-ci.md`](evidence/m3-decision-cache-ci.md)，run `33467476198`。

## 验证边界

当前模块状态为 `implemented / built / statically_verified`，并发 fixture 为固定 runner `runtime_verified`。这些结果只能证明 cache 算法和并发边界；真实 TSF state applier、manual event 接线、服务进程生命周期和用户桌面自动切换仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`。
