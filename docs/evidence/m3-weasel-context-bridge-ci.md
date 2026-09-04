# M3 固定 Weasel TSF Context Bridge CI 证据

## 固定对象

```text
workflow: M3 Weasel TSF Context Bridge
run: 33480300841
job: 99768199155
commit: 7bbf06ecd950fdc3ad22f89e0e097f45774220dc
conclusion: success
date: 2026-09-01
```

Run：<https://github.com/kun002/ContextIME/actions/runs/33480300841>

## 固定上游与 patch-stack

构建门禁从仓库锁文件读取并再次核对：

```text
Weasel 0.17.4
9cc96e20dc71b80876b12f689bb5863c76c2a7ed

librime 1.13.1
1c23358157934bd6e6d6981f0c0164f05393b497
```

Windows runner 从上述提交递归检出 Weasel/librime，依次应用 normalize、ContextIME identity、`0.1.1` 至 `0.2.3` patch-stack，再应用 M3 TSF bridge。门禁执行 `git diff --check`，并把包含新增 GPL 源文件的完整 source patch 随 artifact 输出。

```text
contextime-weasel-m3-context-bridge.patch
bytes: 495,978
SHA-256: 10957c80d4cb79c0fa6755b196af3641705e3e159c4e063318c91748f8de18af
```

## 已实现接线路径

固定 Weasel TSF patch 已把 M3.5 worker/cache/state-applier 接入真实 Weasel 源码：

```text
ActivateEx
→ 创建 ContextIME message-only window
→ 启动 ContextRefreshWorker

OnSetFocus
→ Activate / Deactivate worker

worker callback
→ 只执行 PostMessageW

TSF owner thread
→ 读取最新 composition/candidate/manual/lock 状态
→ Decision Cache Read
→ ImeStateApplier
→ Weasel ascii_mode option
→ 读取 server response 确认
→ 更新 Language Bar / conversion compartment
```

自动化初始化失败不会使 TSF activation 失败。Context Service IPC 不进入 `_ProcessKeyEvent`；worker 不调用 COM、TSF、candidate UI 或现有同步 `weasel::Client`。

## 线程、状态与故障边界门禁

静态审计确认：

- worker callback 只向 `ContextIME.TSF.ContextDecisionWindow.v1` 投递自有消息；
- message handler 必须回到创建 bridge 的 TSF owner thread；
- composition 使用 `_status.composing || _pComposition != nullptr`；
- candidate 使用明确的 `IsContextCandidateVisible()`；
- 手动切换保护期为 5 秒；
- 自动 apply 有 1 秒 pending-origin guard，自动回执不会被误记为手动切换；
- mode sink 复用 Weasel `ascii_mode` option，并读取 server response 后确认状态；
- `Deactivate` 先停止并 join worker，再销毁通知窗口、Weasel session 和 COM/TSF owner；
- 服务缺失、失败或超时只使自动 decision 回退，普通 Weasel/librime 输入链保持可用。

canonical fixtures 在同一 job 重跑：

```text
IME State Applier: 11 assertions passed
Context Refresh Worker Windows: 31 assertions passed
```

这些 fixture 覆盖 Named Pipe worker 请求、cache 发布/读取、即时 composition/manual/lock 重评估、generation 失效和服务缺失回退。它们属于 runner fixture，不是用户交互桌面验证。

## 双架构编译结果

Visual Studio 2022 / MSVC `v143`、Windows SDK `10.0.19041.0` 通过 `weasel.sln /t:WeaselTSF` 构建 Release x64 和 Win32：

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| `weaselx64.dll` | 1,059,840 bytes | `68b53b4288fbe13fa0708a67a0480d0057a1a35d3908f7d3a1e73866e00a2196` |
| `weasel.dll` | 911,872 bytes | `09860b64abefdb43c6ee91c02efc567908dbd9897633edfc0789cead2926616d` |

Artifact：

```text
name: contextime-m3-weasel-context-bridge-build
artifact id: 9789735669
archive bytes: 934,645
archive digest: sha256:7f2f6017651cb476611a00fd87a8299ea974bd99740b61fa3799bd1a0f53f954
retention: 14 days
```

该 artifact 是 compile-only evidence，不是 ContextIME 用户安装包。门禁没有生成或发布新安装器，也没有修改已发布的 `contextime-0.2.3-preview-installer.exe`。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| 固定 Weasel TSF bridge 源码 | `implemented` | 可重复 patch-stack |
| Weasel TSF x64 / Win32 | `built` | 两个 Release DLL |
| 上游、patch、线程和故障边界 | `statically_verified` | 固定 SHA、diff 和 source audit |
| state-applier / worker / pipe fixtures | runner `runtime_verified` | 11 + 31 assertions |
| Context Service 用户运行时生命周期 | `not_implemented` | 尚未安装、启动、升级、停止或卸载 |
| 新版本 ContextIME 安装器 | `not_built` | 0.2.3 发布包未改变 |
| 真实应用自动切换 | `not_verified` | 无交互桌面安装与输入证据 |
| composition/candidate 期间保护 | `not_real_machine_verified` | 只有源码和 fixture 证据 |
| 用户手动切换 5 秒保护 | `not_real_machine_verified` | 只有源码和 fixture 证据 |
| 服务崩溃/超时下正常输入 | `not_real_machine_verified` | 只有服务缺失 fixture 和静态边界 |

CI 成功证明接线可以在固定上游上构建，不证明 DLL 已被 Windows TSF 加载，也不证明真实应用中的模式切换、候选保护或故障回退已经工作。因此继续标记：

```text
REAL_WINDOWS_VERIFICATION_REQUIRED
```
