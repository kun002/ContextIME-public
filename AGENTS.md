# ContextIME Agent Instructions

## 产品定义

ContextIME 是一款面向软件开发者的 Windows 原生中文输入法。

最终必须具备：

- 正常的拼音组合、候选、选词和上屏；
- 不依赖编辑器插件也能正常使用；
- 支持代码、注释、字符串、终端、项目词库等开发场景配置；
- 编辑器插件只作为可选的高精度上下文来源。

ContextIME 不是以 VS Code 插件、输入法切换器或控制其他输入法的外壳作为最终产品。

## 权威文档

进行非简单修改前，先阅读：

- `docs/product-intent.md`
- `docs/product-plan.md`
- `docs/technical-route.md`

如果旧代码、旧 Roadmap 或原型行为与这些文档冲突，以产品规划和技术路线为准，除非用户明确改变方向。

## 当前最高优先级

当前只优先完成 Windows 原生输入法基线：

```text
安装
→ 出现在 Windows 输入法列表
→ 可以激活
→ 输入拼音
→ 出现组合文本
→ 出现中文候选
→ 选择并上屏
→ 中英文模式正常
→ 与其他输入法并存
→ 可独立升级和卸载
