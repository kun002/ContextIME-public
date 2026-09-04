# M3.5 IME Host Context Bridge CI 证据

## 固定对象

```text
workflow: IME Host Context Bridge
run: 33477387139
commit: 8eb8fb7122df54ff39dd56e9d5d3b5c33a9fe7b2
conclusion: success
date: 2026-09-01
```

Run：<https://github.com/kun002/ContextIME/actions/runs/33477387139>

## Windows 2022 / MSVC

Job `99759403418`：

```text
IME State Applier: 11 assertions passed
Context Refresh Worker Windows: 31 assertions passed
```

该 job 使用 Visual Studio 2022 MSVC x64、`/std:c++17 /EHsc /W4 /WX` 编译：

- 共享 Context Engine；
- Context Service protocol/client/server/application source；
- lock-free Decision Cache；
- IME state applier；
- Windows background refresh worker；
- state-applier 和 worker/pipe integration fixtures。

Windows fixture 启动独立、唯一测试 pipe server，验证后台 worker 完成有界请求、发布 cache、通知 reader，并由 reader 用即时 composition/manual/lock 状态重新执行安全门禁。服务不存在时保持 `KEEP / CONTEXT_UNAVAILABLE`，worker 可停止并 join。

## Ubuntu 24.04 / g++

Job `99759403420`：

```text
IME State Applier: 11 assertions passed
```

该 job 使用 `-std=c++17 -Wall -Wextra -Werror -pedantic` 编译共享 Context Engine 与平台无关 state-applier，验证 decision 形状、单次 sink apply、KEEP 和失败回退。

## 同一提交回归

PR #22 的同一 head 还通过：

- Context Engine Windows/Ubuntu；
- Context Service Windows/Ubuntu；
- Decision Cache Windows/Ubuntu；
- Application Context Windows/Ubuntu；
- 仓库通用 Windows package gate。

## 状态

| 能力 | 状态 |
|---|---|
| IME Host worker/state-applier 源码 | `implemented` |
| Windows/Ubuntu 严格编译 | `built` / `statically_verified` |
| state-applier 单元 fixture | runner `runtime_verified` |
| Windows worker + Named Pipe fixture | runner `runtime_verified` |
| 固定 Weasel TSF 接线 | 未实现 |
| 用户交互桌面自动切换 | 未验证 |
| composition/candidate/manual 的真实 TSF event 接线 | 未验证 |
| Context Service 安装、启动、升级、卸载 | 未实现 |

CI runner 不能证明 TSF DLL 在真实应用内正确加载，也不能证明候选窗口存在时不会切换。以下状态继续有效：

```text
REAL_WINDOWS_VERIFICATION_REQUIRED
```
