# OpenClash DNS 守护与 cf-ip 免重启：现状、方案与真实进度

- 整理日期：2026-11-20
- 执行计划修订：2026-11-20，范围扩展至 PassWall、HomeProxy 和 UPnP
- 合并来源：同目录 `spec.md`（2026-11-13 取证与方案）、`tasks.md`（执行前任务快照），以及 2026-11-13 至 11-20 的实际改动记录
- 适用设备：OpenWrt 25.12.5 x86/64（192.168.2.1），`luci-app-openclash 0.47.156`
- 标记约定：
  - **【实测】**：在设备或本地环境实际跑过、有日志/命令输出为证
  - **【设计结论】**：基于实测事实推导的方案判断，尚未落地
  - **【未验证】**：只有文档/源码依据，没有在设备上验证
  - **【未执行】**：计划里列了，但没有任何动作

本文不是对原计划的“完成版”，而是把“原本打算做什么”“实际做了什么”“还欠什么”放在一起的单一事实来源。
原始 `spec.md`、`tasks.md` 保留不动，作为历史快照；两者冲突时以本文为准。

---

## 1. 背景与目标

### 1.1 两个东西的关系

- **cf-ip**（`/usr/bin/cf-ip-auto`，包 `luci-app-cloudflare-ip 2.6.0-r12`，`script_version 2.6.0`）：定时（cron `30 7,18,23 * * *`）测速优选 Cloudflare IP，然后把优选结果写进 OpenClash 的节点配置。
- **OpenClash**：代理栈本体，主配置 `/etc/openclash/config/yunshu.yaml`，内核 mihomo（`alpha-smart-g2e80ed7`，with_gvisor），DNS 入口 653，控制器 `127.0.0.1:9090`。
- **dns-guard**（`/usr/sbin/openclash-dns-guard.sh` + `/etc/init.d/openclash-dns-guard`）：独立于 OpenClash 的 procd 服务（`START=96`），每 15s 用 `nslookup github.com` 打 653/953 判断 DNS 是否可用；653 连续失败则把 dnsmasq 上游切到 mosdns(953)，必要时再退 WAN，恢复后切回 653。

【实测】三者串起来的故障链是：cf-ip 每次运行都会先停掉 OpenClash 去测速 → 653 消失约 2 分钟 → dns-guard 判定“DNS 故障”并降级、发 Bark → OpenClash 回来后 dns-guard 恢复、再发一条 Bark。一次 cf-ip 运行稳定产生 2 条无意义推送、2 次 dnsmasq 重启、1 次策略路由拆除/恢复。

### 1.2 为什么要做免重启

原流程里 cf-ip 应用新 IP 的方式是“停 OpenClash → 改配置 → 起 OpenClash”，应用本身不到 1 秒，却要付出整段服务中断。目标是把“应用”这一步从全量停启改成 mihomo 热刷新：不动主配置、不重建 TUN、不动 fw4 与 DNS。

需要提前说清的边界：**免重启只解决“应用”这一步，不解决“测速窗口”。** 见 §2.3。

---

## 2. 已有实测与待验证事项

### 2.1 cf-ip 实际写到哪里

| 项 | 实测值 |
|---|---|
| 写入目标 | `/yunshu/clash/clash-proxies.yaml`（来自 `uci cf_ip.openclash.config`） |
| 主配置 | `/etc/openclash/config/yunshu.yaml`（`openclash.config.config_path`）；不存在 `/etc/openclash/config/config.yaml` |
| 主配置已有 provider | `订阅` → `./proxy_provider/subconverter.yaml`；`本地节点` → `./proxy_provider/clash-proxies.yaml` |
| 代理组引用 | `自建节点` 同时有 `proxies: [自用cf负载均衡]` 与 `use: [本地节点]`；`自用cf负载均衡` 为 `load-balance` + `use: [本地节点]` + `filter: '[CF-'` |
| 节点名 | `🇺🇸美国-自建1-VLESS-xhttp [CF-1]`、`[CF-2]`…；cf-ip 只改 `server:`，节点名不变 |
| 控制器 | `external-controller: :9090`，`openclash.config.cn_port=9090`，secret 取 `openclash.config.dashboard_password` |
| provider 查询 | `GET /providers/proxies/本地节点` → 200，`type=Proxy`，`vehicleType=File` |

结论【实测】：cf-ip 写的那份文件本身就是一个 **file 型 proxy-provider**，不需要再“把节点搬进 provider”，改造点只在“让内核重读”。

### 2.2 热刷新走哪个接口

【实测】`GET /providers/proxies/本地节点` 在设备上返回 200，说明 provider 与控制器都正常。
【未验证】`PUT /providers/proxies/本地节点` 让 mihomo 重读文件，只有 mihomo 文档与源码依据，**设备上没有做过写操作验证**；写入后是否 204、节点 `server` 是否立即变化、TUN 是否真的不重建，都要等实测。

### 2.3 当前测速路径依赖停服

【实测】一次 cf-ip 运行的时间去向（`/tmp/cf_ip/run-1794526201-16029-3324`）：

```
07:32:54 stopping service for speedtest: openclash
07:33:05 running speedtest with /usr/bin/cfst/ip.txt
07:35:14 verifying selected IPs
07:35:18 updating OpenClash config
07:35:18 restarting service: openclash
07:35:26 done
```

约 129 秒花在 cfst 测速，期间 OpenClash 处于停止；写配置 <1 秒，重启 8 秒，全程 128–141 秒。停止的目的是让测速不被 TUN 劫持，是测速正确性的前提。

所以：即使热刷新上线，cf-ip 每次运行仍会让 653 消失约 2 分钟，dns-guard 仍然会降级、仍然会通知。在线测速隔离验证与 dns-guard 的维护租约、告警和降级策略是独立任务，历史进度见 §5。

### 2.4 dns-guard 的既有缺陷（2026-11-13 实测）

- 探测：`nslookup -port=<p> -timeout=2 github.com 127.0.0.1`，15s 一轮，判定的是解析能力，不是进程存活。
- 触发：`FAIL_CNT >= 2`（约 30s）且距开机超过 90s。**90s 只是开机宽限，不是“持续不可用”阈值**；原规格把两者混为一谈。
- 降级动作：`server` 改成 `127.0.0.1#953`、`noresolv=1`、`commit`、`dnsmasq restart`；再不通退 WAN。
- 策略路由：`ip rule` fwmark `0x162` → table `354`，pref `1888`；fallback/wan 下必须不存在，primary 下必须存在，`repair_if_reverted` 每轮校正。
- 恢复：原脚本硬编码写回 `127.0.0.1#653`，记录的 `ORIG_SERVER/ORIG_NORESOLV/ORIG_RESOLVFILE` 从未用于还原，也没有回读校验。
- watchdog 拉锯：`enable_redirect_dns=1`，上游 `/usr/share/openclash/openclash_watchdog.sh` 每 60s 执行 “Force Reset DNS Hijack”，把上游强写回 653 并 restart dnsmasq；原 `suspend_watchdog` 用 SIGTERM，实际没停住，降级期间双方来回改。
- 通知：Bark（`https://api.day.app/<key>`），失败入队、恢复后补发；原脚本把 key 明文写在脚本里；日志固定落 `/yunshu/openwrt-maintenance/`，外接盘不在时静默失败。

---

## 3. 修订后的方案设计

### 3.1 热刷新（【设计结论】，未落地）

1. cf-ip 照旧写 `/yunshu/clash/clash-proxies.yaml`（节点名保持稳定）。
2. 读 `openclash.config.cn_port` 与 `dashboard_password`，调用
   `PUT http://127.0.0.1:<cn_port>/providers/proxies/本地节点`（Bearer secret），期望 204。
3. 读取 provider 与节点状态，以当前版本实际返回的字段为准。用隔离的真实新连接验证出站目的地址已切换到候选 IP。
4. 比对核心 PID、启动时间及 TUN、fw4、DNS 状态。另测已有连接，单独记录是否中断。

原规格曾要求“一次性改造：主配置加 provider、组加 use”；【实测】设备上这两项**已经存在**，所以本机不需要再做这一步。换设备时仍需检查。

### 3.2 应用失败与恢复

发布前识别运行中实例、控制器和目标文件对应的 file provider，解析实际路径并核对组引用。对歧义、订阅漂移、用户编辑和并发冲突拒绝写入。保留现有锁、备份、YAML 校验、原子发布和恢复日志。相同候选直接跳过。

PUT 超时、拒绝或回读不一致时，先检查内核实际加载情况及文件是否已被其他写入者修改。仍持有本次写入所有权时恢复旧文件，沿同一 provider 接口热回滚并验证旧出站。无法证实恢复时保留事务证据并报告人工处理。自动热应用及其失败恢复禁止硬重启。显式手动重启独立记录。测速阶段若曾停服，恢复原有服务按维护流程记录，本次完整运行不能标为免停机。

### 3.3 在线测速与 DNS 守护

先用已验证候选 IP 独立验收应用阶段。在线测速另外验证 CFST 和候选探测在代理运行时能否避开 TUN、透明转发与 DNS 干扰，并与直连基线对照。无法证明等价时，将原有停服测速保留为显式维护路径。

仍需停服时，继续探测 DNS，并用绑定事务的有界维护租约区分计划内中断。记录持续失败时间，将告警和降级拆开，按故障事件去重。阈值根据实测停机时长和可接受的 DNS 故障窗口确定。维护超期、持有进程退出或设备重启时租约失效。真实 DNS 故障必须有明确的最晚降级时间。沿用已部署的原值回填、回读及 watchdog 释放机制，验证维护中故障、异常退出、重启和通知去重。

## 4. 风险

### 4.1 订阅覆盖 provider（结论：成立、可控，但未实测）

- 【实测】订阅更新由 cron `0 7 * * * /usr/share/openclash/openclash.sh` 执行，用 `mv` **整体替换** `yunshu.yaml`；订阅源是 `https://cdn.jsdelivr.net/gh/hello-yunshu/selfuse@master/openwrt/yunshu.yaml`。
- 【实测】该源模板自身包含 `proxy-providers`（含 `本地节点`）与相关代理组，所以整体替换后 provider 段仍然存在。
- 【设计结论】风险“成立但可控”：只要模板不删掉 provider 段，订阅更新不会破坏热刷新；模板变更后，应在发布候选前发现 provider 或组引用缺失并报告配置漂移。
- **【未验证】原计划 T2.6——“验证 07:00 订阅更新是否覆盖 provider 段”——没有执行。** 也就是说“模板自带该段”是按模板内容读出来的结论，不是跑完一次订阅更新后的实测结果。
- 目前**没有自动校验**：没有脚本在订阅更新后检查 provider 段是否还在，也没有告警。

### 4.2 其他风险

- 【未验证】`PUT /providers/proxies/...` 是本方案的核心动作，但设备上只验证过同路径的 GET。
- provider 名称 `本地节点`、`cn_port`、`dashboard_password` 都是运行期依赖；名称或凭据变化可能导致热刷新失败。按运行态重新识别，凭据只在本机使用并从日志中脱敏。
- 控制器只监听本机，任何热刷新脚本必须在本机执行，不能从外部直接调用。
- dns-guard 与上游 watchdog 的冲突已通过 SIGSTOP/SIGCONT 处理（见 §5 Phase 1 的“计划外已落地”），但如果 OpenClash 升级改了 watchdog 行为，需要重新确认。

### 4.3 回退

- dns-guard：设备上保留了 `cp -a` 备份，`cp -a <bak> /usr/sbin/openclash-dns-guard.sh` 即可回退；配置项缺失时脚本用默认值。
- 热刷新尚未落地。上线后可关闭自动热应用并保留已有 `.bak.<ts>`。需要恢复文件时，按 §3.2 验证旧运行态。
- 全程不刷机、不推送、不合并；未批准不动设备。

---

## 5. 分阶段任务与真实状态

本节记录 2026-11-20 的历史进度，表中的旧任务名与回落措辞保留作追溯。状态用三种：**完成 / 部分 / 未做**。原 `tasks.md` 的勾选是执行前快照，下面是核过的真实状态。

### Phase 0 — 取证（完成）

| 任务 | 状态 | 说明 |
|---|---|---|
| T0.1 恢复设备 SSH | 完成 | — |
| T0.2 取原文三件套 | 完成 | 脚本 373 行，sha256 `faf8bc1a…1072` |
| T0.3 确认通知通道 | 完成 | Bark，失败入队、恢复补发 |
| T0.4 确认探测方式 | 完成 | `nslookup` 打 653/953，非进程检测 |
| T0.5 确认降级实现 | 完成 | dnsmasq 上游切换 + `ip rule` 增删 |
| T0.6 确认 procd | 完成 | `respawn 0 5 0`、`S96`/`K10` |
| T0.7 日志取证 | 完成 | 7 次降级，每次 52–101s，各 2 条 Bark |
| T0.8 发现自身缺陷 | 完成 | watchdog 无效、`ORIG_*` 未用、无防抖 |

### Phase 1 — 通知与降级改造

原计划（ALERT 阶段 / 90s 告警 / 180s 降级 / 维护锁 / incident 去重 / 冷却）**没有按原样执行**，实际走的是“先修脚本自身缺陷”的路线。

| 任务 | 状态 | 说明 |
|---|---|---|
| T1.1 备份脚本 | 完成 | `/root/openclash-dns-guard.sh.bak-20261113-113919`（原始 `faf8bc1a…`）、`/root/openclash-dns-guard.sh.bak-20261113-114454`（中间版） |
| T1.2 配置增加 `alert_after/degrade_after/lock_ttl/notify_cooldown` | 未做 | 配置未加任何新键 |
| T1.3 状态文件增加 `ALERT/FIRST_FAIL/INCIDENT_ID/LAST_NOTIFY` | 未做 | 状态文件只新增了 `WD_STOPPED` |
| T1.4 触发条件改“持续时长” | 未做 | 仍是连续 2 次失败 + 90s 开机宽限 |
| T1.5 ALERT 阶段 | 未做 | 仍只有 primary/fallback/wan 三态 |
| T1.6 降级阈值 180s + 维护锁 | 未做 | 阈值未改，没有维护锁 |
| T1.7 `notify_once` 去重/冷却/重试 | 未做 | 仍是每次翻转各发一条 |
| T1.8 恢复按 `ORIG_*` 回填 + reload + 回读校验 | 完成 | 原值缺失或非法时明确失败、保持降级；`dnsmasq restart` 改 `reload`；最多 3 次回读比对 |
| T1.9 `repair_if_reverted` 日志节流 | 未做 | — |
| T1.10 cf-ip 事务接锁 | 未做 | — |
| T1.11 `sh -n` + `/tmp` 干跑 V2–V7 | 部分 | `sh -n`、shellcheck 0 告警、本地桩环境 A–F 场景通过；V2–V7 那套 90/180 阈值干跑**没有做**，因为阈值没改 |
| T1.12 输出 diff 与干跑日志后落设备 | 完成 | 用户确认清单后部署，观察窗口 ≥90s 无 `SWITCH` |

**计划外但已落地（并已进 YunWrt）**：

- watchdog 从 SIGTERM 改为 `SIGSTOP`/`SIGCONT`：降级期间冻结上游 watchdog，正常恢复、guard 收到 TERM/INT、被 procd 重拉三种情况都释放；被冻结 pid 记入状态文件 `WD_STOPPED`，新进程启动时若当前不在降级态，会先按 `/proc/<pid>/cmdline` 校验再放行，防止 pid 复用误杀。
- 主循环 `sleep` 改为可中断（`sleep & wait` + `trap` 清理），避免 procd `term_timeout` 到点发 SIGKILL、trap 来不及释放 watchdog。
- Bark key 改从 uci 读取，缺失时明确报错、不入队。
- 日志落盘增加回退链：`/yunshu/openwrt-maintenance` → `/var/log` → `/tmp`，启动日志记录实际路径。
- 部署结果【实测】：设备脚本 sha256 `ca84744e…`，仓库副本一致；`/etc/init.d/openclash-dns-guard` 与设备逐字节相同；`/etc/config/openclash-dns-guard` **未覆盖**（设备有真实 `bark_key`，仓库默认值为空）。
- 仓库化【实测】：YunWrt 提交 `b1e417c`（脚本 + init.d + 默认配置 + `.meta.json` + uci-defaults 自启 + 构建门禁）与 `43526bd`（构建期/产物侧断言）；两者已推送，远端 tip 为 `43526bd`；该次 push 的 `build` 与 `secret-scan` 均成功，`b1e417c` 因不是 push tip 没有单独 CI 记录。

### Phase 2 — cf-ip 免重启（未动）

| 任务 | 状态 |
|---|---|
| T2.1 决策记录 D11（含 `PUT ?force=false` 证伪） | 未做（YunWrt 现有 D11 是内核 partsize，与此无关） |
| T2.2 `cf-ip-auto-legacy` 增加 `--render-provider` | 未做 |
| T2.3 `openclash-transform.sh` provider 分支 + 回落 | 未做 |
| T2.4 `common.sh` 增加 `cfip_openclash_apply_provider` | 未做 |
| T2.5 本地 docker mihomo 验证 provider 刷新、TUN 未重建 | 未做 |
| T2.6 验证 07:00 订阅更新是否覆盖 provider 段 | 未做 |
| T2.7 同步非 r11 副本 + 契约测试 | 未做 |

### Phase 3 — 交付（未动）

| 任务 | 状态 | 说明 |
|---|---|---|
| T3.1 汇总报告 | 未做 | 本文是背景整理，不是计划要求的交付报告 |
| T3.2 遵守“不刷机、不推送、不合并” | — | 本轮无设备与仓库动作 |

---

## 6. Goal 执行计划

1. **确认基线。** 读取 CFI、UPnP 和相关 YunWrt 仓库的工作树状态、当前入口与已有测试。历史设备版本仅用于复测，路径、provider、端口和实例均按运行态识别。记录 provider 局部刷新和全配置重载的适用范围及证据，避免复用 YunWrt 已有的 D11 编号。
2. **完成 CFI OpenClash。** 先在匹配版本的隔离环境验证 provider PUT，再接入实际配置转换和事务入口。覆盖打包版、仍维护的独立版、候选无变化、订阅实际更新、凭据变化、超时后可能已经生效、并发改写及热回滚。以 §3.1–3.2 为验收合同。
3. **推进在线测速与 DNS 守护。** 按 §3.3 独立验证在线测速和维护路径。核对 YunWrt 中既有 dns-guard 改动。记录 DNS 切换、dnsmasq 操作和通知次数。分别报告应用无硬重启、完整任务无停机和已有连接零中断的结果。
4. **扩展 CFI 后端。** PassWall 复用现有 UCI 字段所有权与事务恢复，分别验证活跃节点、分流引用节点和未使用节点。HomeProxy 追踪节点字段、生成的 sing-box 配置及活跃出站，验证版本与运行模式支持的入口。检查 reload 是否实际替换实例或全量重建防火墙。PassWall2 独立判定。
5. **扩展 UPnP。** relay 层验收运行中读取配置、增改删、归零、重复同步及启停语义。更新只操作自有 nft 对象。代理层先追踪 OpenClash `lan_ac_traffic` 最终作用层，再验证 PassWall、HomeProxy 的绕行机制。用真实流量验证接口、TCP/UDP、适用地址族及端口边界，并覆盖旧 `openclash_auto_restart` 升级、失败恢复和 CFI 与 UPnP 并发写入。
6. **验收并交付。** 按项目、插件版本、内核与变更类型给出能力矩阵，标明已验证热应用、仅持久化待显式应用、暂不支持或待真机验证。每个已验证组合均提供核心进程身份、真实流量、持久性和回滚证据。贯通 LuCI、RPC、ACL、后端状态、迁移与打包检查。缺少匹配环境时记录复测步骤，继续完成独立工作。归档或提交本目录另行决定。

## 7. 可复制的 Goal 提示词

```text
在 /Users/yunshu/Documents/GitHub 下完成 CFI 与 UPnP 的按能力热应用改造和验收。以 SWrt/docs/specs/openclash-guard-notify-and-cfip-hot-apply/openclash-cfip-consolidated-2026-11-20.md 的 §3 和 §6 为执行合同，§5 为历史进度。实现仓库是 luci-app-cloudflare-ip、luci-app-upnp-nat-relay，dns-guard 子任务涉及 YunWrt。保留现有用户改动及 spec.md、tasks.md 历史快照。

先证实并落地 CFI OpenClash file provider 原位刷新，用已验证的候选 IP 单独验收应用，再验证全流程在线测速和 dns-guard。PassWall 与 HomeProxy 分别按版本、内核、节点角色或运行模式实测。UPnP 分开验收 relay 自身热更新和代理端口绕行规则的运行态生效，覆盖 OpenClash、PassWall、HomeProxy 的可验证组合。

自动热应用和热回滚禁止硬重启。实际停服测速及恢复单独记录，不能宣称全程无停机。兼容并安全迁移旧配置，避免旧选项触发自动硬重启。维护租约不能屏蔽真实 DNS 故障的兜底降级。保护事务所有权和并发更新，应用失败先核对运行态再恢复。未授权不部署生产设备、不刷机、不推送、不合并。

持续推进有条件完成的实现和验证。最终交付分仓改动、带证据的支持矩阵、回滚结果和待复测项目。每个已支持组合要有进程身份、实际流量、重启或订阅后的持久性和失败恢复证据。无硬重启、全程无停机和连接零中断分别判定。
```

## 附：本文与原始文件的关系

- `spec.md`：2026-11-13 的取证与方案原文，包含已被实测修正的假设（例如“90s 是持续故障阈值”）和未采纳的 Phase 1 方案。保留不动。
- `tasks.md`：执行前的任务快照，Phase 1/2/3 全部未勾选。本文 §5 用真实状态覆盖其勾选语义，但不修改原文件。
- 本文：把两份文件与 11-13 之后的实际改动合到一起，按“实测 / 设计结论 / 未验证 / 未执行”分层，供只看一份文档的人理解来龙去脉。
