# OpenClash DNS 守护脚本 — 现状取证、通知改造与 cf-ip 免重启评估

状态：Phase 0 完成（只读取证）；**未修改设备任何文件**
日期：2026-11-13
设备：OpenWrt 25.12.5 x86/64（192.168.2.1），`luci-app-openclash 0.47.156`
本地只读副本：`/tmp/openclash-dns-guard.original.sh`（373 行，sha256 `faf8bc1a5de51f63935d35f52e7d091efe31dbba520f5d171cece8e021bc1072`）

---

## 0. 结论摘要

1. 守护脚本**确实不在镜像/仓库**：设备上的 `/usr/sbin/openclash-dns-guard.sh` 是手工部署的孤儿文件，`/etc/init.d/openclash-dns-guard` 只是它的 procd 启动壳。
2. 探测方式是**真实 DNS 解析**（`nslookup github.com` 打 653 / 953），不是看进程；解析不通即视为故障。
3. 触发条件是 **连续 2 次探测失败**（15s 间隔 ≈ 30s），另有“开机 90s 宽限”。**90s 是开机宽限，不是持续故障阈值**——与规格原假设冲突。
4. 通知通道是 **Bark**（`https://api.day.app/<key>`），失败入队、待 653 恢复后补发；没有“预期中断”概念，也**没有防抖/冷却**。
5. 降级 = dnsmasq 上游切 953 + 删除一条 `ip rule`（fwmark 0x162 → table 354, pref 1888）+ 杀 OpenClash watchdog；两个端口都不通时再退到 WAN DNS。
6. 恢复 = 加回 `ip rule` + dnsmasq 强制写回 `127.0.0.1#653`。**没有回读校验**，且记录的 `ORIG_*` 从未用于还原（原配置不是 653 时会被硬编码覆盖）。
7. 日志给出**硬证据**：cron `30 7,18,23 * * * cf-ip-auto --run` 每次运行，guard 都在 ~30s 后降级、发 2 条 Bark，84–101s 后恢复——正是用户说的“无意义通知”。
8. `suspend_watchdog` **实际无效**：降级后约 60s OpenClash watchdog 仍把 dnsmasq 改回 653，guard 再改回 953，形成拉锯并额外重启 dnsmasq 两次。

---

## 1. 取证结果

### 1.1 文件与触发

| 项 | 值 |
| --- | --- |
| 主体脚本 | `/usr/sbin/openclash-dns-guard.sh`，373 行，11638 字节，mtime 2026-11-12 16:54，`md5 4217e879bd2c2fad628d6eb123e31333` |
| 启动壳 | `/etc/init.d/openclash-dns-guard`，681 字节，`START=96`、`STOP=10`、`USE_PROCD=1` |
| procd 实例 | `command /usr/sbin/openclash-dns-guard.sh`，`respawn 0 5 0`（threshold 0 / timeout 5 / 无限重试） |
| 启停链接 | `S96openclash-dns-guard -> ../init.d/openclash-dns-guard`、`K10openclash-dns-guard -> …` |
| 运行态 | `ubus`: `running=true, pid=6430`；`pgrep -af dns-guard` → `/bin/sh /usr/sbin/openclash-dns-guard.sh` |
| 配置 | `/etc/config/openclash-dns-guard`（`enabled=1`、`probe_interval=15`、`primary_port=653`、`fallback_port=953`、`suspend_watchdog=1`、`manage_ip_rule=1`、mark/table `0x162`、pref `1888`） |
| 状态文件 | `/var/run/openclash-dns-guard.state`（当前 `ROLE=primary`、`OK_CNT=317`、`FB_COUNT=7`） |
| 队列文件 | `/var/run/openclash-dns-guard.queue`（当前 0 字节） |
| 日志 | `/yunshu/openwrt-maintenance/openclash-dns-guard.log`（4759 字节；`/yunshu` 未挂载时退回 `/var/log/…`） |
| syslog | `logread \| grep -i dns-guard` 为空（logd 环形缓冲已滚过；脚本内部用 `logger -t openclash-dns-guard`，不是唯一通道） |

### 1.2 触发链路（实测）

crontab 中存在：

```
30 7,18,23 * * * /usr/bin/cf-ip-auto --run >/dev/null 2>&1 # cf_ip managed
```

它与日志中的三次降级时间戳逐秒对上（18:30:55、23:30:51、07:30:56）。另有 `0 7 * * * /usr/share/openclash/openclash.sh`（订阅更新）与 `openclash_watchdog.sh`（pid 607）常驻。

---

## 2. 脚本逐段行为

### 2.1 探测（第 91 行）

```sh
probe() {
	nslookup -port="$1" -timeout=2 github.com 127.0.0.1 >/dev/null 2>&1
}
```

每轮对 653、953 各打一次，2s 超时。判定的是“能不能解析”，不是进程存活。

### 2.2 判定与状态（第 311–371 行）

```sh
if [ "$P653" = "1" ]; then
    OK_CNT=$((OK_CNT+1)); FAIL_CNT=0
else
    OK_CNT=0; FAIL_CNT=$((FAIL_CNT+1))
fi

case "$ROLE" in
primary)
    if [ "$P653" = "0" ] && [ "$FAIL_CNT" -ge 2 ] && [ $((NOW-BOOT_TS)) -ge 90 ]; then
        record_orig; save_state
        if [ "$P953" = "1" ]; then switch_to fallback; else switch_to wan; fi
    ...
```

- `FAIL_CNT >= 2` 且距**开机**超过 90s → 降级。15s 间隔下，**真实故障约 30s 就降级**。
- `ROLE` 只有 `primary / fallback / wan` 三态，没有“已告警但未降级”的中间态。

### 2.3 切换与通知（第 242–280 行）

```sh
fallback)
    apply_fallback
    rule_del
    FB_COUNT=$((FB_COUNT+1)); FB_START=$NOW
    [ "$NOTIFY_FB" = "1" ] && bark "OpenClash DNS 已降级" \
        "clash 核心无响应（653 连续 2 次解析失败），已临时切到 mosdns(953)…" timeSensitive
    suspend_watchdog
    ;;
primary)
    rule_add
    apply_primary
    DUR=$((NOW-FB_START))
    drain_queue
    [ "$NOTIFY_RC" = "1" ] && bark "OpenClash DNS 已恢复" \
        "已切回 clash(653)…故障开始：${FB_START_STR}，持续 ${DUR} 秒。" timeSensitive
```

即：**降级即通知、恢复即通知**，一次中断必然产生两条推送。没有 incident 去重、没有冷却窗口；653 抖动会反复触发。

### 2.4 通知通道（第 100–145 行）

- Bark：`POST https://api.day.app/$BARK_KEY`，JSON `{title, body, group, level}`，`group=openwrt`，`level=timeSensitive`，5s 超时。
- 发送失败 → `enqueue` 入队 `/var/run/openclash-dns-guard.queue`；在 653 健康时由 `drain_queue` 补发（主循环里 300s 最多一次，切回 primary 时立即一次）。
- 补发文案会追加“原始告警时间 …，因代理中断延迟送达”。

### 2.5 降级/恢复的落地动作（第 146–185 行）

| 函数 | 动作 |
| --- | --- |
| `record_orig` | 记录 `ORIG_SERVER / ORIG_NORESOLV / ORIG_RESOLVFILE`（**仅记录，后续从未使用**） |
| `apply_fallback` | 删 `dhcp.@dnsmasq[0].server` → 加 `127.0.0.1#953` → `noresolv=1` → commit → `/etc/init.d/dnsmasq restart` |
| `apply_primary` | 删 server → 加 `127.0.0.1#653` → `noresolv=1` → 若曾设过 resolvfile 则删 → commit → dnsmasq restart |
| `apply_wan` | server 清空、`noresolv=0`、`resolvfile=/tmp/resolv.conf.d/resolv.conf.auto`（`WE_SET_RESOLVEFILE=1`） |

### 2.6 策略路由（第 198–240 行，正是此前“方案 2”）

```sh
rule_present() { ip rule show | grep -q "fwmark $MARK lookup $TABLE_DEC"; }
rule_del() { ... ip rule del fwmark "$MARK" table "$TABLE" pref "$PREF" ...; }
rule_add() { ... table_ready || return 0; ip rule add fwmark "$MARK" table "$TABLE" pref "$PREF"; }
```

已带幂等判断与 `table_ready` 守卫；`repair_if_reverted` 每轮校正（fallback/wan 必须无此规则，primary 必须有）。这部分无需重写。

### 2.7 与 OpenClash 的冲突（第 187–200、282–303 行）

`suspend_watchdog` 对 `pgrep -f openclash_watchdog.sh` 发 `SIGTERM`，但日志里从未出现 `suspended openclash watchdog`——说明没匹配到或杀掉后被 procd 立刻拉起。结果是：

```
18:30:55 SWITCH primary->fallback（953）
18:31:58 dnsmasq upstream reverted externally (now=[127.0.0.1#653]), re-applying fallback
18:32:16 dnsmasq upstream reverted externally (now=[127.0.0.1#653]), re-applying fallback
18:32:22 SWITCH ->primary
```

60s 内两次拉锯、两次 dnsmasq 重启。`repair_if_reverted` 是“补救”，但补救本身没有日志节流。

---

## 3. 实测证据：cf-ip 就是误报源

`/yunshu/openwrt-maintenance/openclash-dns-guard.log` 近 7 次降级：

| 时间 | 角色切换 | 持续 | 通知 |
| --- | --- | --- | --- |
| 11-12 16:25:54 → 16:27:23 | primary→fallback→primary | 89s | 降级 + 恢复（降级因代理中断入队，恢复后补发） |
| 11-12 16:56:57 → 16:58:21 | 同上 | 87s | 2 条 |
| 11-12 16:59:19 → 17:00:09 | 同上 | 52s | 2 条 |
| 11-12 18:30:55 → 18:32:22 | 同上 | 87s | 2 条 |
| 11-12 23:30:51 → 23:32:33 | 同上 | 101s | 2 条 |
| 11-13 07:30:56 → 07:32:20 | 同上 | 84s | 2 条 |

`FB_COUNT=7`。后三次与 cron `30 7,18,23 * * * cf-ip-auto --run` 完全对应。**每次 cf-ip 运行 = 1 次降级 + 2 条推送 + 2 次 dnsmasq 重启 + 1 次策略路由拆除/恢复。**

---

## 4. 与旧规格对照

### 4.1 可直接套用

| 规格条目 | 现状 | 结论 |
| --- | --- | --- |
| 维护锁（任何发起方可用） | 脚本无此概念 | **可直接加**：主循环入口判断，纯增量 |
| `ip rule` 幂等增删 | 已实现 `rule_present/rule_add/rule_del/table_ready` | **保持不动** |
| 三级降级（953 → WAN） | 已实现 | 保留 |
| 告警/恢复成对 | 已实现（`switch_to` 成对） | 保留骨架，补校验 |
| 通知失败入队补发 | 已实现 | 保留 |

### 4.2 与脚本实际结构冲突，必须改方案

| 规格原设计 | 实际结构 | 冲突 | 处理 |
| --- | --- | --- | --- |
| “持续不可用 90s 才告警” | `FAIL_CNT>=2`（≈30s）；`BOOT_TS` 的 90s 只是开机宽限 | 90s 的语义被用错位置 | 改为按**持续时长**判定：新增 `first_fail_ts`，`NOW-first_fail_ts >= 90` 才 OCR 告警 |
| “ALERT 与 DEGRADED 两阶段” | 只有 `ROLE` 三态，降级即通知 | 无法表达“已告警但未降级” | 新增 `ALERT=0/1`（或 `STATE` 字段）与 `ROLE` 并存；ALERT 只发通知，DEGRADED 才动 DNS/路由 |
| “180s 才降级” | 30s 就降级 | 直接冲突 | 触发条件改为 `ALERT` 持续 180s（或 `FAIL_CNT*INTERVAL>=180`） |
| “恢复要回读校验” | `apply_primary` 后不校验；`ORIG_*` 记录不用 | 实际恢复是硬编码 653 | 恢复改为 `ORIG_SERVER/ORIG_NORESOLV/ORIG_RESOLVFILE` 回填 + 回读校验；失败保持 DEGRADED |
| “防抖/冷却” | 无 incident 概念；每次抖动都推送 | 缺失 | 加 `incident_id` + `last_notify_ts`，同 incident 只推 1 条，冷却 30min |
| “cf-ip 几秒中断不降级” | 30s 即降级 | 直接冲突 | 锁 + 90s 阈值共同解决 |

### 4.3 新发现（规格未覆盖）

1. `suspend_watchdog` 无效，guard 与 OpenClash watchdog 拉锯，产生额外 dnsmasq 重启。
2. `repair_if_reverted` 的“外部改回”日志每轮都可能重复，且未节流。
3. 降级期间 `dnsmasq restart`（全重启）会产生额外 DNS 抖动，应改 `reload`。
4. 通知通道是 Bark（密钥在 UCI），文案里已写“653 连续 2 次解析失败”，改造后需同步措辞，避免误导演示为“进程挂了”。

---

## 5. 改造方案（待批准，未执行）

文件：`/usr/sbin/openclash-dns-guard.sh`；备份：`cp -a` 带时间戳到同目录 `.bak.$(date +%Y%m%d%H%M%S)`。

### 5.1 新增配置项（`/etc/config/openclash-dns-guard`）

```
option alert_after '90'      # 持续不可用多久才告警
option degrade_after '180'   # 持续不可用多久才降级
option lock_ttl '600'        # 维护锁 TTL
option notify_cooldown '1800'
```

### 5.2 状态机（替换第 324–367 行的 `case`）

```
fail 持续时长 = NOW - first_fail_ts
lock 有效      = -f /var/run/openclash-maint.lock 且未超 TTL
if lock 有效: 保持当前 ROLE，不告警不降级（仅在 fallback 下继续 repair）
elif P653 ok 且 OK_CNT>=2: 恢复（若曾 ALERT/降级）
elif P653 fail:
    fail 持续 >= alert_after 且 ALERT=0 -> bark 告警一次，ALERT=1
    fail 持续 >= degrade_after 且 ROLE==primary -> 进入 fallback/wan
    ROLE==fallback 且 P953 fail -> wan
```

### 5.3 逐处改动

| 行 | 现状 | 改为 |
| --- | --- | --- |
| 14 | `BOOT_TS` 兼作宽限 | 保留开机宽限，另加 `FIRST_FAIL=0` |
| 30/39-40 | `INTERVAL` 默认 15、下限 5 | 不变 |
| 62–88 | `load/save_state` | 增 `ALERT`、`FIRST_FAIL`、`INCIDENT_ID`、`LAST_NOTIFY` |
| 149–155 | `record_orig` 只存不用 | 恢复时使用（见 157–168） |
| 157–168 | `apply_primary` 硬编码 653 | 用 `ORIG_SERVER` 回填；无记录时回落 653；`restart` → `reload` + `has_srv` 回读 |
| 169–176 | `apply_fallback` | 保留，`restart` → `reload` |
| 187–200 | `suspend_watchdog` 无效 | 改为向 OpenClash watchdog 的 procd 实例发 `reload`/`stop` 的可行路径需先验证；**验证前保留但不再作为正确性依赖**，由 `repair_if_reverted` 兜底并加节流 |
| 242–280 | `switch_to` 内直接 `bark` | 搬到状态机层，按 incident 去重；`switch_to` 只做动作 |
| 251/264/272 | 三处 `bark` | 统一走 `notify_once` |
| 282–303 | `repair_if_reverted` | 增加“N 秒内同原因只记一次日志” |
| 337 | `FAIL_CNT>=2 && NOW-BOOT_TS>=90` | 改为 `fail 持续 >= degrade_after`，且必须先经过 ALERT |

### 5.4 不动的部分

- `probe()`、`rule_*`、`apply_wan`、Bark 发送/入队/补发、`/etc/init.d/openclash*`、`/usr/share/openclash/*`。

---

## 6. cf-ip 免重启应用新 IP

### 6.1 现状

`cf-ip-auto-legacy`：`OPENCLASH_CONFIG=/etc/openclash/config/config.yaml`，`update_openclash()` 改 `proxies[].server`，备份 `.bak.<ts>`，校验 YAML；事务层 `cfip_stop_service`（`uci enable=0` + `init.d/openclash stop`）→ 写 → `cfip_restart_service`（`enable=1` + `restart`，312s、最多 2 次）。**全量停启，正是触发 guard 误报的那段中断。**

### 6.2 能力判定

| 能力 | 结论 |
| --- | --- |
| `PUT /configs?force=false` | **不可行**：只热更 general，不重建 proxies |
| `PUT /configs?force=true` | 有条件可行：整份重载、TUN 重建、1–3s 瞬断 |
| `proxy-providers` + `PUT /providers/proxies/<name>` | **可行（推荐）**：不动主配置/TUN/fw4 |
| provider `interval` 自动刷新 | 延迟不可控 |
| `SIGHUP` | 不作为方案（procd 托管） |

### 6.3 结论与条件

**有条件可行**，条件：
1. `config.yaml` 增加 `proxy-providers.cf-ip`（`type: file`, `path: ./proxy_provider/cf-ip.yaml`），代理组 `use: [cf-ip]`（一次性改造，需一次重启）。
2. cf-ip 只写 provider 文件，节点名稳定。
3. 写后 `PUT /providers/proxies/cf-ip`（Bearer secret，127.0.0.1:9090/cn_port）并 `GET /group` 校验。
4. OpenClash 订阅更新不会覆盖 provider 段/文件（`0 7 * * * openclash.sh` 每天 07:00 会重写 `config.yaml`，必须先验证）。

影响：隧道不重建、TUN 不动、fw4/nft 不动、DNS 不动；失败回落现有停启流程并标记 `mode=restart`。

---

## 7. 验证（不改设备文件，用 `/tmp` 副本）

| # | 验证 | 通过标准 |
| --- | --- | --- |
| V1 | `sh -n /tmp/guard.new` | exit 0 |
| V2 | 干跑：失败 60s | 无告警、无降级 |
| V3 | 干跑：失败 120s | 1 条告警、仍未降级 |
| V4 | 干跑：失败 200s | 恰好 1 次降级 |
| V5 | 干跑：恢复 | 1 条恢复通知、`ORIG_SERVER` 回填且回读通过 |
| V6 | 干跑：锁存在时失败 300s | 全程静默 |
| V7 | 干跑：强制让回读失败 | 保持降级、ERROR 日志、不发恢复通知 |
| V8 | `curl GET /providers/proxies` | 200（只读） |

---

## 8. 风险与回退

- 风险：改为 180s 才降级后，真故障时用户多断 150s DNS（可接受，因 653 死时本来也没 DNS）；锁忘记释放 → TTL 兜底。
- 回退：`cp -a <bak> /usr/sbin/openclash-dns-guard.sh` 即可；配置项缺失时脚本用默认值。
- 全程不重启 OpenClash、不动 DNS、不改防火墙。

---

## 9. 待批准

1. 是否按 §5 改造（含 90s 告警 / 180s 降级 / 维护锁 / 恢复回读校验）。
2. 是否顺带修 `suspend_watchdog` 的拉锯（需先单独验证 OpenClash watchdog 的重启策略）。
3. cf-ip 免重启是否只做仓库实现 + 本地容器验证。


---

## 附录 A：cf-ip 免重启——设备实测结论（2026-11-13）

### A.1 事实（实测）

| 项 | 实测值 |
| --- | --- |
| `/usr/bin/cf-ip-auto` | 二进制（95860 字节，2026-06-29），`script_version 2.6.0`、`package_version 2.6.0-r12` |
| 实际写入目标 | **`/yunshu/clash/clash-proxies.yaml`**（`uci cf_ip.openclash.config`），不是 `config.yaml` |
| 主配置 | 不存在 `/etc/openclash/config/config.yaml`；实际为 `/etc/openclash/config/yunshu.yaml`（`openclash.config.config_path`） |
| 主配置已有 provider | `proxy-providers: 订阅 → ./proxy_provider/subconverter.yaml`、`本地节点 → ./proxy_provider/clash-proxies.yaml` |
| 组引用 | `自建节点` 同时有 `proxies: [自用cf负载均衡]` 与 `use: [本地节点]`；`自用cf负载均衡` 为 `load-balance` + `use: [本地节点]` + `filter: '[CF-'`，interval 300，lazy |
| 节点名 | `🇺🇸美国-自建1-VLESS-xhttp [CF-1]`、`[CF-2]`…；cf-ip 只改 `server:`，日志确认“name 不变” |
| 控制器 | `external-controller: :9090`，`openclash.config.cn_port=9090`；secret 取 `openclash.config.dashboard_password` |
| mihomo | `alpha-smart-g2e80ed7`（2026-08-10，with_gvisor） |
| GET `/providers/proxies/本地节点` | 200，`type=Proxy`、`vehicleType=File` |
| 订阅更新 | `openclash.@config_subscribe[0].name=yunshu`，源 `https://cdn.jsdelivr.net/gh/hello-yunshu/selfuse@master/openwrt/yunshu.yaml`；`openclash.sh` 用 `mv` **整体替换** yunshu.yaml |
| 源模板 | 本地 `selfuse/openwrt/yunshu.yaml` 第 167 行起即有 `proxy-providers` 与 `本地节点`，故整体替换后 provider 段仍在 |

### A.2 时间去向（实测）

`/tmp/cf_ip/run-1794526201-16029-3324`：runId 起点 07:30:01，outcome `observedAt` 07:32:09，guard 07:32:22 恢复 → 全程约 128–141s。历史日志同构：

```
07:32:54 stopping service for speedtest: openclash
07:33:05 running speedtest with /usr/bin/cfst/ip.txt
07:35:14 verifying selected IPs
07:35:18 updating OpenClash config
07:35:18 restarting service: openclash
07:35:26 done
```

即：**约 129s 花在 cfst 测速**（期间 OpenClash 处于停止），写入 <1s，重启 8s。停止是“为了测速不被 TUN 劫持”，不是应用配置所必需。

### A.3 结论

- **应用新 IP：可行（热刷新）**。目标文件本身就是 file 型 provider，改完后 `PUT /providers/proxies/本地节点`（Bearer secret，127.0.0.1:9090/`cn_port`）即可让 mihomo 重读；不重启进程、不重建 TUN、不动 fw4/DNS。
- **但整次运行仍会中断 OpenClash**：cf-ip 在测速前主动 stop（`stop_service=1`、`_OPENCLASH_INITIAL_HEALTHY` 时），这是测速正确性的前提。**免重启只解决“应用”这一步，不解决“测速窗口”**。
- `PUT` 端点：同路径 `GET` 已在设备验证 200；`PUT` 属 mihomo 文档/源码推导（该版本具备），设备上未做写操作验证。
- 订阅覆盖风险：**成立但可控**——替换的是 yunshu.yaml，provider 段来自用户自己的模板，替换后仍存在；若模板以后删掉该段则失效。

### A.4 改造清单（若采纳）

1. `cf-openwrt-auto.sh`：新增 `cfip_openclash_provider_refresh()`，读 `openclash.config.cn_port` + `dashboard_password`，`curl -X PUT .../providers/proxies/${provider}`，期望 204；随后 `GET` 校验目标节点 `server` 已变为新 IP。
2. `update_openclash()` 末尾：`restart_service openclash` → 先热刷新，失败才回落到 `restart_service`（保留原有 312s/2 次与 health 校验）。
3. 新增 `cf_ip.openclash.provider_name`（默认 `本地节点`）与 `cf_ip.openclash.hot_apply`（默认 1）。
4. 回退：热刷新失败 → 现有 stop/restart 流程；备份 `.bak.<ts>` 不变。
5. 若要彻底消除中断，需另议“测速不停 OpenClash”（让 cfst 走直连旁路），不在本次范围。
