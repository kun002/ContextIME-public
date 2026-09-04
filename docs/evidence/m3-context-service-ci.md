# M3.2 Context Service CI 证据

## 结果

ContextIME 已建立独立 C++17 Context Service executable、版本化本地 Named Pipe v1 协议和客户端故障 `KEEP` 边界。最终 PR head 在 Windows/Ubuntu 严格编译，并在 Windows runner 使用真实 Named Pipe 完成集成测试：

```text
Context Protocol: 46 assertions passed
Context Service Windows: 42 assertions passed
Context Service executable built
```

该证据证明 M3.2 独立模块已经 `implemented`、`built` 和 `statically_verified`，也证明固定 CI Windows runner 上的 IPC 集成 `runtime_verified`。它不证明服务已安装或接入真实 TSF/IME，不属于用户交互桌面的 `real_machine_verified`。

## 固定构建身份

- Source commit：`5909089a34d0e354df33930c71b6c43e0bfeba2e`
- PR：`#19 Add versioned Context Service IPC baseline`
- Pull request workflow run：`33466662527`
- Workflow：`Context Service`
- Windows job：`99727828780`
- Ubuntu job：`99727828662`
- Windows runner：`windows-2022`
- Ubuntu runner：`ubuntu-24.04`

Run URL：

```text
https://github.com/kun002/ContextIME/actions/runs/33466662527
```

## 双平台门禁

Windows：

```text
MSVC /std:c++17 /EHsc /W4 /WX
Context Protocol: 46 assertions passed
Context Service Windows: 42 assertions passed
contextime-context-service.exe built
job conclusion: success
```

Ubuntu：

```text
g++ -std=c++17 -Wall -Wextra -Werror -pedantic
Context Protocol: 46 assertions passed
job conclusion: success
```

Ubuntu 只编译平台无关 protocol codec 和 Context Engine。Windows 另外编译 client/server/entrypoint，并运行 Win32 Named Pipe integration。

## 已验证协议语义

46 项跨平台断言覆盖：

- 固定 `CIME` magic、protocol version `1`、message type、payload size 和 request id；
- 48-byte request、32-byte response 和显式 little-endian 编解码；
- current mode、composition/candidate、automation/context available flags；
- lock、manual deadline 和 user/project/syntax/surface/application 规则 round trip；
- response status、decision、source 和 switch flag round trip；
- 拒绝错误 magic、未知版本、错误 message type/size；
- 拒绝非法 enum、未知 flag、非法 boolean 和非零 reserved bytes；
- 固定 response ACK `0x06` 和稳定诊断字符串。

## 已验证 Windows IPC 语义

42 项 Windows 断言使用唯一测试 pipe 并覆盖：

- Context Service endpoint 与输入引擎 `ContextIMENamedPipe` 隔离；
- 服务端用当前用户 + `SYSTEM` DACL 创建 pipe；
- 普通 syntax decision 经 pipe 返回 `ENGLISH`；
- composition active 经 pipe 后仍优先返回 `KEEP`；
- pipe 不存在时立即 `KEEP / CONTEXT_UNAVAILABLE`；
- 服务接受连接但 250 ms 不响应时，25 ms client deadline 返回 timeout，未等待假服务恢复；
- 服务接受后断开时返回 `KEEP / CONTEXT_UNAVAILABLE`；
- Engine 报告 context unavailable 时 transport 保持正常且 decision 为 `KEEP`；
- response 全部读取后以 ACK 确认，服务端才在有限 deadline 内断开。

收口过程中，双触发 runner 曾暴露服务端写完 response 后立即 `DisconnectNamedPipe` 的竞态：一套通过、一套在最后 fixture 得到 fallback。最终提交加入有限 deadline 内的 1-byte ACK，push run `33466660377` 与本 PR run `33466662527` 的 Windows/Ubuntu 服务门禁随后同时通过。没有通过放宽断言隐藏竞态。

## 热路径、安全与隐私边界

- endpoint：`\\.\pipe\ContextIME.ContextService.v1`；
- `PIPE_REJECT_REMOTE_CLIENTS` 拒绝网络客户端；
- pipe DACL 只包含当前用户和 `SYSTEM`；
- client 的 connect/write/read 共用一个总 deadline；
- 任一 transport/protocol failure 统一返回当前状态的 `KEEP / CONTEXT_UNAVAILABLE`；
- request/response 没有输入正文、源码、密码、Token、API Key 或环境变量字段；
- 同步 client 只允许未来后台 refresh worker 使用，不能由 TSF key path 直接调用。

runner 没有建立其他本地用户或网络客户端，因此 DACL 的拒绝行为和 `PIPE_REJECT_REMOTE_CLIENTS` 当前是代码/配置 `statically_verified`，不是跨用户/跨机器攻击测试完成。

## 验证状态

| 项目 | 状态 | 证据边界 |
|---|---|---|
| protocol v1 codec | `implemented` / `built` / `statically_verified` | 双编译器，46 assertions |
| Windows Named Pipe client/server | `implemented` / `built` / `runtime_verified` | runner 本地 pipe，42 assertions |
| Context Service executable | `implemented` / `built` | MSVC `/W4 /WX` |
| missing/disconnect/timeout fallback | `runtime_verified` | Windows runner transport fixtures |
| 0.2.3 TSF/librime 输入行为 | `unchanged` | 未修改 `native/`、schema 或 installer |
| Context Service 安装/开机生命周期 | `not_implemented` | 后续集成阶段 |
| IME background cache/state applier | `not_implemented` | 不允许直接在 key path 等待 IPC |
| 前台应用/输入区域识别 | `not_implemented` | M3 后续 |
| 真机自动上下文切换 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚无 TSF consumer |
| VS Code Adapter | `not_started` | M4，不在本 PR 范围 |

## M3.2 结论与下一项

M3.2 关闭独立服务 executable、协议 v1 和 transport 故障回退模块。下一项先实现前台应用/输入区域 Context Source 与非阻塞 decision cache 边界；在真实 IME state applier 集成前，仍不能把 Context Service 加入安装器或让 TSF 按键路径同步等待它。
