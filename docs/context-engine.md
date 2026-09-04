# ContextIME Context Engine

## 产品职责

Context Engine 是未来所有自动输入状态决策的唯一 owner。Context Source、Context Service、IME Host 和编辑器 Adapter 都不能各自再实现一套优先级。

```text
App / Surface / Syntax / Project / User sources
                     ↓
            normalized snapshot
                     ↓
            Context Engine (single owner)
                     ↓
      CHINESE / ENGLISH / KEEP + source
                     ↓
              IME state applier
```

M3.1 只建立纯 C++ 决策核心和跨平台单元门禁。它尚未接入 Context Service、Named Pipe 或 ContextIME TSF 前端，因此不会改变当前 `0.2.3-preview` 的输入行为。

## 为什么不直接复用 TypeScript 原型

`packages/policy-core` 保留了有价值的 composition、manual override、surface 和 fallback 经验，但它属于旧 VS Code 控制器原型：

- 运行时依赖 Node.js/TypeScript，不适合进入原生输入路径；
- 输入模型由编辑器原型主导，不是 Windows IME 产品协议；
- 旧优先级把 explicit lock 放在 composition protection 前；
- habit learner 和 surface memory 属于后续个性化阶段，不应提前成为 M3 基础决策的隐式 owner。

因此 M3 复用语义和测试经验，不复制旧运行时结构。

## 核心接口

代码位于：

```text
src/context-engine/include/contextime/context_engine.h
src/context-engine/src/context_engine.cpp
```

输入 `ContextSnapshot` 只包含已归一化、无正文的值：

- 当前中文/英文模式；
- composition 是否活动；
- candidate 是否可见；
- explicit user lock；
- 当前单调时间和 manual override 截止时间；
- automation 是否启用；
- context 是否可靠可用；
- user/project/syntax/surface/application 的可选模式建议。

输出 `Decision`：

```text
desired_mode: KEEP | CHINESE | ENGLISH
target_mode:  CHINESE | ENGLISH
source:       fixed diagnostic source
should_switch: true | false
```

如果规则目标等于当前模式，`desired_mode` 仍记录规则的中文/英文决定，但 `should_switch` 为 `false`，避免重复切换。

## 固定优先级

| 优先级 | 输入 | 结果 |
|---:|---|---|
| 1 | composition active 或 candidate visible | `KEEP` |
| 2 | explicit user lock | lock 指定模式 |
| 3 | recent manual override 未过期 | `KEEP` |
| 4 | automation disabled | `KEEP` |
| 5 | context unavailable/unreliable | `KEEP` |
| 6 | user rule | 用户指定模式 |
| 7 | project rule | 项目指定模式 |
| 8 | syntax context | 语法建议模式 |
| 9 | surface context | 输入区域建议模式 |
| 10 | application default | 应用默认模式 |
| 11 | 无可靠规则 | `KEEP` |

composition/candidate 保护是绝对边界：即使 explicit lock 或其他规则要求切换，也不能破坏当前组合。

automation disabled 只关闭自动规则；explicit user lock 属于用户明确操作，仍然有效。Context Service 断开时 `context_available = false`，Engine 立即 `KEEP`，普通输入法链路继续工作。

## 热路径、隐私和故障边界

核心：

- C++17，平台无关；
- 不调用 Windows API；
- 不访问文件、注册表、网络、AI、Git、PowerShell 或 Node.js；
- 不查询系统时钟，时间由调用方传入；
- 不保存状态、正文、源码、密码、Token 或环境变量；
- `Evaluate` 为 `noexcept` 的确定性内存计算。

Context Engine 只决定，不直接切换 TSF 状态。后续只能有一个 IME state applier 消费决策；Adapter 只能上报 context source，不能复制策略。

## 回归门禁

测试位于：

```text
tests/context-engine/context_engine_test.cpp
scripts/run-context-engine-tests.ps1
.github/workflows/context-engine.yml
```

固定测试覆盖：

- composition 和 candidate 覆盖全部其他输入；
- lock、manual、user、project、syntax、surface、application 的完整优先级；
- manual override 到期边界；
- automation disabled；
- Context Service 不可用时 KEEP；
- explicit lock 在 automation disabled 时仍有效；
- 目标等于当前状态时不重复切换；
- `CHINESE / ENGLISH / KEEP` 和诊断 source 的稳定字符串；
- `Evaluate` 保持 `noexcept`。

Windows 使用 MSVC `/std:c++17 /W4 /WX`，Ubuntu 使用 g++ `-std=c++17 -Wall -Wextra -Werror -pedantic`。本地主开发机当前没有可直接调用的 C++ compiler，因此本地没有虚报 build。固定 GitHub Windows/Ubuntu runner 已使用两套编译器执行同一门禁，证据见 [`evidence/m3-context-engine-core-ci.md`](evidence/m3-context-engine-core-ci.md)。

## 当前验证边界

| 项目 | 状态 |
|---|---|
| C++ Context Engine API 和固定优先级 | `implemented` |
| Windows/Ubuntu 编译和单元测试 | `built` / `statically_verified`；run `33465040903`，每个平台 61 assertions |
| Context Service | `implemented` / `built`；尚未安装或接入 TSF |
| 版本化 Named Pipe 协议 | `implemented` / `statically_verified`；见 [`evidence/m3-context-service-ci.md`](evidence/m3-context-service-ci.md) |
| TSF/IME state applier 接入 | `not_implemented` |
| VS Code Adapter | `not_started` |
| 真机自动上下文切换 | `REAL_WINDOWS_VERIFICATION_REQUIRED` |

M3.1 的 Windows/Ubuntu 严格编译和所有断言已经通过。随后进入 M3.2 Context Service 和版本化 IPC；在服务故障回退与 composition 保护的集成证据存在前，不接入 VS Code Adapter。

M3.2 的独立服务、协议和故障回退设计见 [`context-service.md`](context-service.md)。该实现不改变 M3.1 的决策优先级，也不把同步 IPC 放入 TSF 按键路径。
