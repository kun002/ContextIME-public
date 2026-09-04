# ContextIME 技术路线

## 1. 总体架构

```text
ContextIME Windows IME
├─ Windows TSF 前端
│  ├─ 注册与激活
│  ├─ 按键处理
│  ├─ 组合输入
│  ├─ 候选窗口
│  ├─ 文本上屏
│  └─ 中英文状态
├─ librime 输入引擎
│  ├─ 拼音方案
│  ├─ 基础词库
│  ├─ 候选排序
│  ├─ 用户词频
│  └─ 用户词典
├─ Developer Context Engine
│  ├─ 应用识别
│  ├─ 输入区域识别
│  ├─ 语法上下文
│  ├─ 混合输入识别
│  ├─ 项目词库
│  └─ 个人习惯
├─ Context Service
│  ├─ 配置管理
│  ├─ 项目索引
│  ├─ 策略决策
│  ├─ 本地 IPC
│  └─ 日志诊断
├─ 可选编辑器适配器
│  ├─ VS Code
│  ├─ Visual Studio
│  └─ JetBrains
├─ 设置程序
└─ 安装、升级与卸载
```

原则：

- 原生输入法是产品入口；
- librime 负责成熟的拼音和候选能力；
- 编辑器插件只提供高精度上下文；
- 上下文服务失效时，输入法仍能正常输入；
- 按键热路径不能依赖网络、AI、项目扫描或外部脚本。

## 2. 上游底座

### 2.1 输入引擎

优先采用 librime，负责：

- 拼音解析和组合；
- 基础词典和输入方案；
- 候选生成、排序和过滤；
- 用户词频和用户词典；
- 简繁和方案配置。

禁止从零实现拼音分词、基础词库和候选排序，除非完成书面对比并证明 librime 不满足需求。

### 2.2 Windows 输入法前端

优先路线：

```text
固定 Weasel 上游版本
→ 在干净 Windows 环境复现构建
→ 完成许可证和修改范围审计
→ 建立 ContextIME 品牌分支
→ 先保持原生输入链路完整
→ 再接入开发者上下文能力
```

选择 Weasel 的原因：

- 已具备 Windows 输入法注册与 TSF/IME 前端经验；
- 已具备 librime 接入；
- 已处理候选窗口、组合、安装和应用兼容；
- 比从零编写 TSF 前端更快达到可验证结果。

在上游审计完成前，不直接复制大段代码进入主线。

## 3. 技术栈

### 3.1 原生输入法与输入引擎

使用 C++：

- 与 librime、Weasel 和 Windows COM/TSF 路线一致；
- 避免跨语言运行时进入按键热路径；
- 便于复用现有原生代码和构建系统。

### 3.2 Context Service

第一阶段使用 C++，避免同时引入多个后台运行时。

服务负责非按键热路径任务，不直接拥有输入法组合状态。

### 3.3 设置程序

优先 C# + WPF：

- 不在按键热路径；
- Windows 原生部署成熟；
- 适合配置、词库管理、日志和诊断界面。

### 3.4 编辑器适配器

- VS Code：TypeScript；
- Visual Studio：C#；
- JetBrains：Kotlin。

适配器只上报上下文，不复制输入引擎和完整策略。

## 4. 模块职责

### 4.1 IME Host

负责：

- Windows 注册、激活和生命周期；
- 接收和处理按键；
- 管理组合状态；
- 调用 librime；
- 显示候选；
- 选词和上屏；
- 中英文、全角/半角和标点状态；
- 读取 Context Service 的短决策。

禁止：

- 扫描项目；
- 执行复杂 AST 分析；
- 网络请求和大模型调用；
- 阻塞等待后台服务；
- 保存完整用户输入正文。

### 4.2 Context Service

负责：

- 当前前台应用和窗口信息；
- 用户配置和策略；
- 项目上下文和项目词库；
- 接收编辑器适配器事件；
- 生成简短输入决策；
- 日志和诊断。

通信优先采用 Windows Named Pipe。

要求：

- 本地通信；
- 协议版本化；
- 严格超时；
- 服务不可用时 IME 立即回退；
- 不允许服务故障阻塞输入。

### 4.3 Developer Context Engine

输入：

- 应用标识；
- 输入区域；
- 文件语言；
- 语法位置；
- 项目标识；
- 用户锁定和手动操作；
- 当前组合状态。

输出：

```text
保持当前模式
临时英文片段
中文输入
英文输入
禁用自动干预
```

固定优先级：

```text
组合输入保护
→ 用户锁定
→ 当前临时英文片段
→ 用户明确规则
→ 项目规则
→ 编辑器语法
→ 输入区域
→ 应用默认
→ 保持当前状态
```

禁止多个模块同时修改状态。

### 4.4 编辑器适配器

上报最小上下文：

```json
{
  "protocolVersion": 1,
  "process": "Code.exe",
  "surface": "editor",
  "language": "csharp",
  "syntax": "comment",
  "project": "hashed-project-id",
  "document": "hashed-document-id"
}
```

禁止上报：

- 完整源码；
- 完整用户输入；
- 密钥、Token 和环境变量值；
- 与输入策略无关的文件内容。

### 4.5 Project Indexer

优先使用成熟索引来源：

1. 编辑器或 Language Server 符号；
2. 编译数据库；
3. 项目文件；
4. 文件名和目录名；
5. 没有可用来源时才做本地语法解析。

首批支持：

- C# 和 Unity；
- TypeScript/JavaScript；
- C/C++；
- Python。

输出字段：

```text
symbol
symbol_type
project_id
frequency
last_seen
source
```

## 5. 仓库目标结构

```text
ContextIME/
├─ AGENTS.md
├─ docs/
│  ├─ product-intent.md
│  ├─ product-plan.md
│  ├─ technical-route.md
│  ├─ native-ime.md
│  ├─ context-protocol.md
│  └─ release-plan.md
├─ third_party/
│  ├─ librime/
│  └─ weasel/
├─ src/
│  ├─ ime-host/
│  ├─ context-service/
│  ├─ project-indexer/
│  ├─ policy-core/
│  ├─ settings/
│  └─ installer/
├─ adapters/
│  ├─ vscode/
│  ├─ visual-studio/
│  └─ jetbrains/
├─ schemas/
│  ├─ context-event.schema.json
│  ├─ user-profile.schema.json
│  └─ project-dictionary.schema.json
├─ profiles/
│  └─ developer-default/
├─ tests/
│  ├─ ime/
│  ├─ policy/
│  ├─ integration/
│  └─ installer/
└─ prototype/
   └─ vscode-controller/
```

当前 `packages/` 和 VSIX 在完成迁移前视为原型资产，不能继续定义产品入口。

## 6. 实施阶段

### Phase 0：上游与构建审计

任务：

1. 固定 librime 和 Weasel 的版本或提交；
2. 审计许可证、子模块和补丁策略；
3. 在干净 Windows 10/11 x64 环境复现构建；
4. 记录工具链、SDK、依赖和构建耗时；
5. 选择 fork、submodule 或 patch-stack 管理方式；
6. 产出最小可重复构建脚本。

验收：

- 从空环境可以构建原始 Weasel；
- 上游版本可追溯；
- 无手工复制未记录文件；
- 构建失败可定位到具体依赖。

### Phase 1：ContextIME 原生基线

任务：

1. 建立 ContextIME 品牌和产品标识；
2. 注册为独立 Windows 输入法；
3. 保持 librime 组合、候选和上屏链路；
4. 生成安装和卸载程序；
5. 加入最小日志与诊断；
6. 建立干净机器冒烟测试。

验收证据：

- Windows 输入法列表截图或自动化查询；
- 记事本拼音组合和候选；
- 数字选词和上屏；
- 中英文切换；
- 安装、卸载、重装；
- 至少五类应用输入测试。

### Phase 2：开发者方案

任务：

- 开发者默认 schema；
- 技术词库和用户词典；
- 路径、URL、命令、CamelCase 识别；
- 设置界面和配置迁移；
- 不依赖编辑器的基础场景规则。

### Phase 3：上下文服务与适配器

任务：

- 定义版本化 Named Pipe 协议；
- 迁移可复用 `policy-core`；
- VS Code 原型降级为轻量上报器；
- 组合输入保护；
- 服务超时与故障回退。

### Phase 4：项目词库与个人学习

任务：

- 项目符号增量索引；
- 项目词库隔离；
- 候选来源和权重；
- 用户可查看、删除和暂停学习；
- 性能和隐私审计。

## 7. 当前原型处理

保留：

- `policy-core` 的优先级和防抖经验；
- `syntax-runtime` 的 Tree-sitter 解析能力；
- VS Code 上下文采集；
- Windows 状态观察实验；
- CI、诊断和打包经验。

降级：

- `vscode-adapter` 是可选适配器原型；
- `windows-runtime` 是输入状态控制实验；
- VSIX 是验证包，不是 ContextIME 输入法产品。

禁止继续做：

- 在 M1 前扩展 Chat、Search、Command Palette 等焦点识别；
- 把更多系统输入法切换逻辑包装成正式产品；
- 用 VSIX 安装成功替代原生输入法安装验收。

## 8. 构建与发布要求

### 构建

- 固定工具链和依赖版本；
- Windows 构建必须可重复；
- 第三方源码、补丁和许可证可追溯；
- CI 单元测试不能替代真机输入测试。

### 安装

必须验证：

- 首次安装；
- 覆盖升级；
- 降级或回滚；
- 完整卸载；
- 重启前后状态；
- 不影响系统其他输入法。

### 发布

首个公开版本只提供 Windows x64 测试版，不同时展开其他平台。

## 9. 性能与故障边界

- 按键热路径不得等待 Context Service；
- IPC 超时后立即使用本地默认策略；
- 候选和组合状态只能由 IME Host/librime 主链管理；
- 项目索引在独立线程或进程执行；
- 日志不得包含完整输入、源码、密码和 Token；
- 所有自动规则必须允许用户关闭；
- 崩溃后必须保留正常英文输入或回退能力。

## 10. 下一步执行顺序

```text
1. 固定 librime 和 Weasel 上游版本
2. 完成许可证和构建审计
3. 在干净 Windows 环境复现 Weasel 构建
4. 确定 fork/submodule/patch 管理方式
5. 建立原生主线目录
6. 替换品牌和独立产品标识
7. 生成 ContextIME 安装程序
8. 验证 Windows 输入法注册
9. 验证拼音组合、候选、选词和上屏
10. M1 通过后再接入开发者上下文
```
