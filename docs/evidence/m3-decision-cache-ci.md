# M3.3 非阻塞 Decision Cache CI 证据

## 结果

ContextIME 已建立 Context Service 后台 refresh 与未来 IME state applier 之间的 lock-free decision cache，并在 Windows MSVC 与 Ubuntu g++ runner 上执行相同门禁：

```text
Decision Cache: 65 assertions passed
```

该证据证明 cache 已经 `implemented`、`built` 和 `statically_verified`，并在两个 runner 进程中完成并发 `runtime_verified`。它没有接入 ContextIME TSF 前端，因此不证明真实按键路径、手动切换事件或自动状态应用已完成。

## 固定构建身份

- Source commit：`92ec8284c0a9f875aca42df92510044ade5a45d9`
- PR：`#20 Add nonblocking Context decision cache`
- Pull request workflow run：`33467476198`
- Workflow：`Context Service`
- Windows job：`99730220216`
- Ubuntu job：`99730219978`
- Windows runner：`windows-2022`
- Ubuntu runner：`ubuntu-24.04`

Run URL：

```text
https://github.com/kun002/ContextIME/actions/runs/33467476198
```

## 双编译器门禁

Windows：

```text
MSVC /std:c++17 /EHsc /W4 /WX
Context Protocol: 46 assertions passed
Decision Cache: 65 assertions passed
Context Service Windows: 42 assertions passed
Context Service executable built
job conclusion: success
```

Ubuntu：

```text
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -pthread
Context Protocol: 46 assertions passed
Decision Cache: 65 assertions passed
job conclusion: success
```

## 已验证 cache 语义

65 项断言覆盖：

- 空 cache 返回 `KEEP / CONTEXT_UNAVAILABLE`；
- transport failure 执行 Invalidate 后立即 `KEEP`；
- TTL 未到期时返回缓存自动规则，到期边界立即失效；
- current mode 改变后重新计算 target/`should_switch`；
- composition 和 candidate 使用共享 Context Engine 覆盖 cache/lock；
- explicit lock 覆盖 cache，automation disabled 时仍保持用户锁；
- automation disabled 与 recent manual override 覆盖 cache；
- manual deadline 到期边界后才允许缓存规则；
- 只接受 user/project/syntax/surface/application 与 fallback KEEP；
- composition/lock/manual/context-unavailable 等瞬时 decision 禁止缓存；
- 畸形或不一致 decision 被拒绝并原子清除旧值；
- 10 万次单 writer 交替 Publish 与并发 Read 不产生混合 source/mode 的 torn decision；
- reader 与 writer 竞争时允许立即 `KEEP`，没有等待条件；
- Publish/Read 保持 `noexcept`；
- x64 `atomic<uint64_t>` 必须满足 compile-time `is_always_lock_free`。

## 输入热路径静态边界

`DecisionCache::Read` 只执行：

1. 用调用方当前安全状态调用纯 `ContextEngine::Evaluate`；
2. 最多 3 次 sequence/decision/expiry 原子快照；
3. 根据当前 mode 重算 decision。

它没有 Named Pipe、Windows API、clock、文件、注册表、网络、AI、内存分配、mutex、sleep 或无限重试。后台 owner 才能调用同步 Context Service client；任何 transport/protocol failure 都必须 Invalidate。

单 writer 是当前 API contract。测试没有允许多个后台 writer 并发发布，也不能把本门禁解释成多 writer 已支持。

## 验证状态

| 项目 | 状态 | 证据边界 |
|---|---|---|
| lock-free decision cache | `implemented` / `built` / `statically_verified` | 双编译器，65 assertions |
| 并发 Publish/Read | `runtime_verified` | 两个平台各 10 万次 publish fixture |
| composition/candidate safety read | `statically_verified` | 共享 Engine + cache 单元门禁 |
| manual/lock safety read | `statically_verified` | 共享 Engine + cache 单元门禁 |
| Context Service IPC | `unchanged` | 原 46 + 42 assertions 继续通过 |
| 0.2.3 TSF/librime 输入行为 | `unchanged` | 未修改 `native/`、schema 或 installer |
| TSF cache consumer/state applier | `not_implemented` | 后续原生集成 |
| 服务安装/进程生命周期 | `not_implemented` | 后续原生集成 |
| 真机自动上下文切换 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 无 runtime consumer |

## M3.3 结论与下一项

M3.3 关闭“服务 IPC 不进入按键热路径”的共享缓存基础。下一项是前台应用/输入区域 Context Source；其采集只能在服务/后台线程完成，并向 cache 发布有 TTL 的短 decision，不能让未来 TSF reader 查询 Win32/UI Automation。
