# Syntax Runtime

## 目标

`@context-ime/syntax-runtime` 在不依赖 VS Code API 的情况下判断光标位于：

```text
code | comment | string | unknown
```

它只处理语法上下文，不切换输入法，也不保存源码或用户输入历史。

## 现成底座

运行时使用微软维护的：

```text
@vscode/tree-sitter-wasm@0.3.1
```

该包包含 VS Code 使用的 Tree-sitter 核心 WASM 和预编译语言 WASM。ContextIME 不在 `postinstall` 中联网下载解析器。

与标准 UTF-8 Tree-sitter 构建不同，微软供 VS Code 使用的核心通过 UTF-16 接收 JavaScript 字符串，节点索引和列均以 UTF-16 code unit 表示。这与 VS Code `TextDocument.offsetAt` 和 JavaScript 字符串偏移一致，不应再次转换成 UTF-8 字节。

当前映射包括：

- JavaScript / JSX
- TypeScript / TSX
- C#
- C / C++
- Python
- Go
- Rust
- Java
- Ruby
- PHP
- CSS
- Bash
- PowerShell

ShaderLab、HLSL 和当前未提供语法 WASM 的语言继续使用保守回退，不冒充精确 AST 识别。

## 缓存策略

### 光标移动

文档版本和正文未变化时直接复用已有语法树，不重新解析。

### 文档修改

运行时比较旧正文和新正文，计算一个覆盖全部变化的最小差异区间：

1. 找共同前缀。
2. 找共同后缀。
3. 避免在 UTF-16 代理对中间切分编辑边界。
4. 直接使用 UTF-16 索引和行内列调用 `tree.edit`。
5. 使用旧树执行增量解析。

这种方式比上游单一 `lastTree` 更安全：

- 每个文档独立缓存。
- 不会把一个文件的树传给另一个文件。
- 中文、Emoji 和其他 Unicode 内容位于光标或编辑位置之前时仍与 VS Code 偏移一致。

### 容量限制

默认最多缓存 8 个文档，按最近使用时间淘汰并释放旧树。

## 光标分类

运行时使用光标位置取得最小语法节点，并向父节点上溯：

- 节点类型包含 `comment`：注释。
- 节点类型包含 `string`，或属于字符、Heredoc、Regex 字面量：字符串。
- 没有命中：代码。

光标位于节点末端时会额外检查同一行的前一个 Unicode 码点，避免结束边界返回相邻代码节点。光标落在 Emoji 代理对中间时会先收缩到合法码点边界。

## VS Code 回退规则

```text
Markdown -> 现有 Markdown fence 判断
支持语言 -> SyntaxRuntime
不支持 / WASM 加载失败 -> 保守行级判断
```

因此新增运行时失败不会阻止 ContextIME 工作。

## 当前限制

- ShaderLab/HLSL 尚无正式映射。
- 字符串插值内部的表达式当前继承字符串祖先，后续需要按语言细化。
- 当前使用完整文档字符串计算最小差异，极大文件仍需基准测试。
- 正式 VSIX 必须包含微软包中的核心和语言 WASM 文件及许可证。
