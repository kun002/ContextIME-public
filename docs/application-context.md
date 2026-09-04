# ContextIME 前台应用 Context Source（M3.4）

## 当前范围

M3.4 为 Context Service 增加 Windows 前台应用采集和保守分类。采集只在服务处理线程执行，不进入 decision cache reader 或未来 TSF 按键路径。

```text
GetForegroundWindow
→ PID / thread id
→ QueryFullProcessImageName (transient)
→ normalized executable basename only
→ window class (no title)
→ conservative application/surface classification
→ fill missing ContextSnapshot rules
→ Context Engine
```

服务没有安装或接入真实 IME，因此本阶段不会自动修改用户输入状态。

## 隐私字段

`ApplicationContext` 只保留：

- foreground process id；
- foreground UI thread id；
- 小写 executable basename，固定最大 259 个字符；
- 小写 top-level window class，固定最大 255 个字符；
- application kind；
- surface kind。

禁止保留或记录：

- 完整 executable path；
- window title；
- 文档名、标签页名或项目名；
- 控件文字、输入正文或剪贴板；
- 源码、密码、Token、API Key 或环境变量值。

Win32 API 查询完整 image path 只存在于函数局部固定 buffer，返回前立即裁剪为 basename。超过固定容量的标识会被丢弃，不做可能碰撞的截断。

## 保守分类

当前 application kind：

```text
UNKNOWN
TERMINAL
CODE_EDITOR
BROWSER
DOCUMENT_EDITOR
```

当前 surface kind 只有：

```text
UNKNOWN
TERMINAL
```

独立终端 executable 或可靠 console/Windows Terminal window class 被识别为 `TERMINAL`，并只在 request 没有已有 surface/application rule 时填充 English：

```text
surface_context = ENGLISH
application_default = ENGLISH
```

user/project/syntax 规则仍由 Context Engine 以更高优先级处理。调用方已有 surface/application rule 也不会被覆盖。

VS Code、Visual Studio、JetBrains、浏览器和文档程序只能从 top-level window 判断应用，不能可靠判断 editor/comment/string/terminal/search/chat 等输入区域。因此它们只产生 application kind，surface 保持 `UNKNOWN`，不生成中文/英文建议。

VS Code integrated terminal 也不能仅靠 `Code.exe` 猜测，必须等待未来 Adapter 或可靠输入区域 source。

## 故障边界

以下情况返回 `available=false`：

- 没有 foreground window；
- 不能取得 PID/thread id；
- 进程 image 和 window class 都不可用；
- 唯一标识超过固定容量且没有另一个可靠标识。

Context Source 不可用时不填任何规则，由原 Context Engine `KEEP` 路线处理。不能因为 OpenProcess 权限、UWP window host、桌面切换或非交互 Session 而猜应用。

## 门禁

平台无关分类测试覆盖：

- Windows 与 `/` 路径只保留 lowercase basename；
- 完整路径不会进入结果；
- terminal executable/class 分类；
- Code/Visual Studio/JetBrains、browser、document 分类不猜 surface；
- class-only fallback；
- caller rule 不被覆盖；
- zero PID、空 identity 和 oversized basename 回退；
- 稳定诊断字符串；
- normalization/apply 保持 `noexcept`。

Windows runner 另外调用真实 `GetForegroundWindow` capture，并以固定断言验证：无论 runner 是否存在 foreground window，结果都保持 PID/identity 一致、固定 buffer 终止且不含完整路径。

## 验证边界

| 项目 | 状态 |
|---|---|
| application normalization/classification | `implemented` / `built` / `statically_verified`；双平台 31 assertions |
| Windows foreground capture | `implemented` / `built` / `runtime_verified`（runner API/bounds） |
| terminal surface English default | `implemented` / `statically_verified` |
| generic input-area recognition | `not_implemented` |
| VS Code precise surface | `not_started`；M4 Adapter |
| Context Service 安装/生命周期 | `not_implemented` |
| 真机自动状态应用 | `REAL_WINDOWS_VERIFICATION_REQUIRED` |

CI runner 调用 Win32 API 不等于用户交互桌面验证。真实应用识别、焦点变化和状态应用必须在 Context Service 接入安装/运行时后重新做 Windows 真机证据。

固定证据见 [`evidence/m3-application-context-ci.md`](evidence/m3-application-context-ci.md)，run `33468372256`。
