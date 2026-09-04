# Windows VSIX Adapter Packaging

## 产品边界

ContextIME 当前生成：

```text
context-ime-0.4.0-win32-x64.vsix
```

这是可选的 VS Code report-only Adapter 验证包，不是 ContextIME 原生输入法安装程序。VSIX 不包含 TSF 注册、librime、Context Service 或输入法升级/卸载能力，也不读取或切换系统 IME mode。

用户正常输入只需要 ContextIME 原生 `.exe` 安装程序。只有需要 VS Code 精确 code/comment/Markdown/terminal 上下文时才安装 VSIX，并且必须配合包含 M4.1 editor receiver 的 ContextIME 原生版本。

## 包内容

Adapter 业务代码 bundle 为 `dist/extension.js`。唯一随包发布的生产依赖是固定版本：

```text
@vscode/tree-sitter-wasm@0.3.1
```

WASM 只在 VS Code extension host 内本地分析当前文档，不在运行时联网下载。包内不包含 Koffi、`windows-runtime`、旧 policy owner、habit learning、manual lock 或 Windows mode switching。

## 构建与审计

```powershell
npm install --ignore-scripts
npm run check
npm run package:vsix
```

打包门禁：

1. 编译 TypeScript project references；
2. 使用 esbuild bundle Adapter 与 `syntax-runtime`；
3. 拒绝 Koffi、旧 controller/policy/learning 标识；
4. 拒绝 `vscode`、固定 Tree-sitter 包和 Node 内建模块之外的 external require；
5. 在隔离 staging 目录仅安装固定 production dependency；
6. 检查 bundle、核心 WASM、TypeScript/C# WASM、README、许可证和 Adapter 专属第三方声明；
7. 使用 `@vscode/vsce` 生成 `win32-x64` VSIX。

Windows 打包任务只在 Windows、Ubuntu 类型检查与测试均通过后运行。CI artifact `context-ime-win32-x64-vsix` 只是 Adapter 包，不能作为用户的 ContextIME 输入法安装包。

## 安装与诊断

VS Code 图形界面：

```text
Extensions: Install from VSIX...
```

命令行：

```powershell
code --install-extension .\context-ime-0.4.0-win32-x64.vsix --force
```

安装后可运行：

```text
ContextIME: Show Adapter Status
ContextIME: Report Editor Context Now
```

状态窗口用于检查最近一次 normalized context、Named Pipe transport 和本地 syntax runtime，不会切换系统输入法。配置项仅位于 `context-ime.adapter.*`。

## 卸载

```powershell
code --uninstall-extension kun002.context-ime
```

卸载 Adapter 不会卸载 ContextIME 原生输入法，也不删除 ContextIME 用户词库。

## 验证边界

- VSIX 尚未发布到 Marketplace，也未做商业代码签名；
- 当前只生成 Windows x64 Adapter 包；
- 构建、测试与包审计只能证明 `implemented / built / statically_verified`；
- 当前已安装的 `0.3.1-preview` 不含 M4.1 receiver；
- VS Code → installed Context Service → Context Engine → TSF 的 code/comment/terminal 行为仍为 `REAL_WINDOWS_VERIFICATION_REQUIRED`。
