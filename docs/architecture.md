# Architecture（历史控制器原型）

> 本文记录旧 VS Code/auto-IME 控制器原型，不再定义 ContextIME 产品架构。当前原生产品路线以 [`technical-route.md`](technical-route.md) 为准，M3 的单一决策 owner 见 [`context-engine.md`](context-engine.md)。下方 `Existing IME` 结构不能用于实现或验收 ContextIME 原生输入法。

## 目标边界

本项目只负责“应该使用中文还是英文”的决策，不负责拼音转换、候选窗口和 Windows TSF 输入法实现。

```text
VS Code / Codex / Terminal context
                │
                ▼
        Context Adapter Layer
                │
                ▼
     Policy Engine + Habit Learner
                │
                ▼
      Switch Guard / Manual Lock
                │
                ▼
     auto-ime IPlatformAdapter
                │
                ▼
 Existing IME: WeChat / Rime / Microsoft
```

## 决策优先级

1. 用户锁定。
2. 拼音组合输入保护。
3. 用户刚刚手动切换的保护期。
4. 个人习惯学习结果。
5. 语法上下文规则。
6. 输入区域规则。
7. 应用默认规则。
8. 保持当前状态。

## 性能要求

- 热路径不得访问网络或调用大模型。
- 光标移动只做内存计算；AST 复用上游增量解析结果。
- 相同目标模式不重复调用 Windows API。
- 习惯数据异步批量持久化，第一版仅使用内存接口。
- 目标决策耗时 P95 小于 1ms，不含上游 AST 与系统切换耗时。

## 隐私

习惯学习只保存：

- 应用标识。
- 输入区域标识。
- 文件语言。
- 语法上下文。
- 预测模式与用户纠正模式。
- 次数、置信度和时间。

不保存完整输入文本、剪贴板、源码和聊天内容。
