# M4.2 VS Code Report-Only Adapter 验证证据

## 结果

旧 `packages/vscode-adapter` controller 已收缩为可选的 report-only Adapter。Adapter 只采集和上报归一化 VS Code 上下文，不再读取、轮询或切换 Windows 输入状态；Context Engine 保持唯一 decision owner。

当前本地门禁：

```text
npm run typecheck: passed
npm test: 43 / 43 passed
Adapter tests: 12 / 12 passed
source capability audit: 0 forbidden matches
npm run package:vsix: passed
git diff --check: passed（仅 Git 换行提示）
```

PR 双平台门禁和 Windows VSIX job 已通过，因此 M4.2 标记为 `implemented / built / statically_verified`。真实 VS Code → installed Context Service → Context Engine → TSF 链路保持 `REAL_WINDOWS_VERIFICATION_REQUIRED`。

## 固定构建身份

- Implementation commit：`3881d2fd5348a6cc5d65b08a4e319f737adc0be1`
- Verified head commit：`ac93dc1890e7a7b40f7f882958defddad6c1dba4`
- Branch：`codex/m4-vscode-report-only-adapter`
- Base merge：`f16bcb2e33cca41cd0f43c9110b6d39b3fb1dcde`（PR #30 / M4.1）
- PR：`#31 M4.2: convert VS Code extension to report-only adapter`
- General CI run：`33636211313`
  - Ubuntu check job：`100267611452`
  - Windows check job：`100267611790`
  - Windows VSIX job：`100267978565`
- Context Service regression run：`33636211191`
  - Ubuntu job：`100267611593`
  - Windows job：`100267611815`

Run URLs：

```text
https://github.com/kun002/ContextIME/actions/runs/33636211313
https://github.com/kun002/ContextIME/actions/runs/33636211191
```

## PR CI 结果

General CI 在 Windows/Ubuntu 分别完成 TypeScript project references 与 43 项 workspace tests，结果全部通过。依赖这两个 check 的 Windows package job 随后通过内置 capability/external-module audit，生成并上传 34-file VSIX。

Context Service workflow 在 MSVC `/W4 /WX` 与 g++ `-Wall -Wextra -Werror -pedantic` 路径完成原生协议、store、service 和 lifecycle 回归，证明 Adapter 改造没有破坏 M4.1 receiver foundation。

## Owner 与删除能力

Adapter 已删除旧 controller 的：

- `session-controller.ts` 与本地 `PolicyEngine`；
- `windows-runtime-bridge.ts`、Windows IME polling 和 `switchTo()`；
- manual lock、pause/correction 命令和 learning/persistence；
- status bar 决策显示、surface memory 和 upstream bridge；
- `policy-core` 与 `windows-runtime` production dependency。

Extension manifest 现在只暴露 `Report Editor Context Now`、`Show Adapter Status` 与四项 `context-ime.adapter.*` 有界配置。Adapter 没有本地 mode decision 或系统 IME fallback controller。

## 协议与 transport 门禁

TypeScript codec 与 M4.1 原生协议保持：

- `CIME` magic、version `1`；
- editor request/response message type `3/4`；
- 固定 48-byte request 与 32-byte response；
- request ID、payload length、enum、boolean 和 reserved bytes 严格验证；
- language ID 最长 20 bytes，只允许 normalized ASCII 子集；
- 成功 response 后发送 `0x06` ACK。

Named Pipe client 每次只执行一个有界 exchange，connect/write/read/ACK 共用总 deadline。测试覆盖：

- 成功 request/response/ACK；
- service application rejection；
- service stall timeout；
- response 前 clean disconnect；
- endpoint 缺失 / service unavailable；
- frame corruption、request ID mismatch 和 oversized/invalid fields。

Scheduler 测试证明高频事件合并为最新 trigger，且 report 进行中出现的新 trigger 会在当前 report 结束后发送。

## 隐私与输入链路边界

wire request 只包含 focused、surface、syntax 和 language ID。Tree-sitter 只在 VS Code extension host 内分析当前文档，并使用 `document-N` 不透明 cache ID。

Adapter 不上报或持久化：

- source/selection text 或完整用户输入；
- document URI、文件/项目路径、project/document ID；
- password、Token、API Key 或环境变量值。

本改动未修改 TSF key event、librime、schema、composition、candidate 或 commit 链路。Pipe/service 故障只进入 Adapter diagnostics，不阻塞普通输入。

## VSIX 包审计

PR CI 验证包：

| 项目 | 值 |
|---|---|
| 文件名 | `context-ime-0.4.0-win32-x64.vsix` |
| 大小 | `2,117,251` bytes |
| SHA-256 | `750ada418b17138c9bfe6a2c519912b0e35de0c7666915061cd4b2561f3a06a` |
| 文件数 | `34` |
| bundle | `45,384` bytes |

bundle external require 仅为 `vscode`、`@vscode/tree-sitter-wasm` 和 Node 内建模块。打包脚本固定拒绝 Koffi、`windows-runtime`、旧 policy/learning/controller 标识及其他 external module。VSIX 使用 Adapter 专属第三方声明，只分发固定 `@vscode/tree-sitter-wasm@0.3.1`。

VSIX 是 Adapter 验证包，不是 ContextIME 原生输入法 `.exe` 安装程序，也不能注册、升级或卸载 ContextIME TSF 产品。

## 验证状态

| 项目 | 状态 | 证据边界 |
|---|---|---|
| report-only Adapter source | `implemented / built / statically_verified` | Windows/Ubuntu TypeScript + 12 Adapter tests |
| protocol/transport/scheduler | `implemented / runner_runtime_verified` | Windows Named Pipe / Ubuntu Unix socket fixtures |
| source capability isolation | `statically_verified` | 固定 forbidden audit 0 matches |
| VSIX bundle | `built / package_audited` | Windows CI；34 files；无切换 runtime/policy owner |
| Windows/Ubuntu PR CI | `passed` | General CI `33636211313` |
| Context Service regression | `passed` | run `33636211191` |
| installed M4.1 Context Service | `not_installed` | 当前 `0.3.1-preview` 不含 receiver |
| VS Code code/comment/Markdown/terminal | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 需要新版原生安装包和交互桌面 |
| Adapter 故障下普通中文输入 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | CI fixture 不替代 TSF 真机 |

## 下一阻塞

PR CI 已完成。合并后必须构建版本真实递增、包含 M4.1 receiver 的原生安装包，再在已登录且解锁的 Windows 交互桌面安装 Adapter，验证 code/comment/Markdown/terminal、TTL/focus、service stop/restart 和普通输入回退；在此之前不得把 M4 标记为 `real_machine_verified`。
