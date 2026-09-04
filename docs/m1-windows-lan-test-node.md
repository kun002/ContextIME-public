# M1 Windows LAN Test Node

## 产品结果与边界

Windows LAN Test Node 把真实 UI 输入验收移到一台独立的局域网 Windows 笔记本：主开发机负责生成带哈希的测试包、同步、触发和回收证据；笔记本只在指定用户已经登录且桌面解锁时，通过计划任务运行 ContextIME 的真实 TSF 输入测试。

该能力属于 `native IME verification`。它不修改 ContextIME、Weasel 或 librime 的产品运行时，也不允许 WinRM、SSH 或后台服务的非交互 Session 直接发送 UI 输入。

第一阶段只支持：

```text
Notepad
→ shurufa
→ 候选与“输入法”上屏
→ Shift 英文 abc
→ 恢复中文
→ 循环与 Server/IPC 资源审计
```

Edge、VS Code、Visual Studio 和 Terminal 在 LAN Notepad 结果与本机结果一致后再迁移。

## 控制与执行边界

```text
主开发机
  ├─ 生成 ZIP + manifest + SHA-256
  ├─ SMB 同步到 packages/
  ├─ 原子发布 requests/<runId>.request.json
  └─ schtasks /Run /S <node>
                    │ 只触发，不执行 UI
                    ▼
笔记本 Task Scheduler
  └─ LogonType = InteractiveToken
       └─ ContextIMELanNodeWorker.ps1
            ├─ WTSActive / SessionId / Explorer / unlocked input desktop 门禁
            ├─ 校验 ZIP SHA-256 与内部逐文件 manifest
            ├─ 调用现有 run-contextime-stability.ps1
            ├─ 原子发布 result.json
            └─ 保存日志、候选 PNG、逐轮 JSON、前后资源快照
                    │
                    ▼
主开发机回收并复核 artifacts-manifest.json
```

计划任务没有定时 Trigger，只响应显式远程 `/Run`。任务设置为 `InteractiveToken` 和 `RunLevel Limited`；用户未登录时无法获得该 token，桌面锁定、Session 非 Active、Explorer 不在同一 Session、输入桌面不可切换或前台窗口属于其他 Session 时，worker 在发送任何测试按键前拒绝运行。主机每次触发前都会读取远端任务 XML，确认 `InteractiveToken`、`LeastPrivilege`（Windows 可省略该节点，省略即 Task Scheduler schema 的最小权限默认值），并要求 PowerShell action 与安装时记录的 worker、节点根目录和完整参数逐字一致。

## 文件与协议

节点默认安装目录：

```text
%ProgramData%\ContextIME\LanTestNode\
├─ worker\ContextIMELanNodeWorker.ps1
├─ work\<runId>\
└─ exchange\                         # SMB: ContextIMELanTest$
   ├─ node-info.json
   ├─ node-status.json
   ├─ packages\<packageId>.zip
   ├─ requests\<runId>.request.json
   ├─ requests\<runId>.cancel
   ├─ requests\archive\
   └─ runs\<runId>\
      ├─ result.json
      ├─ artifacts-manifest.json
      ├─ interactive-session.json
      ├─ resource-before.json
      ├─ resource-after.json
      ├─ test.stdout.log
      ├─ test.stderr.log
      ├─ contextime-stability.json
      └─ contextime-stability.json.iterations\*.json / *.png / *.trace.log
```

协议版本：

- 包：`contextime.lan-package.v1`；
- 请求：`contextime.lan-request.v1`；
- 结果：`contextime.lan-result.v1`；
- 证据清单：`contextime.lan-artifacts.v1`。

包和请求都先写临时文件，再在同一目录改名发布。worker 先把 ZIP 复制到仅测试账号可写的本地工作目录，再校验外层 SHA-256、package id、固定 commit、唯一入口、ZIP 路径边界，以及 manifest 中固定文件集合的长度和 SHA-256；打包运行时由 worker 显式把 commit 传给 stability harness，不依赖测试包中不存在的 `.git`。主机回收后再次校验结果身份、全部证据文件及清单覆盖范围。

## 笔记本一次性安装

前提：

- 笔记本已安装待测 ContextIME；
- 用将来实际执行测试的 Windows 用户登录笔记本；
- 该用户桌面保持解锁；
- 管理员允许“文件和打印机共享”和“远程计划任务管理”入站规则；
- 主机当前凭据可以访问节点 SMB 和远程计划任务。脚本不接收、记录或输出密码。

把 `native/lan-test-node/` 复制到笔记本，在笔记本的提升权限 Windows PowerShell 中运行：

```powershell
Set-Location <copied-native-lan-test-node>
./install-contextime-lan-test-node.ps1 `
  -RunAsAccount 'LAPTOP\ime-test' `
  -OperatorAccount 'LAPTOP\ime-test'
```

工作组环境通常在笔记本创建专用本地账号，主机使用同名凭据访问 SMB/RPC；域环境可把 `OperatorAccount` 设为域账号。不要把密码写入仓库、请求 JSON 或命令行。安装器不自动开启防火墙规则，也不启用 WinRM/SSH。

安装器给运行账号授予 `work\` 的 `Modify` 和 `worker\` 的 `ReadAndExecute`，给运行账号与操作账号授予 `exchange\` 的 `Modify`；它不会给运行任务管理员权限。

确认任务身份：

```powershell
$task = Get-ScheduledTask -TaskPath '\ContextIME\' -TaskName 'LAN Test Node'
$task.Principal | Format-List UserId,LogonType,RunLevel
```

必须看到 `LogonType` 为 `Interactive`/`InteractiveToken`，`RunLevel` 为 `Limited`。

## 主开发机生成与触发

提交测试 harness 后生成正式包；默认拒绝相关文件未提交：

```powershell
./native/scripts/new-contextime-lan-test-package.ps1
```

开发调试包可显式使用 `-AllowDirtyHarness`，但这种包不能成为验收证据。

远程触发 3 分钟对照：

```powershell
./native/scripts/invoke-contextime-lan-test.ps1 `
  -NodeName 'LAPTOP' `
  -PackagePath './artifacts/lan-test/packages/<packageId>.zip' `
  -DurationSeconds 180
```

正式 30 分钟：

```powershell
./native/scripts/invoke-contextime-lan-test.ps1 `
  -NodeName 'LAPTOP' `
  -PackagePath './artifacts/lan-test/packages/<packageId>.zip' `
  -DurationMinutes 30
```

同一节点一次只接受一个活动请求。若 `requests\` 中已有 `.request.json` 或 `.running.json`，控制器会在同步新请求前停止，避免 `MultipleInstances = IgnoreNew` 让第二个 UI 测试滞留。

控制器默认把证据回收到：

```text
artifacts/native-evidence/lan-node/<runId>/
```

运行期间不要操作笔记本的物理键盘鼠标。远程 `/Run` 只负责启动已经注册的交互计划任务；即使改用 WinRM/SSH，也只能远程调用 `Start-ScheduledTask`，禁止在那个非交互远程 Session 中直接执行 stability/app-smoke 脚本。

## 超时与取消

节点测试超时为目标时长加控制面余量。控制器超时后原子写入 `requests/<runId>.cancel`；stability harness 只在一轮真实 smoke 完整清理后、下一轮开始前响应取消。若取消宽限期仍无结果，控制器可调用远程 `schtasks /End`，并回收所有已有部分证据和 `controller-timeout.json`。

超时、取消、锁屏拒绝、包校验失败和测试失败都必须产生 `passed: false`；不得把部分 JSON、旧截图或任务的 `LastTaskResult = 0` 当作测试通过。

## 本机与 LAN 对照门禁

第一阶段对照至少检查：

- `repositoryCommit` 与 package manifest 一致；
- `elapsedSeconds` 达到目标；
- 每轮原有 20 个 app smoke 字段均为 `true`；
- 每轮 `applicationProcessExited = true`；
- `输入法 → 输入法abc` 精确匹配；
- ContextIME Server PID 与启动时间不变；
- `ContextIMENamedPipe` 每轮存在；
- `failures`、子测试 `Errors` 和节点 `errors` 为空；
- 首轮、中间轮、末轮候选 PNG 只包含隔离 Notepad；
- Server 私有内存和句柄趋势与本机结果解释一致。

只有实际笔记本结果通过后才能标记 `real_machine_verified`。脚本解析、开发包生成、SMB 同步或计划任务注册只能分别标记 `implemented`、`built` 或 `statically_verified`。

机械门禁通过后，人工抽查本机与笔记本的首轮、中间轮、末轮候选截图，并解释两边 Server 私有内存与句柄趋势，再生成对照结果：

```powershell
./native/scripts/compare-contextime-lan-notepad-result.ps1 `
  -LocalStabilityPath './artifacts/native-evidence/<local>/contextime-stability.json' `
  -LanRunDirectory './artifacts/native-evidence/lan-node/<runId>' `
  -ScreenshotsReviewed `
  -ResourceTrendReviewed
```

未提供两个人工复核开关时，对照器仍会写出机械审计 JSON，但 `passed` 保持 `false`，不能作为一致性验收证据。

## 卸载与回滚

默认卸载只移除计划任务和 SMB 共享，保留节点证据：

```powershell
./uninstall-contextime-lan-test-node.ps1
```

确认不再需要历史包和证据时才显式删除节点目录：

```powershell
./uninstall-contextime-lan-test-node.ps1 -RemoveNodeData
```

卸载脚本只接受 `%ProgramData%\ContextIME\LanTestNode` 这一专用目录形状，拒绝对 `%ProgramData%` 或其他宽路径做递归删除；它不卸载 ContextIME，也不触碰 Weasel 或 `%APPDATA%\Rime`。

## 当前验证状态

- 脚本和协议：`implemented`；
- 本机开发包生成与逐文件哈希：`statically_verified`；
- 空队列 worker 状态发布：`statically_verified`；
- 独立 LAN 笔记本任务注册、锁屏拒绝、远程 Notepad 输入与证据回传：`REAL_WINDOWS_VERIFICATION_REQUIRED`。
