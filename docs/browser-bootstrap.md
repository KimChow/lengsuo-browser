# Browser bootstrap 与 Touch 首批接入

状态：补丁已生成，固定源码临时副本 apply-check 通过；尚未通过
Chromium GN、编译、Windows 或浏览器运行验收，不可启用为完整指纹产品。

基线：`0e66b94549a8bd85f03fbdf189b752d6e7b63c32`（Chromium 155）。
本批只接入无凭据 LBPF1 早期加载和 Touch WebPreferences。
Screen、DPR、UA/UA-CH、GPU、字体、Canvas、Audio 等尚未原生接入。

## 源码核对与补丁

- `chrome/browser/chrome_browser_main.cc` 的
  `ChromeBrowserMainParts::PreEarlyInitialization` 在 extra parts 和
  BrowserProcess 创建前加载一次；错误返回 `CHROME_RESULT_CODE_INVALID_CMDLINE_URL`。
- `chrome/browser/chrome_content_browser_client.cc` 的
  `OverrideWebPreferences` 在 extra parts 后应用 Touch；
  `OverrideWebPreferencesAfterNavigation` 同样重新应用并报告是否发生改变。
- `chrome/browser/BUILD.gn` 的 `source_set("core")` 依赖
  `//lengsuo/integration:bootstrap`，不是仅向 header target 接依赖。
- Bootstrap 只依赖 base、native decoder、content public/common 和 Blink
  common，没有反向 chrome/browser 依赖。Mojom 经 content public/common
  暴露，遵循固定源码 mojom/BUILD.gn 的外部依赖要求。

Overlay 将 `integration/` 与原有 `native/` 放到源码根 `lengsuo/` 下。
按文件名顺序应用 `patches/*.patch`；补丁验证必须针对固定 revision。
本批未修改或应用到正在同步 DEPS 的 Chromium 源码树。

## 加载契约

Opt-in 参数：`--lengsuo-profile-wire=<absolute-local-file>`。
没有参数时不读取文件、不覆盖上游 Touch 行为。
有参数但空路径、相对路径、父目录引用、网络路径、不可读、空文件、
超过 4096 字节或 LBPF1 解码失败均拒绝启动，不修补或随机回退。
Windows 只接受本地 fixed drive 的普通盘符路径，拒绝 UNC、设备路径及
映射网络盘；文件末级 symlink 拒绝。未支持 legacy App-V 间接启动。

运行器必须先建立网络阻断，并把无凭据画像写入受控本地目录，防止文件
替换、父目录 junction/symlink 重定向、网络挂载和权限变化。
此阶段不验证所有文件系统来源，不提供文件读取墙钟超时。
有大小上限的读取不等于超时保证。画像中不含 Cookie、密码、token。
签名授权、catalog 解析/成员验证与环境网络隔离仍由生产运行器负责。
本加载器不是权限或真实性校验，也不证明全部早期网络尚未启动。

画像状态只初始化一次；失败也不能在同进程内重试替换。对外只暴露 const
画像，不向 renderer 传递文件或读取接口。Touch 通过现有 WebPreferences
和上游 IPC 传播；不修改 `NavigatorEvents::maxTouchPoints` getter。

Touch=0 使用 fine pointer、hover；Touch>0 使用 coarse pointer、no hover。
同步 max_touch_points、touch feature detection、primary/available pointer
与 hover 六个字段。该单一设备模型不支持混合鼠标+触摸画像；未验证真实
输入事件、DevTools emulation、OOPIF、BFCache、prerender 等运行期行为。

## Singleton 边界

Singleton 在 `chrome/app/chrome_main_delegate.cc` 中获得，早于上述加载。
因此 `ChromeProcessSingleton::NotifyOtherProcessOrCreate` 对带参数的启动
使用上游 `ProcessSingleton::Create()`；失败返回 PROFILE_IN_USE，不转发
新命令行，不进入 Notify 的旧进程恢复路径。旧锁也会拒绝，需明确诊断，
本批不清锁、不终止进程。Windows App-V relaunch 分支提前拒绝。

已有受管实例的 NotificationCallback 消费并丢弃所有二次请求，记录无画像
内容的错误，保持原画像不变。消费返回 true 是为避免发送方误判进程挂起；
普通发送方可能显示上游“使用现有进程”退出文本，但请求没有被执行。
这不是完整的跨进程错误回传协议。后续须专项验证 Windows/Linux 同画像、
异画像、无画像发送方、并发启动与旧锁情况，不能声称已完成 singleton 验收。

## 验证证据与待执行

隔离目录：`/home/ubuntu/lengsuo-browser-dev/integration-review`。
审查日志目录：`/home/ubuntu/lengsuo-browser-dev/reviews`。

- standalone 状态用例覆盖 opt-out 不读、非法路径不读、读取失败、空、
  超长、非法 wire、成功后不可变、失败后不可替换；普通编译及 ASan/UBSan通过。
- 新接口缺失实现时 link red 已记录；该 red 只证明缺符号，不是浏览器行为复现。
- 四个 patch 在独立固定源码副本 `git apply --check` 和 `git diff --check` 通过。
- Chromium base 单测已编写，尚未编译或运行；不把 standalone 代替 base/API 验收。

DEPS 就绪后，overlay 准备并应用后执行（本批未执行）：

```text
gn gen out/lengsuo
gn check out/lengsuo //lengsuo/integration:bootstrap
autoninja -C out/lengsuo bootstrap_unittests chrome
out/lengsuo/bootstrap_unittests
```

之后必须运行真实浏览器矩阵、Windows 安装/签名验证。所有待执行项均不以
补丁能应用、接口存在或原生状态单测通过替代。
