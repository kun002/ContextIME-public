# M1.1 ContextIME 身份隔离预览构建证据

## 结果

Windows 2022 CI 已从固定上游生成第一个 ContextIME 原生身份隔离预览安装包。

该结果证明：

- 身份补丁能够重复应用到 Weasel 0.17.4；
- 修改后的 TSF 前端能够编译；
- 可生成独立命名的 `contextime.dll/.ime` 系统文件；
- 可生成显示为 ContextIME 的 NSIS 安装程序；
- 安装脚本静态审计未发现小狼毫注册表、卸载键或 Program Files 身份残留。

该结果不证明真机并存、输入和卸载已经通过。

## 固定上游

- Weasel：`0.17.4`
- Weasel commit：`9cc96e20dc71b80876b12f689bb5863c76c2a7ed`
- librime：`1.13.1`
- librime commit：`1c23358157934bd6e6d6981f0c0164f05393b497`

## 工作流

- 工作流：`ContextIME Native Identity Preview`
- Run ID：`29822651474`
- Runner：`windows-2022`
- 结论：success

通过步骤：

1. 精确校验 Weasel 与 librime 提交；
2. 归一化 Weasel 0.17.4 的拆分 Profile GUID 字符串；
3. 应用 ContextIME 产品身份补丁；
4. 执行 `git diff --check`；
5. 构建 Boost、Rime 数据和派生 TSF 前端；
6. 生成 `contextime.dll/.ime` 及 x64 文件；
7. 生成 ContextIME NSIS 安装程序；
8. 执行安装脚本身份隔离审计；
9. 上传安装包、固定 identity、GPL 源代码 patch 和工具链证据。

## 安装包

- 文件：`contextime-0.1.0-preview-installer.exe`
- 大小：`12,048,357` bytes
- SHA-256：`1094513dfd2aac6c8d9d975483d6cb98bedaa1767bc62d55569c571c7f5ae902`

工作流 artifact：

- 名称：`contextime-native-0.1.0-preview`
- Artifact ID：`8492279255`
- ZIP SHA-256：`9aa0e4f102379e36df1970f230488f1cf4ec564eb1f0b9abe4b7bffa5b63a445`

## 已隔离身份

- TSF Text Service GUID；
- TSF Profile GUID；
- 语言栏、显示属性、输入状态和保留键 GUID；
- `contextime.dll`、`contextimex64.dll`；
- `contextime.ime`、`contextimex64.ime`；
- `Software\ContextIME` 注册表；
- ContextIME 独立卸载键；
- `Program Files\ContextIME`；
- `%APPDATA%\ContextIME`；
- `%TEMP%\contextime`；
- IPC 窗口、命名管道、部署互斥锁和服务名；
- 上游自动更新通道已禁用。

## 随 artifact 提供的源代码证据

- `native/contextime.identity.json`；
- `contextime-weasel-identity.patch`；
- `contextime-identity-report.json`；
- `contextime-upstream-commits.txt`；
- `contextime-toolchain.txt`；
- GPL-3.0 许可证和派生说明。

## 未验证边界

必须在装有小狼毫的真实 Windows 桌面完成：

1. ContextIME 安装时不覆盖或升级小狼毫；
2. 输入法列表同时出现小狼毫和 ContextIME；
3. ContextIME 能激活、拼音组合、显示候选并上屏；
4. 两个输入法不串 IPC、候选和用户数据；
5. 卸载 ContextIME 后小狼毫仍然正常；
6. 小狼毫的系统文件、注册表和 `%APPDATA%\Rime` 未被修改或删除。

完成以上验收前，本产物称为“ContextIME 0.1.0 身份隔离预览”，不称为稳定版。
