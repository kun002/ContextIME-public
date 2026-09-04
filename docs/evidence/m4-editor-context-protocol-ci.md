# M4.1 编辑器上下文协议 CI 证据

## 结果

ContextIME 已在现有 `\\.\pipe\ContextIME.ContextService.v1` 上建立 report-only editor context request/response、service-owned 2 秒 TTL store、真实 VS Code foreground 门禁和保守 Context Engine 映射。

固定 Windows/Ubuntu Context Service runner 全部通过：

```text
Context Protocol: 75 assertions passed
Decision Cache: 73 assertions passed
Application Context: 31 assertions passed
Editor Context: 21 assertions passed
Context Service Windows: 67 assertions passed
Context Service lifecycle passed: singleton, quit, restart
```

固定 IME Host runner 继续通过：

```text
IME State Applier: 11 assertions passed
Context Refresh Worker Windows: 31 assertions passed
```

固定 Weasel 0.17.4 TSF x64/Win32 完整构建和 `0.3.1-preview` 原生安装器构建/manifest/隔离审计也通过。该结果证明 M4.1 native receiver foundation 已 `implemented`、`built` 和 runner `runtime_verified`，并证明没有破坏既有 TSF consumer 的编译边界。它不证明未来 VS Code Adapter 已实现，也不是真实交互桌面验收。

## 固定构建身份

- Source commit：`a8f3b16599254d758cbc02b624df4a812c9f8e8f`
- PR：`#30 M4.1: add report-only editor context protocol`
- Context Service PR run：`33628881889`
  - Windows job：`100243128873`
  - Ubuntu job：`100243128581`
- IME Host Context Bridge PR run：`33628881837`
  - Windows job：`100243128328`
  - Ubuntu job：`100243128561`
- M3 Weasel TSF Context Bridge run：`33628881821`
  - job：`100243128391`
- ContextIME Native Preview 0.3.1 run：`33628882358`
  - job：`100243130637`
- General CI run：`33628881822`

Run URLs：

```text
https://github.com/kun002/ContextIME/actions/runs/33628881889
https://github.com/kun002/ContextIME/actions/runs/33628881837
https://github.com/kun002/ContextIME/actions/runs/33628881821
https://github.com/kun002/ContextIME/actions/runs/33628882358
https://github.com/kun002/ContextIME/actions/runs/33628881822
```

## 已验证协议与 store 语义

双平台严格编译使用 MSVC `/std:c++17 /W4 /WX` 与 g++ `-std=c++17 -Wall -Wextra -Werror -pedantic`。门禁覆盖：

- evaluate 旧消息保持 48-byte request / 32-byte response；
- editor context 使用新 message type `3/4`，仍保持相同固定 wire size；
- header、版本、type、payload length、enum、flag、boolean 和 reserved bytes 严格校验；
- language ID 最长 20 bytes，只允许 normalized lowercase ASCII 子集；
- malformed editor frame 返回 type `4`、回显 request ID、`MALFORMED_REQUEST / accepted=false`；
- service 无 store 时返回有界 `INTERNAL_ERROR / accepted=false`；
- code/Markdown code → syntax English；
- comment/Markdown text → syntax Chinese；
- string/unknown → 不提供建议；
- integrated terminal → surface English；
- caller 已有 syntax/surface rule 不被覆盖；
- 2 秒 TTL 在边界失效，zero TTL 直接 invalidate；
- unfocused、其他 editor 和非 VS Code foreground 不提供建议；
- service 使用自己的 `GetTickCount64()`，不信任 adapter 时间；
- editor update 能经真实本地 Named Pipe 到达 shared store；
- DACL、`PIPE_REJECT_REMOTE_CLIENTS`、总 deadline、response ACK 和 singleton/lifecycle 回归继续通过。

## 隐私与输入链路边界

request 只包含 focused、surface、syntax 和 normalized language ID。协议和 store 不含 source/selection text、document URI、project path、token、secret 或环境变量字段，没有网络行为。

本 PR 未修改：

- librime、schema、candidate、composition 或 commit；
- TSF key-event path；
- Context Engine 固定优先级和 composition/manual override protection；
- 已安装主开发机上的 `0.3.1-preview`；
- 旧 VS Code controller prototype 的运行行为。

Weasel workflow 把更新后的 client protocol/header 复制到固定 patch-stack，重新构建 x64 和 Win32 TSF，证明新增 editor 消息没有把 store implementation 或服务端逻辑链接进 TSF consumer。

Native Preview workflow 生成的同版本 artifact 只作为构建/manifest 回归证据。它不是新的用户 release，不能覆盖发布 `0.3.1-preview`，也没有被安装到主开发机。

## 收口中发现并修复的问题

首次 Windows Context Service run 在新增测试 helper 处拒绝把 const request buffer 传给可写参数。修复将 helper 分为 `WriteAll(const uint8_t*)` 和 `ReadAll(uint8_t*)`；产品代码未改。修复后 Windows 67 项全部通过。

首次完整 Weasel build 暴露 `context_service.h` 把 service-owned store 类型泄漏给 TSF client。修复改为前置声明 store，让 service main/server 显式依赖完整类型，并只在固定 patch-stack 复制 protocol codec 所需的轻量 editor header。修复后固定 Weasel x64/Win32 完整构建通过。

## 验证状态

| 项目 | 状态 | 证据边界 |
|---|---|---|
| editor protocol codec | `implemented / built / statically_verified` | 双平台 75 protocol assertions |
| editor TTL store/mapping | `implemented / built / statically_verified` | 双平台 21 assertions |
| Windows editor Named Pipe branch | `implemented / built / runtime_verified` | Windows service 67 assertions |
| service executable/lifecycle | `built / runtime_verified` | singleton、quit、restart |
| IME worker/client regression | `built / statically_verified` | Windows 31 + 双平台 applier 11 |
| fixed Weasel TSF consumer | `built / statically_verified` | x64 + Win32 |
| native installer regression | `built / statically_verified` | compile-only artifact；不发布 |
| report-only VS Code Adapter | `not_implemented` | 下一 PR |
| installed VS Code → service → Context Engine | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 需要新版与交互桌面 |
| code/comment/terminal 自动状态应用 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | CI 不能证明真实焦点/TSF 行为 |

## M4.1 结论与下一项

M4.1 原生协议/store foundation 可以收口。下一项只能把旧 VS Code controller prototype 替换成轻量上报器和 Node Named Pipe client；不得保留 `PolicyEngine`、`HabitLearner`、Windows mode polling 或 `switchTo()`，也不得把 Adapter 变成第二个决策 owner。
