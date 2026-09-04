# M0 上游基线

## 产品结果

建立可重复验证的 Windows 原生输入法上游基线。该阶段不修改品牌、不增加开发者规则，也不继续扩展 VS Code 原型。

M0 完成后必须能够回答：

1. ContextIME 原生主线从哪个 Weasel 版本开始。
2. 使用哪个 librime 版本和提交。
3. 构建需要哪些固定工具。
4. 分发修改版时必须履行哪些许可证义务。
5. 干净 Windows CI 是否能生成可安装的上游安装程序。

## 固定版本

### Weasel

- 仓库：`rime/weasel`
- 版本：`0.17.4`
- 提交：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- 许可证：GPL-3.0
- 用途：Windows TSF 前端、候选窗口、输入服务、设置程序和安装器基线

选择稳定版而不是 Nightly。ContextIME 必须先复现一个已发布、已被用户使用的组合，再评估上游新提交。

### librime

- 仓库：`rime/librime`
- 版本：`1.13.1`
- 提交：`1c23358157934bd6e6d6981f0c0164f05393b497`
- 许可证：BSD-3-Clause
- 用途：拼音组合、输入方案、词典、候选、用户词频和上屏结果

Weasel 0.17 系列已经验证 librime 1.13.1。虽然 librime 已有更新稳定版，但 M0 不允许直接升级。升级必须在原始组合构建和安装成功后单独建立兼容性分支。

## 许可证边界

### 直接 fork Weasel

ContextIME 若修改并分发 Weasel 派生程序：

- 原生前端派生代码按 GPL-3.0 分发；
- 安装包必须附带许可证；
- 必须向接收二进制的用户提供对应源代码或合规的源代码获取方式；
- 修改必须明确标识，不能让问题归因于原上游；
- 第三方组件许可证和声明必须随发行物保留。

因此原生输入法主仓不能继续以当前 MIT 许可覆盖全部代码：

- `native/weasel` 及其派生部分使用 GPL-3.0；
- 独立、未派生的 ContextIME 协议、配置工具和适配器可按各自许可证管理；
- 与 GPL 派生前端静态或紧密组合的代码在发布前必须进行最终许可证审计。

### 使用 librime

librime 使用 BSD-3-Clause。允许修改和二进制分发，但必须保留版权、许可条件和免责声明，且不得使用上游名称为产品背书。

## 构建基线

固定环境：

- GitHub Actions：`windows-2022`
- Visual Studio 2022
- MSVC `v143`
- Windows SDK `10.0.19041.0`
- Boost `1.84.0`
- NSIS
- xmake 最低 `2.9.4`，仅作为第二构建路径

不使用 Weasel 旧工作流中的 `windows-2019`，因为该 GitHub 托管镜像已经退役。

## 构建路径

M0 使用上游已验证的 MSBuild 路线：

```text
checkout Weasel exact commit with submodules
→ verify Weasel and librime SHAs
→ copy env.vs2022.bat to env.bat
→ install/build Boost 1.84.0
→ download librime 1.13.1 development assets explicitly by tag
→ build Rime data
→ build Weasel x64/x86 frontend
→ build NSIS installer
→ archive installer and SHA report
```

`get-rime.ps1` 必须显式传入 `-tag 1.13.1`，禁止使用默认的 latest。

## M0 验收

- [x] Weasel checkout SHA 与锁文件一致。
- [x] librime 子模块 SHA 与锁文件一致。
- [x] 下载的 librime 资产来自 `1.13.1` tag。
- [x] Windows 2022 runner 完成构建。
- [x] 生成 Weasel 安装程序 artifact。
- [x] 记录安装程序 SHA-256。
- [x] 记录构建日志和工具版本。
- [ ] 在真实 Windows 10/11 机器完成安装、输入、卸载验证。

CI 证据见 [`docs/evidence/m0-upstream-build.md`](evidence/m0-upstream-build.md)。最后一项必须在真实桌面会话中完成，GitHub Actions 不能替代。

## M0 不做

- 不替换 Weasel 产品名、图标和 GUID。
- 不修改 TSF 注册逻辑。
- 不接入 ContextIME 策略层。
- 不做代码/注释自动判断。
- 不升级 librime。
- 不发布 ContextIME 安装包。

## M0 通过后的下一步

建立 `native/contextime-weasel` 派生主线，只做最小品牌隔离：

1. 产品名和安装目录。
2. TSF Profile GUID、服务 GUID 和安装注册项。
3. 用户数据目录。
4. 图标和开始菜单入口。
5. 安装、升级、卸载隔离。

品牌隔离完成并验证不会覆盖 Weasel 后，才进入拼音候选和 ContextIME 开发方案验证。
