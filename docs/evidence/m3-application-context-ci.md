# M3.4 前台应用 Context Source CI 证据

## 结果

ContextIME 已建立后台 Windows foreground application source、隐私收缩后的 application identity 和保守 terminal surface 规则。固定 Windows/Ubuntu runner 严格编译并通过：

```text
Application Context: 31 assertions passed
```

Windows runner 还把真实 Win32 capture 接入 Context Service integration，原 42 项 pipe 断言增加 5 项固定 capture 边界断言：

```text
Context Service Windows: 47 assertions passed
```

该结果证明 application normalization/classification 已 `implemented`、`built` 和 `statically_verified`，并证明 Windows API capture 可以在 runner 进程中调用且保持固定隐私/buffer 边界。runner 可能没有用户交互 foreground window，因此不证明某个真实应用已被正确识别，也不属于桌面 `real_machine_verified`。

## 固定构建身份

- Source commit：`7cca452e42b50e30aab23998c715ca705114a08d`
- PR：`#21 Add conservative foreground application context`
- Pull request workflow run：`33468372256`
- Workflow：`Context Service`
- Windows job：`99732857358`
- Ubuntu job：`99732857185`
- Windows runner：`windows-2022`
- Ubuntu runner：`ubuntu-24.04`

Run URL：

```text
https://github.com/kun002/ContextIME/actions/runs/33468372256
```

## 双编译器门禁

Windows：

```text
MSVC /std:c++17 /EHsc /W4 /WX
Context Protocol: 46 assertions passed
Decision Cache: 65 assertions passed
Application Context: 31 assertions passed
Context Service Windows: 47 assertions passed
Context Service executable built with Advapi32 + User32
job conclusion: success
```

Ubuntu：

```text
g++ -std=c++17 -Wall -Wextra -Werror -pedantic
Context Protocol: 46 assertions passed
Decision Cache: 65 assertions passed
Application Context: 31 assertions passed
job conclusion: success
```

Ubuntu 只编译平台无关 identity normalization/classification。Windows 另外编译 `foreground_application_win.cpp`，链接进真实 Context Service executable 和 pipe integration test。

## 已验证 application 语义

31 项跨平台断言覆盖：

- Windows `\\` 路径和 `/` 路径只保留 lowercase executable basename；
- 返回 identity 不包含任何路径分隔符；
- window class 归一化；
- Windows Terminal executable/class 识别为 terminal application/surface；
- terminal 只填缺失的 `surface_context/application_default = ENGLISH`；
- 调用方已有 surface/application rule 不被覆盖；
- VS Code 分类为 editor，但 surface 保持 `UNKNOWN` 且不建议 mode；
- Edge 分类为 browser，但 input surface 保持 `UNKNOWN`；
- Notepad 分类为 document editor，但 surface 保持 `UNKNOWN`；
- process image 不可访问时允许可靠 console window class fallback；
- zero PID/thread、空 identity 回退 unavailable；
- oversized basename 被丢弃而不是截断；
- application/surface 诊断字符串稳定；
- normalization/apply 保持 `noexcept`。

Windows 5 项固定 capture 断言无论 runner 是否有 foreground window 都验证：

- available capture 必须有非零 PID；
- executable 结果不得包含完整路径；
- executable/window-class 固定 buffer 必须 NUL 终止；
- available capture 必须至少有一个归一化 identity。

## 隐私与热路径审计

Windows source 调用：

```text
GetForegroundWindow
GetWindowThreadProcessId
GetClassNameW
OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)
QueryFullProcessImageNameW
```

没有调用 `GetWindowText`、UI Automation text、clipboard 或文档 API。完整 process image path 只存在局部固定 buffer，返回前裁剪为 basename；struct 中不存在 window title/full path/text 字段。

capture 只在 Context Service request evaluation 中执行。Decision Cache reader 没有新增 Windows API、IPC、clock 或锁。服务/source 不可用时不填规则，由原 `KEEP` 路径处理。

## 收口中发现的既有门禁问题

首次 PR 双触发中，一套 Ubuntu cache regression 合法地在 writer 持续更新期间全部返回 `KEEP`，但旧测试错误要求并发期间至少读取一次稳定值。cache contract 本来就规定竞争时立即 `KEEP`。最终提交只修正测试：并发期验证无 torn decision，writer 完成后验证最终值可见；未放宽 torn-decision 断言，也未修改 cache 实现。修正后 push/PR 的 Windows/Ubuntu 门禁全部通过。

## 验证状态

| 项目 | 状态 | 证据边界 |
|---|---|---|
| application normalization/classification | `implemented` / `built` / `statically_verified` | 双平台 31 assertions |
| Windows foreground capture API | `implemented` / `built` / `runtime_verified` | runner 调用与 5 项边界断言 |
| terminal surface English default | `statically_verified` | 不覆盖 caller rule |
| editor/browser/document surface | `KEEP / UNKNOWN` | 明确不猜 |
| Context Service executable integration | `built` / `runtime_verified` | Windows pipe regression 47 assertions |
| generic input-area recognition | `not_implemented` | Adapter/UI source 后续 |
| 0.2.3 TSF/librime 输入行为 | `unchanged` | 未修改 `native/`、schema 或 installer |
| 用户交互桌面 foreground identity | `REAL_WINDOWS_VERIFICATION_REQUIRED` | CI 可能无 foreground window |
| 真机自动状态应用 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 无 TSF consumer |

## M3.4 结论与下一项

M3.4 关闭前台应用 identity 和独立终端 surface baseline。下一项 generic input-area/source 不能靠 top-level app 猜；在 M4 Adapter 之前，只能接入可靠 TSF/Windows surface 信号，否则保持 `UNKNOWN/KEEP`。
