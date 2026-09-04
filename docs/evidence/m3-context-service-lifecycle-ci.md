# M3 Context Service 生命周期与 0.3.0 Preview CI 证据

## 固定对象

```text
workflow: ContextIME Native Preview 0.3.0
run: 33491126742
job: 99802620019
build commit: 697510714340a761631d4ec76b3261d181c97654
conclusion: success
date: 2026-09-01
```

Run：<https://github.com/kun002/ContextIME/actions/runs/33491126742>

同一 commit 的独立 Context Service 双平台 run `33491126619`、Weasel TSF bridge run `33491126666`、identity patch check、clean-runner evidence 和仓库通用 CI 也全部通过。

## 为什么使用 companion process

本阶段没有引入 Windows SCM 服务、守护进程或恢复管理器。Context Service 是一个隐藏的普通用户会话 companion executable：

```text
contextime-context-service.exe
```

它复用现有 ContextIME NSIS 安装和 HKLM Run 生命周期，只增加：

- 独立 `Local\ContextIME.ContextService.Singleton.v1` mutex；
- 独立 `Local\ContextIME.ContextService.Stop.v1` stop event；
- 幂等 `--quit`，发出停止信号、唤醒阻塞的 pipe accept，并等待进程释放 mutex；
- 安装后启动和 `ContextIMEContextService` autorun；
- 覆盖升级、同目录覆盖和卸载前的有界停止；
- 卸载/升级时删除 Context Service autorun。

Context Service IPC 仍只由后台 refresh worker 调用，不进入 TSF `_ProcessKeyEvent`。服务不可用时保留普通 Weasel/librime 输入。

## 固定上游与构建链

```text
Weasel 0.17.4
9cc96e20dc71b80876b12f689bb5863c76c2a7ed

librime 1.13.1
1c23358157934bd6e6d6981f0c0164f05393b497

runner: win22
image: 20260824.284.2
```

runner 应用完整 patch-stack：

```text
normalize
→ ContextIME identity
→ 0.1.1 ... 0.2.3 fixes
→ M3 Weasel TSF Context Bridge
→ 0.3.0 Context Service lifecycle
```

完整 Weasel derivative source patch：

```text
contextime-weasel-0.3.0.patch
bytes: 497,320
SHA-256: a2b2e54aea170773bd94bfb8d05e40a9c10d96b3f1d1d3d274ef776422408f45
```

Artifact 同时包含 Context Engine、Context Service、IME Host 原始源码和 native patch scripts。

## Runner 编译与生命周期 fixture

Context Service 使用 MSVC `/std:c++17 /EHsc /W4 /WX` 构建为 Windows subsystem executable，不在登录时弹出 console。固定 runner 通过：

```text
Context Protocol: 46 assertions passed
Decision Cache: 73 assertions passed
Application Context: 31 assertions passed
Context Service Windows: 47 assertions passed
Context Service lifecycle passed: singleton, quit, restart
```

生命周期 fixture 使用唯一测试 pipe/mutex/event，实际启动 production-shaped executable，验证：

- 首个进程保持运行；
- 第二个实例立即成功退出，不形成重复服务；
- `--quit` 有界等待首个进程退出；
- 停止后能够重新启动；
- 重启后的进程仍能由 `--quit` 正常停止。

同一 job 还重跑：

```text
IME State Applier: 11 assertions passed
Context Refresh Worker Windows: 31 assertions passed
```

## 安装器产物

```text
file: contextime-0.3.0-preview-installer.exe
bytes: 12,133,784
SHA-256: 5f70f757fa98c21251f416f0c2d18b975655911ceeb29c53324f16e50441daba
Build commit: 697510714340a761631d4ec76b3261d181c97654
```

打包的 Context Service：

```text
file: contextime-context-service.exe
bytes: 318,976
SHA-256: 50e1e2fbe672267abc25c41cb403194b7d5eb0c58b3d4b5fe18a5e2cb6fd6b24
```

安装器 manifest gate 确认根目录包含 Context Service executable，并继续包含 `contextime_developer.schema.yaml` 和必需的 `data\rime.lua`。NSIS 脚本静态审计确认升级、覆盖和卸载停止路径，以及 autorun 写入/删除路径。

Artifact：

```text
name: contextime-native-0.3.0-preview
artifact id: 9793968934
archive bytes: 12,272,768
archive digest: sha256:d147d11c038ad907b297df747db8e95206fa40435551ada211abc8cffcfefd3b
retention: 14 days
```

Artifact ZIP 是 CI 证据容器；用户安装对象是其中的 `.exe`。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| Context Service singleton / `--quit` / restart | `implemented` / `built` / runner `runtime_verified` | production-shaped executable fixture |
| 独立 pipe/mutex/event/autorun identity | `implemented` / `statically_verified` | identity、source 和 installer audit |
| M3 Weasel TSF + Context Service 完整 patch-stack | `built` / `statically_verified` | 固定上游全量 native build |
| 0.3.0 preview installer | `built` / `statically_verified` | NSIS 产物、manifest 和 SHA-256 |
| NSIS 首次安装与 Context Service 自动启动 | `not_runtime_verified` | runner 没有执行安装器 |
| 0.2.3 → 0.3.0 覆盖升级 | `not_runtime_verified` | 只有升级脚本静态证据 |
| 卸载停止进程并删除 autorun/files | `not_runtime_verified` | 只有卸载脚本静态证据 |
| 真实 TSF 自动模式切换 | `not_real_machine_verified` | 无用户交互桌面输入证据 |
| 真实 composition/candidate/manual 保护 | `not_real_machine_verified` | 只有 fixture 和源码证据 |
| Context Service 崩溃/超时下真实应用输入 | `not_real_machine_verified` | 只有故障 fixture 和线程边界 |

因此必须继续标记：

```text
REAL_WINDOWS_VERIFICATION_REQUIRED
```

后续主开发机已完成覆盖升级、安装后启动、singleton、幂等 `--quit`、restart 和服务停止时普通 TSF 输入回退，见 [`m3-context-service-lifecycle-host-verification.md`](m3-context-service-lifecycle-host-verification.md)。首次安装、重新登录 autorun、卸载和真实自动切换保护仍未验证。
