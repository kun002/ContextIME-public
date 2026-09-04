# M1 30 分钟稳定性与宿主机基线验证证据

## 产品结果

本轮关闭 ContextIME Native IME Baseline 在主开发机上的最后一项既定门禁：在真实 Windows 交互桌面和真实 TSF 输入链路中，连续运行隔离 Notepad 输入循环超过 30 分钟，并审计 ContextIME Server 的进程、IPC、私有内存和句柄。

影响链路：

```text
System key event
→ Notepad TSF client
→ ContextIME IPC
→ ContextIME Server
→ librime composition / candidates
→ Notepad text commit
```

没有修改 ContextIME、Weasel 或 librime 产品运行时来迁就测试。稳定性 harness 反复启动独立 Notepad fixture，每轮完成候选显示、中文上屏、英文直输、恢复中文和进程清理。

## 固定运行身份

- ContextIME 安装版本：`0.1.3-preview`
- ContextIME runtime build commit：`aa80d79a659511c0a00f534011bb39698ec84e9c`
- Weasel `0.17.4`：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime `1.13.1`：`1c23358157934bd6e6d6981f0c0164f05393b497`
- 稳定性 harness commit：`a73273e473b4b43437ee9bd57554f3bcb95bf544`
- harness 脚本 SHA-256：`8f4a674d829108484ac896577bed634b20294c5b8a83c001b7a4fcc515771668`
- `WeaselServer.exe` SHA-256：`c81fb351bda84efe0a247dce571c4915dd3919c4891cdce9573e4b8fb475797e`
- Windows：Windows 10 Pro，DisplayVersion `25H2`，Build `26200.9168`

测试发生在已有开发历史的 Windows x64 主开发机，不是干净 VM，也不是独立 LAN 笔记本。

## 测试方法与门禁

`native/scripts/run-contextime-stability.ps1` 每轮调用现有 Notepad app smoke，不直接调用 librime API。正式输入序列为：

```text
shurufa
→ 候选首项“输入法”
→ Space
→ 输入法
→ Shift
→ 输入法abc
→ Shift 恢复中文
```

每轮必须同时满足：

- app smoke 的 20 个 Profile、前台、焦点、候选、上屏、状态恢复和清理字段均为 `true`；
- `CommittedText` 精确等于 `输入法`，`FinalText` 精确等于 `输入法abc`；
- 候选 PNG 已生成，且候选区域发生可测像素变化；
- 该轮 Notepad PID 已退出；
- ContextIME Server PID 和启动时间不变，进程响应正常，`ContextIMENamedPipe` 存在；
- `failedFields`、app `Errors`、子进程错误和总 `failures` 均为空。

测试开始前若检测到任何 Notepad 进程，harness 会拒绝运行；每轮只清理自己创建并核对过 PID 的隔离 Notepad，不终止无关用户进程。

## 正式结果

正式证据目录：`artifacts/native-evidence/m1-local-30-minute-stability-a73273e/`。该目录按仓库规则被忽略，不进入 Git；以下摘要和哈希进入版本控制。

| 指标 | 结果 |
|---|---:|
| 目标时长 | `1800` 秒 |
| 实际时长 | `1809.022` 秒 |
| 运行区间（UTC） | `2026-08-30T15:00:58.6591010Z` – `2026-08-30T15:31:07.6996571Z` |
| 成功轮次 | `147 / 147` |
| 逐轮 JSON | `147` |
| 候选 PNG | `147` |
| trace log | `147` |
| 候选变化像素范围 | `51,787` – `51,811` |
| ContextIME Server PID | 全程 `29476` |
| Server 私有内存增量 | `+69,632` bytes |
| Server 句柄增量 | `0` |
| 总 failures | `0` |
| 最终结果 | `passed: true` |

机械复核确认：147 轮全部通过 20 项 app smoke 门禁，全部生成候选、提交 `输入法`、得到 `输入法abc`、恢复中文、关闭该轮 Notepad，并保持同一个响应正常的 Server 和 Named Pipe。正式测试结束后再次检查，系统中没有残留 Notepad 进程。

## 证据哈希与截图复核

- 汇总 JSON：`contextime-stability.json`
- 汇总 JSON SHA-256：`9e84fc2577884e99100c9133e8403df17799292a440b0892d453ce34662967a9`
- 逐轮目录文件清单：147 JSON + 147 PNG + 147 log
- 按文件名排序、使用 `SHA256  filename\n` UTF-8 生成的清单 SHA-256：`950020ac6419a3d0acd9211f39d9f216ac28481e94de15cbe9378ccf3a0e96d9`

人工抽查第 1、74、147 轮候选 PNG：三张均显示 `shu ru fa` composition、简体首候选“输入法”和 1–5 中文候选，没有其他应用或用户窗口内容。

## M1 主开发机基线结论

既定宿主机门禁现已全部有真实 Windows 证据：

| 门禁 | 状态 | 证据 |
|---|---|---|
| 安装、TSF 注册、组合、候选、上屏、中英文、升级/卸载/重装 | `real_machine_verified` | `m1-native-preview-0.1.3-host-verification.md` |
| 与官方小狼毫 0.17.4 并存和双向卸载隔离 | `real_machine_verified` | `m1-weasel-coexistence-host-verification.md` |
| 数字选词、候选翻页、翻页后选词 | `real_machine_verified` | `m1-number-selection-and-paging-host-verification.md` |
| Notepad、Edge、VS Code、Visual Studio、Terminal | `real_machine_verified` | `m1-five-application-host-verification.md` |
| 连续 30 分钟输入与资源审计 | `real_machine_verified` | 本文 |

因此可记录：

```text
M1_HOST_BASELINE = VERIFIED
```

该状态只表示当前主开发机上的 M1 宿主机基线通过，不等于跨机器、干净系统、LAN 节点、RDP 或正式 Release Gate 已通过。

## 仍未验证

| 能力 | 状态 | 边界 |
|---|---|---|
| 独立 LAN 笔记本交互节点 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 未完成远程触发与本机结果一致性验收；后续除非用户明确要求，不连接或触发该笔记本 |
| 干净 Windows 10/11 安装矩阵 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前证据来自已有开发历史的 Build 26200.9168 主机 |
| RDP 输入稳定性 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未执行 |
| 用户词频和自定义用户词典行为 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 卸载保留 `luna_pinyin.userdb` 已验证，学习行为本身尚未验收 |

## 本地复验

运行期间不要操作物理键盘和鼠标。使用新的证据路径，避免覆盖既有结果：

```powershell
./native/scripts/run-contextime-stability.ps1 `
  -DurationMinutes 30 `
  -EvidencePath './artifacts/native-evidence/<new-run>/contextime-stability.json'
```

## 下一项

按用户确认的执行顺序，下一项是 M2.1 技术英文片段。进入 M2 只基于 `M1_HOST_BASELINE = VERIFIED`；上述跨机器和 Release 边界继续保留，不能随 M2 开发被误报为已验证。
