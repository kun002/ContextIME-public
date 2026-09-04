# M3 Context Service 生命周期主机验证

## 结论

`contextime-0.3.0-preview-installer.exe` 已在主开发机完成 `0.2.3-preview → 0.3.0-preview` 覆盖升级。安装后的 Context Service singleton、幂等 `--quit`、退出等待、停止后输入回退和重启均已在已登录且解锁的交互桌面 Session 中验证。

本轮没有证明真实 Terminal 自动切换、composition/candidate 期间保护、5 秒 manual override、首次安装、登录后 autorun 或卸载清理。因此 M3 仍保留：

```text
REAL_WINDOWS_VERIFICATION_REQUIRED
```

## 验证对象

```text
installer: contextime-0.3.0-preview-installer.exe
installer bytes: 12,133,784
installer SHA-256: 5f70f757fa98c21251f416f0c2d18b975655911ceeb29c53324f16e50441daba
build commit: 697510714340a761631d4ec76b3261d181c97654
CI workflow run: 33491126742
PR head: 4f391112d901a8b0a4b284f5ff25c9436a53e822
merge commit: 5b311184bd8e2f2407f9ea1f63858b92eacdfda5
installed version: 0.3.0-preview
Windows display version: 25H2
Windows build: 26200.9168
desktop session: 1
```

安装前主机为 `0.2.3-preview`，ContextIME Server 和官方小狼毫 `0.17.4` 同时运行；Context Service 文件、进程、pipe 和 autorun 均不存在。

## 覆盖升级

安装器通过正常 Windows UAC 授权后执行 `/S` 覆盖升级。安装后的只读收敛证据为：

- uninstall registry 的 `DisplayVersion` 为 `0.3.0-preview`；
- 安装目录为 `C:\Program Files\ContextIME\contextime-0.3.0-preview`；
- 旧 `contextime-0.2.3-preview` 目录已移除；
- `ContextIMEServer` autorun 指向新目录；
- `ContextIMEContextService` autorun 指向新目录；
- ContextIME TSF x64/WOW6432Node 注册项仍存在；
- ContextIME Server、Context Service 和官方小狼毫都在交互 Session 1 运行；
- `\\.\pipe\<user-scope>\ContextIMENamedPipe` 和 `\\.\pipe\ContextIME.ContextService.v1` 同时存在。

安装后的 Context Service 文件：

```text
file: C:\Program Files\ContextIME\contextime-0.3.0-preview\contextime-context-service.exe
bytes: 318,976
SHA-256: 50e1e2fbe672267abc25c41cb403194b7d5eb0c58b3d4b5fe18a5e2cb6fd6b24
```

该哈希与 CI artifact 完全一致。

PowerShell `Start-Process -Wait` 会等待安装器派生的常驻 Server/Context Service 进程树，不能作为该安装器的唯一完成条件。本轮没有为此修改产品运行时；安装完成由版本注册表、安装目录、文件哈希、进程和 pipe 的一致收敛共同确认。

## Singleton 与停止/重启

已安装服务初始 PID 为 `17588`。重复启动第二实例 PID `26708`：

```text
duplicate exit code: 0
service count before: 1
service count after: 1
original PID retained: true
pipe retained: true
```

第一次 `--quit` PID `26704`：

```text
quit exit code: 0
service process count after: 0
Context Service pipe after: absent
```

服务已经停止时再次执行 `--quit`，PID `21020` 仍以 `0` 退出，确认控制命令幂等。随后重新启动为 PID `21424`，单实例计数恢复为 `1`，Context Service pipe 恢复存在。

## 服务运行时输入 smoke

在 Context Service 运行且 pipe 存在时，复用原有 TSF smoke v6 和固定 M2 混输 fixture：

```text
fixture: native/tests/tsf-smoke/fixtures/m2-mixed-input.json
committed: 你好GameObjectAssets/Textures/UIhttps://github.comgit status && npm test输入法
final text: 你好GameObjectAssets/Textures/UIhttps://github.comgit status && npm test输入法abc
sequence matched: true
candidate detected on all 6 steps: true
foreground retained on all 6 steps: true
passed: true
```

本机原始 JSON：

```text
artifacts/native-evidence/20260901T105829Z/contextime-0.3.0-service-running-mixed-input.json
bytes: 7,812
SHA-256: 1af7ae2e4213f6ae3ccd08467811115f7edf50552b8a159474bf7d8bae25ad72
```

## 服务停止时输入回退

确认 Context Service 进程和 pipe 均不存在后，原样重跑相同 TSF smoke：

```text
committed: 你好GameObjectAssets/Textures/UIhttps://github.comgit status && npm test输入法
final text: 你好GameObjectAssets/Textures/UIhttps://github.comgit status && npm test输入法abc
sequence matched: true
candidate detected on all 6 steps: true
foreground retained on all 6 steps: true
service remained stopped through smoke: true
passed: true
```

本机原始 JSON：

```text
artifacts/native-evidence/20260901T105917Z/contextime-0.3.0-service-stopped-fallback-mixed-input.json
bytes: 7,866
SHA-256: 0d0435d20d96c5cd3e0671f9d988fac76566f31e36a9677c4082d16c73d495f5
```

该结果证明本次安装包在真实 TSF 桌面中满足“Context Service 缺失时普通 Weasel/librime 输入继续工作”。它不证明模拟的 pipe 卡住、协议损坏或进程崩溃场景；这些仍由固定 Windows runner fixture 覆盖。

## 验证边界

| 能力 | 状态 | 证据边界 |
|---|---|---|
| `0.2.3 → 0.3.0` 覆盖升级 | `real_machine_verified` | 版本、目录、文件哈希、注册表、进程和 pipe |
| 安装后 Context Service 启动 | `real_machine_verified` | 交互 Session 1 进程和 Named Pipe |
| singleton / `--quit` / 幂等 quit / restart | `real_machine_verified` | 已安装 production executable |
| 服务运行时普通 TSF 输入 | `real_machine_verified` | TSF smoke v6 六步 fixture |
| 服务停止时普通 TSF 输入回退 | `real_machine_verified` | 同一 fixture，服务和 pipe 全程缺失 |
| 官方小狼毫并存 | `real_machine_verified` | 两套 Server 和独立 IPC 同时存在 |
| 首次安装 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮是覆盖升级 |
| 登录后 autorun | `REAL_WINDOWS_VERIFICATION_REQUIRED` | registry 已写入，尚未重新登录 |
| 卸载停止与清理 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚未执行卸载 |
| Terminal 自动切换 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前 smoke 不表达自动模式期望 |
| composition/candidate/manual 保护 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚无交互桌面行为证据 |

## 过度工程判断

当前普通 companion process 已满足安装、单实例、停止、重启和故障回退边界。现阶段不需要 Windows SCM、supervisor、恢复管理器或新的后台 UI。下一项应是复用现有 TSF/应用探针做最小 Terminal 自动切换与保护行为验收，而不是扩展服务生命周期架构。
