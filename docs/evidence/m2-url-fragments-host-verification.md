# M2.3 常用 URL 片段宿主机验证证据

## 产品结果

ContextIME `0.2.1-preview` 已经通过继承的 librime URL recognizer 支持常用小写 HTTP/HTTPS、`www` 和小写裸域名输入。URL 原样上屏后，同一 TSF 会话继续输入简体中文和 Shift 英文：

```text
https://github.com输入法abc
https://github.com/kun002/ContextIME输入法abc
http://localhost:3000/api输入法abc
www.example.com/docs输入法abc
github.com/kun002/ContextIME输入法abc
https://example.com/api?q=ContextIME&mode=1#readme输入法abc
```

## 复用路线

`contextime_developer` 通过：

```yaml
__include: luna_pinyin_simp.schema:/
```

继承成熟的 librime URL 识别。最终用户编译 schema 中存在：

```yaml
url: "^(www[.]|https?:|ftp[.:]|mailto:|file:).*$|^[a-z]+[.].+$"
```

因此 M2.3 没有新增 ContextIME 私有 URL parser、translator、TSF 逻辑或按键热路径代码。重新实现同一规则会制造第二个 owner，并增加与 librime 行为漂移的风险；本轮通过真实输入验收现有成熟实现。

影响链路为：

```text
Key event
→ ContextIME TSF / IPC / Server
→ inherited librime url recognizer
→ composition / raw URL candidate
→ commit
```

## 固定测试身份

- 已安装版本：`ContextIME 0.2.1 Preview`
- 安装程序：`contextime-0.2.1-preview-installer.exe`
- 文件大小：`12,050,362` bytes
- SHA-256：`49b856a9c6ed5814e14c810d33ab70d67caea619bb359f1df89f8005b2a4f689`
- Build commit：`f82e0788854841df6ee1d34cce6835735ad34429`
- CI run：`33364340995`
- Windows：Windows 10 Pro，DisplayVersion `25H2`，Build `26200.9168`
- Smoke schema：`contextime.tsf-smoke.v4`
- Authenticode：`NotSigned`

测试发生在已登录、解锁的主开发机交互桌面 Session 1。实验 `contextime_developer.custom.yaml` 不存在，结果来自 `0.2.1-preview` 安装包和 librime 继承方案，不是本机临时规则。

## 六条 URL 真机结果

每条均执行：

```text
URL
→ Space 原样上屏
→ shurufa
→ Space 上屏“输入法”
→ Shift
→ abc 英文直输
```

| 输入 | URL commit | 最终文本 | 候选变化像素 | 结果 |
|---|---|---|---:|---|
| `https://github.com` | `https://github.com` | `https://github.com输入法abc` | `6,670` | `Passed: true` |
| `https://github.com/kun002/ContextIME` | `https://github.com/kun002/ContextIME` | `https://github.com/kun002/ContextIME输入法abc` | `7,732` | `Passed: true` |
| `http://localhost:3000/api` | `http://localhost:3000/api` | `http://localhost:3000/api输入法abc` | `8,578` | `Passed: true` |
| `www.example.com/docs` | `www.example.com/docs` | `www.example.com/docs输入法abc` | `8,434` | `Passed: true` |
| `github.com/kun002/ContextIME` | `github.com/kun002/ContextIME` | `github.com/kun002/ContextIME输入法abc` | `10,954` | `Passed: true` |
| `https://example.com/api?q=ContextIME&mode=1#readme` | 原样 | `https://example.com/api?q=ContextIME&mode=1#readme输入法abc` | `16,833` | `Passed: true` |

六条 JSON 与候选截图位于：

```text
artifacts/native-evidence/m2-url-0.2.1-baseline/
```

每条均记录 `FollowupMatched: true`、`EnglishModeMatched: true`、`CapsLockRestored: true` 和 `Passed: true`。

## 明确边界和失败证据

本轮不验收：

```text
localhost:3000
127.0.0.1:3000
HTTPS://EXAMPLE.COM
含空格 URL
```

真实探测结果：

边界探测刻意不设置 `ExpectedText`，只用于记录实际 commit；因此这里以 `CommittedText` 判断 URL 是否保持原样，不能把诊断 JSON 的通用 `Passed` 字段单独当作 URL 正确性结论。

| 输入 | 实际提交 | 结论 |
|---|---|---|
| `localhost:3000` | `咯嚓落后身体：3000 ` | 无 scheme，进入拼音和中文标点链路 |
| `127.0.0.1:3000` | `127.0.0.1:3000 ` | 没有 URL composition，并额外提交空格 |
| `HTTPS://EXAMPLE.COM` | `HTTPS：/EXAMPLE。COM` | 全大写 scheme 不命中继承的 lowercase URL pattern |

失败 JSON 位于：

```text
artifacts/native-evidence/m2-url-0.2.1-boundary-probes/
```

虽然继承 pattern 还声明 `ftp`、`mailto` 和 `file`，本轮没有用真实 TSF 用例验证这些 scheme，因此不把它们列入 M2.3 已验收范围。

## 验证状态

| 能力 | 状态 | 证据边界 |
|---|---|---|
| librime URL recognizer | `implemented` | 成熟上游能力，由 `contextime_developer` 继承 |
| 0.2.1 Preview 包和编译 schema | `statically_verified` | 编译 schema 含继承 URL pattern，实验 custom 不存在 |
| 六类常用 URL、后续中文和 Shift 英文 | `real_machine_verified` | Windows Build `26200.9168` 主开发机 |
| 无 scheme localhost/IP 和 uppercase scheme | `unsupported` | 已保留失败 JSON，不虚报完成 |
| `ftp`、`mailto`、`file` | `unverified` | pattern 存在，但没有真实 TSF 验收 |
| 干净 Windows 10/11 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 当前主机有开发和安装历史 |
| 独立 LAN 笔记本 | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未连接、未触发 |
| RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | 本轮未执行 |

## M2.3 结论与下一项

M2.3 只关闭上述有限 URL 范围。无 scheme localhost/IP、uppercase scheme、含空格 URL 和未真机验证的其他 scheme 仍保持边界；没有开始命令识别、Context Engine、编辑器 Adapter、项目词库或个性化。

下一项是 M2.4 命令片段识别。M2.3 只新增验收证据和 roadmap 状态，不需要发布重复的输入法安装包。
