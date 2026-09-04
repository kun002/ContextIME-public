# ContextIME IME Host Context Bridge（M3.5 / Weasel TSF 接线）

## 当前范围

M3.5 先建立 Context Service 与 Weasel TSF state applier 之间可独立测试的线程和故障边界：

```text
TSF focus lifecycle
        ↓ Activate / Deactivate（不在按键热路径）
background refresh worker
        ↓ 25 ms bounded Context Service IPC
single-writer Decision Cache
        ↓ generation + TTL guarded lock-free Read
fresh IME safety snapshot
        ↓
TSF-thread ImeModeSink
```

随后固定 Weasel patch-stack 已把 worker、cache reader 和 state-applier 接入真实 Weasel TSF 源码。接线只增加 Context Service 的后台自动决策路径；没有修改 librime、schema、原有按键输入链或已发布的 `0.2.3` 安装器，也没有把 `contextime-context-service.exe` 加入用户运行时。

## 线程所有权

后台 worker 只允许：

- 调用有统一 deadline 的 `EvaluateViaContextService`；
- 发布或失效 `DecisionCache`；
- 调用一个 non-throwing ready callback。

ready callback 运行在 worker 线程。固定 Weasel patch 用它向 TSF owner thread 的 message-only window 投递消息；callback 不调用 COM、TSF、candidate UI、现有 `weasel::Client` 或 `ImeModeSink`。

`ImeModeSink::ApplyMode` 只能由 TSF owner thread 调用。`ImeStateApplier` 本身不执行 IPC、Win32、clock、分配或锁；它只验证 decision 形状并最多调用 sink 一次。

## 稳定自动规则与即时安全状态

发送 Context Service 前，worker 使用 `PrepareContextRefreshSnapshot` 移除：

- composition/candidate；
- explicit lock；
- manual override deadline；
- automation-disabled 瞬时门禁。

这些字段不是被忽略，而是禁止进入可缓存 response。TSF owner thread 每次准备落地时用最新状态重新执行同一个 `ContextEngine`：

```text
composition / candidate
→ explicit lock
→ manual override
→ automation disabled
→ cached automatic rule
```

因此 worker 请求期间开始 composition、用户手动切换或关闭自动化，都会在模式落地前阻止旧 decision。

`EvaluateDecisionCacheSafety` 允许新 cache generation 尚未返回时继续执行本地安全优先级。此时陈旧自动规则不可见，但 explicit lock 仍不依赖 Context Service。

## Focus、generation 与过期结果

worker 只在 IME instance 处于 foreground focus 时刷新。每次 `Activate`、`Deactivate` 都推进 generation：

- 新 generation 发布前，旧 cache 不可读；
- 已失焦或已被后续 focus 替代的 IPC response 不发布；
- 服务失败只使当前 generation 返回 `KEEP / CONTEXT_UNAVAILABLE`；
- `Deactivate` 后即使 callback 消息已经排队，也不能读到旧自动切换；
- `Stop` 唤醒 worker，并在销毁 TSF notification target 之前 join。

周期刷新默认 250 ms，cache TTL 默认 1000 ms。请求只保留最新 focus snapshot，不建立无界队列。

## 固定 Weasel TSF 接线

当前 native patch 已按以下边界接线：

1. `ActivateEx` 创建 TSF-thread notification target 并启动 worker；失败时保持普通输入法。
2. foreground `OnSetFocus` 发布 live snapshot；失焦立即 `Deactivate`。
3. worker callback 只投递自有、隔离命名的 Windows message。
4. TSF message handler 从 `_status.ascii_mode`、`_status.composing`、`_pComposition` 和 candidate 可见状态构造最新 `DecisionCacheReadState`。
5. handler 调用 `Read` 后再调用 `ImeStateApplier`；不得在 `_ProcessKeyEvent` 中调用 Context Service。
6. sink 在 TSF 线程复用现有 `ascii_mode` server option，并读取 server response 确认状态后更新 Language Bar/TSF conversion compartment。
7. 自动 apply 必须带 origin guard；普通按键、语言栏和 compartment 产生的模式变化才延长 manual override deadline。
8. `Deactivate` 必须先停止并 join worker，再销毁 notification target 和 COM/TSF owner。

现有 Weasel `weasel::Client` 是同步 pipe client，并且连接、读写和 `FlushFileBuffers` 没有 Context Service 的 25 ms deadline。worker 因此不调用它。当前 sink 只在原有 TSF owner thread 复用该 client 更新 `ascii_mode` 并确认 server response，它不是 Context Service IPC，也没有进入 `_ProcessKeyEvent`。

## 门禁

`tests/ime-host/ime_state_applier_test.cpp` 在 Windows 和 Ubuntu 覆盖：

- KEEP/protection 不触发 sink；
- 当前模式已匹配时不重复 apply；
- 合法中文/英文 decision 最多调用 sink 一次；
- sink failure 可见且不在同次调用内重试；
- malformed decision 不触达 sink。

`tests/ime-host/context_refresh_worker_win_test.cpp` 在 Windows 覆盖：

- refresh request 不缓存瞬时安全状态；
- worker 与真实版本化 Named Pipe server 完成后台请求；
- live composition/manual 状态覆盖已缓存自动规则；
- explicit lock 在新 generation 返回前仍有效；
- `Deactivate` 屏蔽陈旧 decision；
- 服务缺失保持普通输入并可停止 worker。

统一入口：

```powershell
./scripts/run-ime-host-tests.ps1
```

## 验证边界

平台无关 M3.5 bridge core 已标记为 `implemented / built / statically_verified`，worker/pipe fixture 标记固定 runner `runtime_verified`。

固定 Windows/Ubuntu core CI 证据见 [`evidence/m3-ime-context-bridge-ci.md`](evidence/m3-ime-context-bridge-ci.md)，run `33477387139`。固定 Weasel TSF 接线和 x64/Win32 compile-only 证据见 [`evidence/m3-weasel-context-bridge-ci.md`](evidence/m3-weasel-context-bridge-ci.md)，run `33480300841`。

当前状态为：

- Weasel TSF 接线源码：`implemented`；
- Weasel TSF x64/Win32：`built`；
- patch-stack、线程与故障边界：`statically_verified`；
- state-applier / worker / Named Pipe fixture：runner `runtime_verified`；
- Context Service singleton、`--quit`、幂等 quit 和 restart：主开发机 `real_machine_verified`；
- Context Service NSIS 覆盖升级与安装后启动：主开发机 `real_machine_verified`；首次安装、重新登录 autorun 和卸载仍未验证；
- `contextime-0.3.0-preview-installer.exe`：`built / statically_verified`，覆盖升级 `real_machine_verified`；
- 服务停止时普通 TSF 输入回退：主开发机同一六步 mixed-input fixture `real_machine_verified`；
- 交互桌面自动切换、真实 composition/candidate 保护和手动切换保护：`not_real_machine_verified`。

主开发机 smoke 已证明 TSF DLL 在用户桌面加载且 Context Service 缺失不破坏普通输入，但没有证明 Terminal 自动切换或候选窗口存在时不会切换。必须保持：

```text
REAL_WINDOWS_VERIFICATION_REQUIRED
```

主开发机生命周期与回退证据见 [`evidence/m3-context-service-lifecycle-host-verification.md`](evidence/m3-context-service-lifecycle-host-verification.md)。
