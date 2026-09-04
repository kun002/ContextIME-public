# M3.1 Context Engine 核心 CI 证据

## 结果

ContextIME 已建立平台无关的 C++17 Context Engine 决策核心，并在固定 Windows MSVC 与 Ubuntu g++ runner 上严格编译、执行相同的 61 项断言：

```text
Context Engine: 61 assertions passed
```

该结果证明纯决策核心已经 `implemented`、`built` 和 `statically_verified`，不证明 Context Service、Named Pipe、TSF 状态应用或真机自动切换已经完成。

## 固定构建身份

- Source commit：`db7f0e274ee165c071eb731ff3a443d7515464d3`
- PR：`#18 Add native Context Engine decision core`
- Pull request CI run：`33465040903`
- Workflow：`Context Engine`
- Windows job：`99723039084`
- Ubuntu job：`99723038960`
- Windows runner：`windows-2022`
- Ubuntu runner：`ubuntu-24.04`

Run URL：

```text
https://github.com/kun002/ContextIME/actions/runs/33465040903
```

## 双编译器门禁

Windows：

```text
MSVC
/std:c++17 /EHsc /W4 /WX
61 assertions passed
job conclusion: success
```

Ubuntu：

```text
g++
-std=c++17 -Wall -Wextra -Werror -pedantic
61 assertions passed
job conclusion: success
```

两个平台使用同一份：

```text
src/context-engine/include/contextime/context_engine.h
src/context-engine/src/context_engine.cpp
tests/context-engine/context_engine_test.cpp
scripts/run-context-engine-tests.ps1
```

## 已验证语义

断言覆盖：

- composition active 时覆盖 explicit lock、manual 和所有自动规则，输出 `KEEP`；
- candidate visible 时使用同一 composition protection；
- explicit user lock 优先于 recent manual override；
- manual override 未过期时 `KEEP`，到期边界后才允许自动规则；
- user rule 优先 project rule；
- project rule 优先 syntax/surface/application；
- syntax 优先 surface/application；
- surface 优先 application；
- 无可靠规则时 `KEEP`；
- automation disabled 时忽略自动规则；
- context unavailable 时忽略外部规则并 `KEEP`；
- explicit lock 在 automation disabled 时仍有效；
- 规则目标等于当前状态时 `should_switch: false`；
- `CHINESE / ENGLISH / KEEP` 和诊断 source 字符串稳定；
- `Evaluate` 保持 `noexcept`。

## 热路径与隐私静态边界

核心没有文件、注册表、Windows API、网络、AI、Git、PowerShell 或 Node.js 依赖。它不读取系统时钟，不保存状态，也没有输入正文、源码、密码、Token 或环境变量字段。

调用方必须提交归一化 `ContextSnapshot`。Context Engine 只返回决策，不直接切换 IME；后续 Context Service 和 IME state applier 仍必须保持单一 owner。

## 验证状态

| 项目 | 状态 | 证据边界 |
|---|---|---|
| C++17 Context Engine | `implemented` | 固定 API 和优先级已提交 |
| Windows MSVC build/test | `built` / `statically_verified` | job `99723039084`，61 assertions |
| Ubuntu g++ build/test | `built` / `statically_verified` | job `99723038960`，61 assertions |
| 原有 TypeScript/VS Code 原型 | `unchanged` | 只作为研究资产，不是产品决策 owner |
| ContextIME 0.2.3 输入行为 | `unchanged` | 未修改 TSF、IPC、schema、Lua 或安装器 |
| Context Service | `not_implemented` | M3.2 |
| 版本化 Named Pipe | `not_implemented` | M3.2 |
| 真机自动上下文切换 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 尚未接入运行时 |
| VS Code Adapter | `not_started` | 不在 M3.1 范围 |

## M3.1 结论与下一项

M3.1 只关闭纯 C++ Context Engine 的接口、优先级、故障 KEEP 和双编译器单元门禁。下一项是 M3.2 Context Service 与版本化本地 IPC，先证明超时/断开不阻塞普通输入，再考虑连接 IME state applier；VS Code Adapter 继续保持未开始。
