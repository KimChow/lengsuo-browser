# 第四批：画像语言的基础接入

状态：基础接入已在Linux与Windows通过真实运行（2026-10-17），两平台
9条用例全部通过；尚不能视为完整语言/时区能力。

## 接口与兼容范围

复用 LBPF1 的单个 `language` 标签，不新增 wire 字段或命令行参数。
有画像时读取冻结值，无画像时保持上游路径。与 UA catalog 是否提供无关。
不修改宿主系统语言，也不把浏览器 UI 资源包替换当作画像接口。
客户端已接受的标签仍只表示语法校验，不证明 Chromium/ICU支持所有标签。

## 固定155源码出口（0007）

| 出口 | 目的 |
| --- | --- |
| renderer_preferences_util 的 GetLanguageListForProfile | 页面语言与语言列表 |
| ChromeContentBrowserClient::GetAcceptLangs | Worker等上游消费通道 |
| ChromeContentBrowserClient::GetApplicationLocale | renderer的 --lang 与默认 Intl locale |
| AppendExtraCommandLineSwitches → RendererMain | 向renderer传冻结语言，初始化ICU默认locale |
| ProfileNetworkContextService::ComputeAcceptLanguage | HTTP语言头，复用上游展开与权重生成 |
| ChromeContentBrowserClient::ShouldReduceAcceptLanguage | 受管画像不从宿主偏好协商改写语言 |

`chrome/browser/net:impl` 显式依赖 bootstrap，避免只在编译物理头文件可见时
偶然编过。没有画像时保留上游语言协商、用户偏好和网络上下文行为。
画像只携带单一语言；HTTP中的基础语言回退和q权重由上游 HttpUtil生成，
例如 `fr-FR,fr;q=0.9`，不手写协商器。

## 测试先行与发现

1. 合成画像将 en-US 改为等长 fr-FR，UA/catalog保持不变。
   Windows原浏览器实际返回zh-CN，语言断言失败。
2. 初始出口覆盖使Linux页面和Dedicated Worker变为fr-FR，但请求头仍为
   en-US；真实请求头断言失败，补齐语言协商门禁后请求头变为fr-FR。
3. 补测Intl默认locale，Windows仍返回zh-CN；增加renderer启动locale出口。
   Linux实测证明仅传 --lang 仍不足以更新zygote继承的ICU状态，因此增加
   renderer早期初始化，须在V8 isolate创建前设置ICU默认locale。

内部子进程参数 `--lengsuo-managed-language=<tag>` 只由已加载画像的browser
向renderer追加，不携带wire路径、值池或凭据，不代表授权或网络隔离。
RendererMain 在初始化Skia/V8之前设置ICU；未传该参数时保持上游路径。
浏览器UI资源包和原有 --lang 传播仍走上游通道，不在JS层重写Intl对象。

复现命令（每次使用新的仓外目录）：

```text
python tests/browser_ua_smoke.py --check-language --chrome /absolute/path/to/chrome --repo /absolute/path/to/lengsuo-browser --work /absolute/path/to/new-test-directory
```

断言主页面和Dedicated Worker的 language、languages、Intl.DateTimeFormat
默认locale，并比对实际HTTP Accept-Language。完整画像、仅画像、无参数及
catalog的错误输入路径与第三批共用9条用例，避免以JS单面结果替代一致性。

Windows红相收据：`D:/lengsuo-build/logs/language-red.log`、
`language-locale-red.log`；构建收据使用 `logs/language-win-chrome-build.log`。
Linux真实运行收据放在1.47仓外 `reviews/language-run*/`。

Windows最终收据 `D:/lengsuo-build/language-win/summary.json`：9条全部通过，
受管页面和Worker的language/Intl locale均为fr-FR，请求头为fr-FR,fr;q=0.9；
无画像时仍为宿主zh-CN。构建退出0，完整运行日志 `logs/language-win-smoke.log`。

Linux最终收据 `reviews/language-run4/summary.json`：9条全部通过，
主页面与Worker均为 language=fr-FR、languages=[fr-FR]、Intl locale=fr-FR，
实际Accept-Language=fr-FR,fr;q=0.9。仅画像、无catalog时同样成立。
1.47支持脚本39项全部通过；GN的renderer、chrome:core和net:impl头依赖
检查均通过。固定基线顺序应用0002/0006/0007补丁，精确重建全部5个修改文件。

## 尚缺验收

- 全部允许标签与ICU支持关系、别名和不支持标签的处理，不宣称任意BCP47标签可用。
- Service Worker、Shared Worker、OOPIF、BFCache、prerender与恢复矩阵。
- 日期/数字格式专项、翻译/拼写/浏览器UI语言及扩展可观察行为。
- 时区及夏令时；本批只处理语言与默认locale，未覆盖画像timezone。
