# Cookie / LocalStorage 目录隔离回归（2026-10-17）

状态：Linux 1.47 与 Windows 本机真实 Chromium 各 10/10，通过且退出 0。
本批补充测试与修复测试的平台路径探测，不新增产品运行器隔离能力。

脚本 `tests/browser_storage_isolation_smoke.py` 只使用合成 Cookie、
LocalStorage 和一个持续运行的 loopback HTTP origin。各画像目录独立：

- A 写 alpha，B 初始不可见；B 写 beta 后，A 重启仍为 alpha。
- A/B 分别重启保留各自数据；清理 A 后重启为空，B 的 beta 不受影响。
- 独立 C 写 gamma 并跨进程重启读回，作为持久化正向对照。

每次实际 Chrome 调用均有超时，脚本只管理自己的测试进程与服务。
测试目录必须新建，拒绝复用已有 A/B 画像。没有真实凭据或外部流量。

## Windows 红相与修复

首跑退出 1：Web 层数据正确，但脚本只检查 `Default/Cookies`，因此磁盘
Cookie 路径断言失败。实读确认 Windows 使用 `Default/Network/Cookies`。
修复同时识别两种实际路径，Cookie/LocalStorage 的隔离等值断言保持不变。
LocalStorage 同时识别 leveldb 与 SQLite 后端。Windows 测试不添加
`--no-sandbox`；Linux 保留既有研究机测试启动方式。

## 复现与回执

```text
python tests/browser_storage_isolation_smoke.py \
  --chrome /absolute/path/to/chrome --work /new/test/directory --timeout 45
```

脚本 SHA256：
`8d2827d392c7d4e93afcb66749878af59504ac8393a93fe16034f54e0ee254a0`。

| 平台 | 最终目录 | 结果 |
| --- | --- | --- |
| Linux | `/home/ubuntu/lengsuo-browser-dev/reviews/storage-isolation-final-20261017` | `passed: 10`，退出 0 |
| Windows | `D:/lengsuo-build/storage-isolation-win-run2-20261017` | `passed: 10`，退出 0 |

各目录 `summary.json` 保留逐项观测；同名外部 `.log` 以
`STORAGE_ISOLATION_OK 10 cases` 收尾。Windows 首次失败日志位于
`D:/lengsuo-build/storage-isolation-win-20261017.log`。原始日志、画像和数据库不入库。

## 未覆盖

不是产品级环境隔离证明：尚未覆盖运行器环境绑定、TUN/出口、缓存、
扩展、IndexedDB、ServiceWorker、CacheStorage、多租户权限和数据加密。
Cookie 生命周期产品策略与凭证托管也不因本测试通过而视为完成。
