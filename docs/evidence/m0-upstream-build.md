# M0 原生上游构建证据

## 结论

已在 GitHub Actions `windows-2022` 环境中，从固定提交完整构建 Weasel 0.17.4 上游安装程序。

这证明：

- 上游版本和子模块可以精确复现；
- Visual Studio 2022 构建链可以工作；
- 固定版本的 librime 开发资产可以获取并参与构建；
- Rime 数据、Weasel 前端和 NSIS 安装程序可以生成。

这不证明：

- ContextIME 已经注册为 Windows 输入法；
- 安装程序已经在真实 Windows 桌面安装；
- 拼音组合、候选窗口、选词和上屏已经人工验证；
- 卸载和重装已经通过；
- 该安装包已经完成 ContextIME 品牌和 GUID 隔离。

## 构建来源

### Weasel

- 版本：`0.17.4`
- 提交：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- 许可证：GPL-3.0

### librime

- 版本：`1.13.1`
- 提交：`1c23358157934bd6e6d6981f0c0164f05393b497`
- 许可证：BSD-3-Clause

CI 在构建前读取 `native/upstream.lock.json`，并对两个仓库的实际 `HEAD` 执行精确 SHA 校验。任一提交不匹配都会停止构建。

## GitHub Actions

- 工作流：`Native Upstream Baseline`
- 运行 ID：`29818696944`
- 运行结果：`success`
- Runner：`windows-2022`
- Runner image：`20260714.244.1`
- Artifact：`contextime-m0-weasel-0.17.4`
- Artifact ID：`8490707972`
- Artifact ZIP SHA-256：`533fe5a526ee049193e7520efb057a91d1832e81afe023ad84078205042e74e3`

## 生成安装程序

- 文件：`weasel-0.17.4.0-installer.exe`
- 大小：`12,048,125` 字节
- SHA-256：`bd2a07f851f43bbf333da4d03c69bf087dda50b46afd37898dddab1a7c06adeb`

该文件仍是未修改的 Weasel 上游安装程序，只用于确认原生输入法底座可以被 ContextIME 仓库稳定构建。

## 工具链

- Visual Studio 2022 Enterprise
- MSVC `19.44.35228`
- Platform Toolset `v143`
- Windows SDK `10.0.19041.0`
- CMake `3.31.6`
- Boost `1.84.0`
- NSIS installer

## 已通过步骤

- [x] 读取上游锁文件。
- [x] checkout 固定 Weasel 提交及递归子模块。
- [x] 校验 Weasel 提交。
- [x] 校验 librime 子模块提交。
- [x] 配置 Visual Studio 2022 环境。
- [x] 构建 Boost 1.84.0。
- [x] 按 `1.13.1` tag 获取 librime 开发资产。
- [x] 构建 Rime 数据。
- [x] 构建 Weasel x64/x86 原生前端。
- [x] 生成 NSIS 安装程序。
- [x] 计算并保存安装程序 SHA-256。
- [x] 上传安装程序和构建证据。

## 仍需真机完成

- [ ] 在干净 Windows 10 x64 安装。
- [ ] 在干净 Windows 11 x64 安装。
- [ ] 验证输入法出现在系统输入法列表。
- [ ] 在记事本中验证拼音组合、候选、选词和上屏。
- [ ] 在 VS Code、浏览器和 Terminal 中验证基础输入。
- [ ] 验证中文和英文模式。
- [ ] 验证卸载、重装和注册项清理。
- [ ] 记录屏幕截图、日志和测试环境版本。

## 下一步边界

下一阶段只建立 ContextIME 的产品身份隔离：

1. 新产品名和安装目录；
2. 独立 TSF Profile GUID、服务 GUID 和注册项；
3. 独立用户数据目录；
4. 独立图标和开始菜单入口；
5. 与原版 Weasel 并存安装、卸载和升级。

在上述隔离通过前，不接入代码/注释识别、个人习惯和项目词库。
