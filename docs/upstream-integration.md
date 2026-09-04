# Integrating with CI124/auto-ime

## 审计结论

ContextIME 固定参考：

```text
CI124/auto-ime@3873421e6ef2361f7f78324c02c378ed0e6ceca5
version 0.8.1-beta
```

上游当前是完整 VS Code 扩展，不是稳定的库包：

- 没有面向外部使用的公共模块入口。
- `postinstall` 会下载 Tree-sitter WASM。
- 平台适配、AST、日志和 VS Code 生命周期相互引用。

因此当前不把整个仓库作为 npm/Git 依赖，也不完整复制。采用**固定提交 + 小模块同步 + 来源清单 + 差异测试**。

## 已复用：Windows 运行时

已从上游以下模块复用设计和必要实现：

- `src/win32/ime-ffi.ts`
- `src/platforms/windows/dual-keyboard.ts`
- `src/core/state-tracker.ts`

ContextIME 保留：

- `koffi` 直接调用 `user32.dll`、`imm32.dll`。
- `PostMessageW(WM_INPUTLANGCHANGEREQUEST)` 的低延迟布局切换。
- 自动切换后内部目标状态与系统状态协调。
- 轮询捕获外部输入模式变化。

ContextIME 增加：

- 中文键盘布局内部的 IMM32 Open Status 检测。
- 当前窗口没有输入上下文时的默认 IME 窗口回退。
- 双布局与单中文布局策略。
- 本地/RDP 独立轮询配置。
- 自动协调超时不得直接进入习惯学习。
- 原生运行时不可用时自动回退到 dry-run。
- 注入式原生端口和跨平台单元测试。

具体来源和许可证见 [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)。

## 待复用：ASTAnalyzer

上游 `ASTAnalyzer` 值得保留的能力：

- 快速同步注释检测。
- Tree-sitter WASM 按语言延迟加载。
- 注释和字符串 Query 缓存。
- 旧语法树复用与解析取消机制。

接入前需要先拆除以下耦合：

- 直接依赖上游扩展目录结构和 `dist/wasm`。
- 直接依赖上游 Logger 类型。
- 单一 `lastTree` 无法安全服务多个文档和语言。
- WASM 下载与扩展安装生命周期绑定。

目标结构：

```text
packages/syntax-runtime/
  parser runtime
  language registry
  document tree cache
  query registry
  wasm manifest
```

随后由 `vscode-adapter` 将编辑器、文档和光标信息转换为：

```text
code | comment | string | markdownText | markdownCode | unknown
```

## 决策链

```text
VS Code / Windows event
  -> Context Provider
  -> Syntax Runtime
  -> InputContext
  -> PolicyEngine
  -> SwitchGuard
  -> Windows Runtime
```

## 不允许进入按键热路径的内容

- 网络请求或大模型推理。
- SQLite 全表查询。
- PowerShell 进程逐次启动。
- 每次事件重新解析所有文件。
- AutoHotkey 高频全局键盘钩子。
- 未固定版本的远程代码或运行时下载。
