# CFI 与 UPnP 热应用验收记录

- 日期：2026-11-20
- 执行依据：同目录 `openclash-cfip-consolidated-2026-11-20.md` §3、§6；历史快照 `spec.md`、`tasks.md` 未修改。
- 结果范围：隔离环境与仓库测试；没有向生产设备写入、安装包或重启服务，也没有刷机、推送或合并。

## 基线与保护

| 仓库 | 分支 | 本次改动前已存在的用户文件 |
|---|---|---|
| `luci-app-cloudflare-ip` | `codex/cfi-lancelot-status-r23-20261120` | `luci-four-repo-ui-evidence-2026-11-15/` |
| `luci-app-upnp-nat-relay` | `codex/upnp-footer-r15-20261120` | `real-luci-ui-evidence-2026-11-15/` |
| `YunWrt` | 原分支保持 | 本轮只在 DNS 守护子任务文件中改动 |
| `SWrt` | 原分支保持 | `SEED/*`、`SELF/X86/config_self.seed`、原有 `docs/` 与 `yunwrt-upstream-coupling-audit.md` 保留 |

生产设备仅做只读运行态核对：OpenWrt 25.12.5 x86/64、OpenClash 0.47.156、活跃 Mihomo Meta 核心、控制器与 file provider。没有执行 provider PUT、订阅更新、UCI 写入、服务操作或流量切换。

## 支持矩阵

| 组合 / 操作 | 判定 | 已有运行态证据与限制 |
|---|---|---|
| CFI → OpenClash file provider，直接同步 | **打包 v2 和独立脚本委托入口均在匹配隔离 guest 验证热应用/热回滚** | v2及 standalone bridge 复用同一 writer lock/transaction；成功后真实代理新连接到测试 Xray backend，拒绝/不可达候选触发热回滚，活动 `-f` 配置不匹配在写入前拒绝；PID/start identity 不变且无代理 restart。standalone成功出口使用测试 runner替换 post-probe以提供本地 VLESS→Xray 流量，不能当成生产候选 HTTPS probe 成功证据。生产设备尚未 PUT；不证明 TUN/策略路由/DNS 连续性或已有连接零中断。 |
| CFI → OpenClash 完整测速优化 | **匹配版本隔离 guest 的 CFST 测量阶段无停服通过；完整生产流程及全面零中断未证明** | 当前仓库的 `stop_service_for_measurement` 与 `run_cfst` 实际调用成功，TCPing/HTTPS HTTPing 均仅为候选 TCP/443 创建临时 fw4 例外。旁路无法证实时，旧配置显式允许的维护停服仍会停服；关闭维护停服则失败关闭。 |
| CFI → PassWall1 | **仅配置持久化，运行态需完整服务重建** | CFI 事务不自动 stop/restart，结果标记 `configurationPersisted=true`、`serviceApplied=false`、`applyPending=true`。隔离 ImmortalWrt 24.10.4/aarch64_generic 环境中，PassWall 26.11.11-r1 的 UCI SOCKS 节点经 `app.sh run_socks` 生成 sing-box 配置并启动进程；本地 SOCKS 上游 HTTPS 返回 200。UCI upstream 端口改动后 `/etc/init.d/passwall reload` 执行完整重启，进程 PID 与生成配置随之更换，重启后 HTTPS 返回 200。日志出现无 `CAP_NET_ADMIN` 导致 nft 清理失败；此流量只证明应用层 SOCKS 出站，不证明透明代理/firewall，也不是 CFI 写入后的热刷新。 |
| CFI → HomeProxy | **仅配置/进程/应用层出站验证；CFI 热应用不支持** | HomeProxy 26.300.08316 在同一 feed 环境读取 UCI SOCKS 节点并生成 sing-box JSON；匹配的官方 sing-box 1.12.25 `check` 成功，procd 控制的服务出站经 mixed inbound 返回 HTTPS 200。UCI 节点变化后 `homeproxy reload` 记录 stop/start、替换 sing-box PID；切换窗口请求一次返回 HTTP 000/连接拒绝，重试监听恢复后为 200。因此 reload 是服务重建且有短暂不可用，不是热应用。PassWall feed 的 sing-box 1.14.4 与 HomeProxy 生成器字段不兼容，不能混用。 |
| CFI → PassWall2 | **仅配置持久化，运行态需完整服务重建** | PassWall2 26.11.11-r1 独立验证：UCI SOCKS 节点经其 `app.sh run_socks` 生成 sing-box 配置并启动独立进程，本地 SOCKS 上游 HTTPS 返回 200。UCI upstream 端口改动后 `/etc/init.d/passwall2 reload` 替换进程 PID、更新生成配置，重启后 HTTPS 返回 200；同样有无 `CAP_NET_ADMIN` 的 nft 清理错误。此项不推断自 PassWall1，不证明透明代理/firewall 或 CFI 写入后的热刷新。 |
| UPnP relay → 实际 nft DNAT 流量 | **IPv4 TCP/UDP 已验证** | OpenWrt 24.10.8 特权容器中，relay 读取映射并写入真实 `inet upnp_nat_relay` nft 表；独立客户端经 relay 路由收到 TCP、UDP 目标服务响应。TCP 未映射端口及错误协议连接被拒绝。重复同步规则不重复；映射更新与归零后规则相应改变/清空。没有 IPv6 流量证据。 |
| UPnP relay → 运行中配置重读 | **有界隔离证据** | ad-hoc relay daemon 轮询期间，UCI/允许协议与端口变化被读取并更新实际 nft 规则；观察到的 daemon PID 集合在更新前后不变。映射归零的空列表用 `upnpc: 0 entries` 模拟器输出。OpenWrt 24.10 的 rc.common reload trigger 也已按 `enabled/running` 四种转换测试：只在启用且未运行时 start、禁用且运行时 stop，已运行服务不重启。daemon 同 PID 的真实 procd 硬件行为仍待目标设备验收。 |
| UPnP → OpenClash `lan_ac_traffic` 绕行 | **不支持；规则元组不匹配入站转发流量** | 生产设备 OpenClash 0.47.156 源码只读核对及隔离运行 OpenClash 0.48.002 规则函数测试均指向 `mangle_prerouting` 子链，匹配 `iifname`、源地址、源端口。隔离真实流量中 DNAT 入站 TCP 到本地服务成功，但该规则计数为 0、代理路径计数增长；入站源是远端 client/ephemeral port，与 relay 写入的 downstream WAN IP/external port 不符。目标下游回包又与要求的上游入口接口不符。IPv4 `src_ip` 配 `family=both` 且 OpenClash IPv6 开启时，代码把 IPv4 地址拼为 `ip6 saddr`，IPv6 nft 规则插入失败且函数仍返回成功。该无效自动生成路径现已 fail-closed。 |
| UPnP → HomeProxy 绕行 | **匹配版本 guest 中仓库 relay IPv4/TCP DNAT 真实通过；HomeProxy common UDP 端口与 IPv6 不支持** | 当前仓库 relay `--sync` 生成的独立 nft DNAT 规则优先于 HomeProxy TCP NAT redirect，映射 TCP 到达后端 HTTP 200，HomeProxy redirect 计数不变；非 common 端口 UDP 到达后端。UDP common 端口先被 HomeProxy TPROXY 截获，IPv6 不匹配 `dnat ip` 并落入 input reject。仅支持已测 IPv4/TCP tuple，不外推 UDP/IPv6。 |
| UPnP → PassWall1 绕行 | **源码显示 IPv4 WAN 目标有前置 RETURN；真实流量未验收** | `b3cf8e7` nft backend 在 `dstnat-1` hook 先对 `psw_wan` 做 `ip daddr ... return`，再跑 ACL；WAN IP set 从非 LAN routed interfaces 填充。推断它可让目标为路由器自身 WAN IP 的 IPv4 UPnP 外部端口在 fw4 DNAT 前返回。未用真实流量/计数器验证；其他 backend/config 需单测。 |
| UPnP → PassWall2 绕行 | **源码显示 WAN 目标有前置 RETURN；真实流量未验收** | `7d6bae5` nft backend 同样在 `dstnat-1` 对 WAN IP set 返回，再进入 ACL；WAN set 同时收 IPv4/IPv6。已见 IPv6 mangle 独立 RETURN，但 NAT 规则的 WAN RETURN 是 IPv4。是否覆盖实际 UPnP 转发取决于 WAN set、协议/模式和 hook ordering；当前无入站流量计数器证据。 |

HomeProxy、PassWall1、PassWall2 的隔离 UCI→配置→进程→HTTPS 证据与 reload PID/短暂中断记录另存于 `passwall-homeproxy-isolated-runtime-evidence-2026-11-20.md`。
PassWall/HomeProxy UPnP 绕行仅源码追踪，不是运行态证明；隔离代理容器共享 7.2.12 host kernel、缺少 `CAP_NET_ADMIN`，guest kmod ABI 为 6.6.110，无法验收透明转发/真实 nft flow。HomeProxy/PW1/PW2 reload 的进程重建证据在上述文件中。

## CFI 与 DNS 变更

- CFI 直接热刷新按运行态核心 `-f` 配置解析 file provider、provider 文件、代理组引用和控制器；对歧义、路径不安全或身份漂移 fail-closed。事务快照只包含 provider 文件，应用与失败回滚都通过 provider API，不调用代理 init restart。
- CFI 同步结果新增 `hotApplied`、`maintenanceRestarted`、`applicationMethod`，避免把维护恢复误报为 provider 热应用；OpenClash disabled 状态报告 deferred。
- 候选 IP 探测修复 Bash `local` 同语句展开顺序缺陷：`curl --resolve` 之前会得到空地址。新增 `observe-candidate-resolve.sh` 回归测试，确认 IPv4/IPv6 被传给 curl 参数。
- UPnP 对已证伪的 OpenClash 入站绕行组合改为 fail-closed：setup/sync/auto-sync/preview 不再生成或写入 `lan_ac_traffic`，返回 `openclash_inbound_bypass_unsupported`；显式移除旧规则与显式手动重启仍保留。旧 `openclash_auto_restart` 由升级脚本迁移删除，后端忽略遗留值。
- CFST v2.3.5 `-httping` 在隔离 OpenWrt 24.10.8 容器中，以 direct 和显式 Mihomo HTTP listener 两种方式各测出可用候选；但容器无 `/dev/net/tun`。显式 `HTTPS_PROXY` 下 curl 虽成功，`--resolve` 候选地址可能由代理接管解析；本地 Mihomo 规则最终走 DIRECT。因此此测试只证明 CFST/curl 能通过本地 listener 发请求，不证明候选 IP 是实际连接目标，也不证明生产 TUN 下的预期路径。
- 已安装脚本的 legacy OpenClash 自动应用入口 fail-closed；全配置备份不能误走 provider API restore。
- PassWall 事务在写入前恢复测速期间的运行服务，再持久化 UCI；提交失败回滚 UCI 与托管状态，不自动重启代理。
- YunWrt DNS 守护增加有界 owner/PID/start-time/boot-ID 租约，最长 180 秒；相同故障告警去重并有冷却。配置将故障降级阈值限制在 140 秒、轮询最多 15 秒、探测超时 2 秒。对实际守护脚本副本运行了故障注入：只把 UCI、resolver、dnsmasq reload、Bark 和文件路径替换为隔离模拟器；653/953 同时失败后观察到 6 秒进入 WAN fallback，测试契约上界为 15 秒（5 秒轮询），恢复两端点后 10 秒回 primary，分别只记录 1 条降级和恢复通知。测试使用短阈值，证明状态机/动作顺序，不证明生产阈值下真实网络故障最晚时间，也不证明真实 dnsmasq、WAN resolv.conf 或 Bark 服务行为。
- DNS 原始 `server`、`noresolv`、`resolvfile` 先持久化快照再切换；恢复逐项回读，旧状态缺少快照时拒绝猜测恢复值。
- 隔离 guest 已实际运行一次 OpenClash 订阅更新，验证更新后活动主配置的 file-provider 路径、代理组 `use`/`proxies` 引用、provider API 和新出站均有效；订阅 updater 本身会 stop/start，证据与 CFI 热应用分开记录。生产订阅持久性未验证。

## 回滚与测试

**隔离运行态回滚：** provider 更新到故障候选后，新的代理请求失败；用旧 provider 快照进行 provider API 热回滚后，新的代理请求恢复成功。核心进程身份保持不变。旧连接是否连续、TUN 是否连续均未测。

**通过的定向检查：**

- CFI：`observe-candidate-resolve.sh`、`openclash-provider-transaction.sh`、`openclash-dns-maintenance-lease.sh`、`openclash-transaction-write-failure.sh`、`openclash-rollback-external-conflict.sh`、`openclash-review-regressions.sh`、`openclash-legacy-service-guard.sh`、`openclash-service-guard.sh`、`v2-transaction.sh`、`passwall-managed.sh`。
- PassWall 单同步及提交失败回滚：`sync-single-restart.sh`、`sync-stop-failure-cleanup.sh` 使用仓库 `tests/helpers/flock-kernel.py` 适配本机缺少 `flock` 的环境；均确认无代理 stop/restart。
- CFI UI：两份 JS `node --check`、`tests/i18n-contract.py`、英文/简中 `msgfmt --check`、`git diff --check` 通过。
- UPnP：`tests/runtime-hot-apply-contract.sh`、`tests/fw4-detection-timeout.sh`、`tests/openclash-inbound-bypass-unsupported.sh`、`tests/reload-enable-transitions.sh`、两份 JS `node --check`、英文/简中 `msgfmt --check`、`git diff --check` 通过；四个 relay 合约现由 real-LuCI workflow 在 UI 环境启动前执行。
- YunWrt：`scripts/test-dns-guard.sh`、`scripts/test-dns-guard-runtime-fault.sh` 与 dns-guard `sh -n` 通过；两个脚本都已接入 `check-dns-guard.sh` 门禁。独立故障 harness 运行实际守护脚本副本、模拟 resolver/UCI/dnsmasq/Bark：在 5 秒轮询设置下 15 秒内切 WAN，双端点恢复后回到 primary，只生成 1 条 failover 与 1 条 recovery 通知；原 server/noresolv 值回读一致。这是隔离状态机故障注入，不是设备 DNS 故障流量或生产阈值计时。

## 分类结论

- **应用无硬重启：** CFI OpenClash file-provider API 同步及失败热回滚在隔离 Mihomo 环境中成立；PassWall1 配置保存不应用运行态；relay 规则刷新不重启代理。匹配 OpenClash guest 的当前完整 `run_cfst` 测量段已实际无停服运行。
- **全程无停机：** 匹配版本/活动链/TCPing 或 HTTPS HTTPing 的 CFST 测量段无停服通过；旁路失败且显式允许维护停服时仍会停服务。整次生产等价任务和所有配置组合没有统一验收，不能宣称全组合全程无停机。
- **已有连接零中断：** 一条既有 OpenClash HTTP 代理流在同一隔离 CFST 测量期间收到全部 18 个数据块；不外推为任意拓扑/协议都零中断。
- **在线测速路径等价：** 匹配 OpenClash 输出链中，完整 CFST TCPing/HTTPS HTTPing 使用精确 UID+候选 IP+TCP/443 bypass，候选 TCP redirect 计数不增长；同候选绕行关闭的负控制则进入 OpenClash redirect。测速吞吐只有单样本，未形成稳定性能基准。
- **生产持久性：** 未验证生产设备写入与重启/订阅后的持久性；所有生产端状态保持未触碰。

## 可复跑的待验收项

1. 在与生产插件/内核 ABI 匹配的隔离 OpenWrt 中，分别为 HomeProxy、PassWall1、PassWall2 配置活跃节点、分流引用节点和未引用节点，记录 UCI → 生成配置 → 活跃进程/出站流量，并比较 reload 前后 PID、监听端口、路由及防火墙规则。
2. 用运行中的 Mihomo/TUN 环境运行 CFST 与候选探测，同时记录直连基线、代理路径、DNS 目标、接口流量和 TUN/路由状态；要求证明实际 TCP 远端是候选 IP（代理模式下单看 curl 成功或 `--resolve` 参数不够），无法证明时继续标记维护测速。
3. 在隔离设备对真实 DNS 目标做双端点持续故障注入，使用生产限值并记录故障开始至 WAN 降级时间、通知次数、租约持有人退出/过期和恢复读回；当前短阈值模拟不能替代此项。
4. 完整运行一次 OpenClash 订阅刷新，验证活动主配置中的 provider 类型/路径以及代理组 `use`/`proxies` 引用；比较核心身份与 provider API 返回。
5. 完成 `lan_ac_traffic` 的真实流量计数/连接追踪，确认 TCP/UDP、IPv4/IPv6、接口和端口边界；如生成元组对入站/回包均不匹配，先修正规则定义后再验收。
6. 以并发写入者同时更新 CFI provider 与 UPnP 规则，制造超时/部分失败；先读取实际运行态，再确认事务所有权下回滚且不硬重启代理。

## 本轮实施与隔离复测增补（2026-11-20）

以下是本轮代码和隔离测试的新增结果。它们不替代上文已经留存的历史证据；若历史测试与本轮环境不同，按下表标明的范围解释。

### CFI / OpenClash

- `luci-app-cloudflare-ip` 的 v2 事务现在包含按活动 core `-f` 配置解析 provider、provider 文件和 proxy-group 引用；写入范围只限被引用的 file-provider 文档。应用及事务拥有的回滚均走 authenticated `/providers/proxies/<name>` API，没有 restart fallback。旧整份 Clash config 备份会在事务分配前拒绝，避免留下空事务日志。provider 事务、restore、发布故障注入和身份/服务守卫契约通过。
- 运行态 bypass helper 按 OpenClash v0.47.156 的真实拓扑，在 `mangle_output`/`nat_output` hook base chain 跳入的 OpenClash 普通子链中识别活动规则；仅放行 `uid=65534,gid=1` helper 到候选地址 TCP/443。选用 `nobody:daemon` 是为了不命中 OpenClash 自身 `skgid 65534 RETURN` 的全流量豁免。临时集合有 300 秒元素 TTL；规则、集合、provider identity 或子链关系丢失会使整轮数据作废并清理，清理失败报告恢复错误。
- **真实内核/流量边界证据：** ImmortalWrt 24.10.4 armsr/armv8、kernel 6.6.110、nft 1.1.1 QEMU guest 中，复用了真实 nft 与 start-stop-daemon，按 v0.47.156 源码布局建立 base hook → 普通 OpenClash child chains。`nobody:daemon` 到候选 `203.0.113.10:443` 的新 TLS 连接返回 200；同 UID 非候选 IP:443、候选 IP:8443 和 root 到候选 IP:443 都未命中精确旁路，并进入模拟代理 redirect 计数。flush/rebuild child chain 后 helper assert 使本轮无效，cleanup 后无自有 rule/set 残留。复跑脚本：`/tmp/openclash-runtime-lab-20261120/probe-test.sh`。
- **证据边界（被后续精确版本复测更新）：** 后续隔离 QEMU 已运行匹配 OpenClash v0.47.156 与精确 `alpha-smart-g2e80ed7` core，补上真实 Mihomo provider PUT/GET、新连接出站、provider 热回滚、配置文件替换/`PUT /configs?force=true`、真实 OpenClash 活动 nft 链及 CFST/HTTPS 候选路径。详见本报告“本轮追加运行态与安全边界”及 `/tmp/openclash-runtime-isolated-20261120/acceptance-notes.md`。订阅下载器/parser 和生产设备仍未验收。
- 在线测量分支先尝试精确 bypass；能力探测失败时，若既有 `stop_service=1`，仍按显式维护操作停服、测量和有界恢复；`stop_service=0` 时拒绝不确定路径，不会默默走代理测速。TCPing 与 HTTPS HTTPing 的仓库完整 CFST 调用均已在匹配活动核心下通过无停服测量验收，但整次生产等价任务仍待统一跑通。**早期**独立版和打包 legacy 的 OpenClash 入口曾 fail-closed；后续已经给独立版增加由打包 v2 锁和事务托管的 `--apply-selected` bridge，见本报告最后的独立 CFI 补验。打包 legacy binary 的直接 OpenClash apply 仍 fail-closed，避免旧整配置写入/硬重启路径被误启用。

### CFI 后端组合与 DNS 守护

- HomeProxy/PassWall2 实验 guest：ImmortalWrt 24.10.4 armsr/armv8、kernel 6.6.110，HomeProxy `26.300.08316~d72e293`、PassWall2 `26.11.11-r1`、sing-box `1.12.25-r1` 与对应 kmods。
- PassWall2 的真实 IPv4 TCP IGD 映射 `198.51.100.1:19080 → 10.33.0.2:8080` 返回 `BACKEND-DIRECT-OK`；`psw2_wan` 中命中目标、`WAN_IP_RETURN` counter 增长，之后 fw4 DNAT 生效。非本机目标负对照进入 PassWall2 redirect 并失败。此项是 **UPnP ↔ PassWall2 IPv4/TCP 绕行已验证**，不是 CFI 对 PassWall2 的热应用支持证明。
- HomeProxy 同一 guest 上的真实 IGD 映射请求经其生成的 fw4 redirect chain 后，由 `homeproxy_local_addr_v4` return 并到达后端；但选择测试节点后其生成配置被 sing-box 1.12.25 以 `outbounds[2].routing_mark: unknown field` 拒绝，core child crash-loop。故只验证了 **HomeProxy 防火墙侧入站 DNAT 路径**；HomeProxy 代理运行态绕行及 CFI 热应用均不支持/未证明。需匹配 HomeProxy 官方所需 sing-box 版本重测。完整命令、包版本和链计数见 `/tmp/immortalwrt-qemu-lab-20261120/acceptance-notes.md`。
- YunWrt `scripts/test-dns-guard.sh` 与 `scripts/test-dns-guard-runtime-fault.sh` 本轮复跑通过。隔离故障注入在 6 秒切入 WAN fallback；双端点恢复后回到 primary，只有 1 条降级和 1 条恢复通知。租约边界、重启/owner 失效、最大期限、incident 去重和原 UCI 回读测试通过。仍非真实设备 resolver/dnsmasq 故障或生产阈值最晚降级计时。

### UPnP relay 与代理侧边界

- relay DNAT 重建改为一份经过验证的 nft batch（单次 `nft -f` 提交 flush + 全量重加）；提交失败保留旧规则，成功后才更新 fingerprint。归零通过空规则 batch 处理。UCI 配置变化仍由 daemon 同一 PID 在下一轮同步重读；代理 init 不参与 DNAT 更新。隔离 nft container 已验证 batch 失败时旧规则仍在、成功更新 TCP/UDP 项和归零。五个 relay 热应用/迁移/fail-closed 合约测试通过。
- relay 不再按 IP 添加/删除 OpenClash 共享 `localnetwork` 成员，避免删除无明确所有权的管理员/代理项。旧 `openclash_auto_restart` 升级选项仍迁移删除。
- OpenClash `lan_ac_traffic` 对入站映射的 source tuple 不匹配，自动生成/写入被 fail-closed。页面与 README 说明 relay 直接写自有 DNAT、不会改 OpenClash UCI 或自动重启。**当时**还未完成真实 WAN ingress；后续用匹配 OpenClash guest 和 WAN veth namespace 验证了 relay 自有 DNAT 的 TCP/UDP路径，具体限制见本报告末尾“OpenClash 活动 guest 的 relay WAN-ingress 路径补验”。
- 上述 QEMU guest 另外证明 PassWall2 IPv4/TCP WAN 目标返回与 HomeProxy 的 firewall local-address 返回；UDP、IPv6、其他版本/模式未覆盖。HomeProxy core 未运行时的链计数不能当作 HomeProxy 全栈支持。
- 独立核语法/检查：CFI `shellcheck -x -S error` 通过，provider/restore/bypass/DNS lease/服务守卫定向测试通过；UPnP 5 个合同测试、JS `node --check`、PO `msgfmt --check`、`git diff --check` 通过。CFI `openclash-document-boundary.sh` 当时被 macOS 自带 BSD sed / Ruby 2.6 环境阻塞；后续用 GNU sed 与 Homebrew portable Ruby 4.0.9/Psych 5.3.1 重跑并通过，见本轮后续补验记录。

### 本轮判定

| 能力 | 当前判定 |
|---|---|
| CFI OpenClash provider 发布/回滚事务 | **当前仓库 transaction runner、真实新连接出站、不可达/超时失败热回滚及订阅更新后引用有效均在匹配 guest 通过；生产持久性待验** |
| CFI OpenClash 候选在线旁路 | **活动 OpenClash daemon、精确 core、仓库完整 CFST 调用、TCPing/HTTPS HTTPing 与 HTTPS 候选探测已隔离验证；单条既有 HTTP 代理流在测速间连续完成** |
| OpenClash 自动应用“无硬重启” | v2 provider 应用/回滚与 standalone委托桥接没有 restart fallback；打包 legacy binary 的直接 apply 仍 fail-closed |
| CFI 完整测速无停服边界 | **匹配 OpenClash 版本/识别链/TCPing或HTTPS HTTPing已证实测速段无停服**；bypass 不可用时显式 `stop_service=1` 仍允许维护停服，`0` 则失败关闭；全任务生产等价运行仍待验 |
| CFI 已有连接零中断 | **单条既有 OpenClash HTTP 代理连接跨测速完整完成；其他协议/拓扑与生产零中断未证明** |
| PassWall2 UPnP | **IPv4/TCP 真实 IGD 流量已验证**；其他协议/地址族待复测 |
| HomeProxy UPnP | **fw4 规则路径已验证；代理 core 配置不兼容，非全栈支持** |
| OpenClash UPnP | **自有 DNAT 不改代理；旁路依赖运行时 localnetwork，真实 OpenClash WAN ingress 待验证** |


## 本轮追加运行态与安全边界（2026-11-20 后续复测）

### OpenClash file provider 的真实 Mihomo API

- 新建隔离 QEMU guest：ImmortalWrt 24.10.4 `armsr/armv8`、kernel 6.6.110、OpenClash 官方包 `0.47.156`。先在该包默认 `master/meta` core `alpha-gbc76c13` 完成 provider 与配置文件替换 fixture；随后按计划历史设备版本，从不可变官方 commit `7dedf5508d1aba0360820bff3532eb4257c79fae` 下载并验证精确 `alpha-smart-g2e80ed7` ARM64 Smart core（SHA256 `fbcc55ee7d0421354e218c629cce686af1bd18e40912b843bba040f25ef23bca`）。精确版分别用直接启动 fixture 和 OpenClash v0.47.156 init 真实生成的活动 fw4 链验收。
- 实测确认 file provider 的热刷新语义：先原位写 provider 文件，再向已认证 `PUT /providers/proxies/candidate`（无 body），返回 204；认证 GET 返回刷新后的节点。带 body 的 PUT 虽然也返回 204，但不会把 body 当作 provider 内容。
- 实际新连接经 Mihomo HTTP 入站、`TEST` selector 和 file provider 到宿主 fixture `10.0.2.2:19872`，返回 `BACKEND-19872-OK`，宿主日志记录 CONNECT 与 GET。替换 provider 到无监听的 `19873` 后真实请求返回 502；恢复旧 provider 并再次 PUT 后新请求恢复 200。provider 变更、失败及回滚前后 PID/start ticks 都是 `7067/53953`。
- 将完整主配置 fixture 替换为模拟订阅文件（provider 定义路径与 `TEST.use` 保留）并调用认证 `PUT /configs?force=true` 后，provider GET、selector API 和新出站仍有效，core identity不变。这验证了文件替换/核心配置重载的持久性边界，不是 OpenClash 真实订阅下载器/parser 验收。
- 精确 `.7` core 上 authenticated PUT=204/GET=200；实际新请求经 file provider+`TEST` selector 到 19872 fixture 返回 `BACKEND-19872-OK`。将 provider 指向无监听 19873 后新请求返回 HTTP 502，恢复 provider 文件并 PUT 后请求恢复 200；PID/start ticks `16491/164648` 全程不变，GET `/group` 可用。完整主配置替换后 `PUT /configs?force=true` 返回 204；provider 与 `TEST` group 引用仍有效，新请求仍到达 19872 fixture，PID/start ticks 不变。该步骤模拟订阅文件替换和 Mihomo reload，不运行 OpenClash 订阅 downloader/parser。
- 精确 `.7` 下启动 OpenClash v0.47.156 daemon 成功，活动 fw4 `nat_output -> openclash_output` 链由服务实际生成，PID/start ticks `17222/170528`。原封未改的仓库 helper 在该链临时插入 `uid=65534/gid=1 + candidate IP + TCP/443` 规则；候选 `.10:443` 新流量到本地 fixture 得 HTTP 200 且未进 core 日志。`.10:8444`、非候选 `.11:443` 和 root 的 `.10:443` 都进 core 日志并使 `redirect :7892` 计数分别增长。helper assert/清理通过，代理 PID不变。非443 fixture 虽收到完整200响应，但 HTTP/1.0 listener 未及时 close 导致 curl exit 28；代理拦截由 core日志/规则计数证明。该项证明真实 OpenClash chain 下候选新连接旁路边界，不等于现有连接零中断。
- 全部复跑命令、哈希、配置、请求日志与边界见 `/tmp/openclash-runtime-isolated-20261120/acceptance-notes.md`。
- 同一 Cloudflare endpoint `www.cloudflare.com -> 104.16.132.229` 的直接路径与候选 helper 路径均返回 HTTP 200，记录到单次 connect/TLS/total 时间；候选路径 redirect counter 不变，非候选对照递增。只证明路由路径相符及单次计时观测，不是重复性能基准，也没有带宽数据。
- 仓库 `transaction.sh` 尚未直接接到该活动 guest：活动 OpenClash 配置没有 provider/use 引用，guest 缺少 resolver 所需 Ruby/Psych。为避免改写活动配置，没有继续构造该入口 fixture。CFI 与 relay 并发写入也未验证；这两项不得由 Mihomo API 手工热回滚证据替代。

### CFI 测速的停服保护与 PassWall 应用边界

- 打包 v2 中，`stop_service=0` 时以 PassWall 运行进程目录探测（不信任其 `running` init 动作）；如果进程活动且没有已验证在线绕行，测速在 CFST、停服和 UCI 写入前失败关闭。新安装默认关闭自动维护停服。旧安装的 UCI 既有值不会被升级覆盖；启用维护停服才允许过时服务中断路径。
- PassWall1 的 CFI 事务只持久化匹配节点的 `address` 和可选 `remarks`，结果为 `serviceApplied=false/applyPending=true/restartCount=0`；不会为应用新 UCI 自动重启服务。仍维护的 `cf-openwrt-auto.sh` 同样在活动 PassWall 且维护停服未启用时拒绝在线误测，并仅在事务已拥有维护停服时恢复服务；普通持久化不重启运行中的服务。
- 定向测试：`passwall-measurement-policy.sh`、`passwall-running-probe.sh`、`standalone-passwall-policy.sh`、OpenClash service/provider/bypass/restore/v2 transaction tests 通过；shellcheck、PO/POT `msgfmt --check`、`git diff --check` 通过。PassWall 容器/guest 的插件级生成配置和 HTTPS 出站不是 CFI 热应用证据。
- CFI 对 PassWall1 保持字段事务持久化、`applyPending=true`，不自动 reload/restart。现已加入 HomeProxy **仅配置持久化** mode，严格限定 `26.300.08316~d72e293`、普通 SOCKS main_node、`global|bypass_mainland_china|proxy_mainland_china` 简单路由、`main_udp_node=same` 和单个精确目标域名；一次只更新活动 main_node 的 top1 address，用户引用、包版本、UCI 冲突或 rollback preimage 变化即 fail-closed。`sync` 不操作 HomeProxy 服务；运行态 egress仍未验证。HomeProxy reload 经源码及插件级实验确认是 stop/start，不能称为热应用。
- HomeProxy 在 QEMU 的已安装包与 sing-box 1.12.25-r1 组合中，生成的 active config 曾因 `outbounds[2].routing_mark: unknown field` 被拒绝，child crash-loop；之前的 app-level fixture egress仅证明插件生成配置可代理 HTTPS，不证明 CFI 对真实生产节点的应用。复杂自定义 routing、独立 UDP、URLTest、未知 HomeProxy 版本均拒绝 CFI 写入。契约测试 `tests/homeproxy-persistence-contract.sh` 覆盖版本/路由/节点/UDP边界、top1、并发引用和字段回滚；它是 UCI stub contract，不是实时 HomeProxy 应用验收。
- PassWall2 单独举证：CFI 尚无 PassWall2 mode/adapter，故**不支持 CFI PassWall2 更新**。其 QEMU UCI 证明全局节点可能经 `_shunt` 及 `shunt_rules`/proxy_tag/ACL间接引用；插件 reload 会 stop/start。由于不能安全泛化该配置图，不把 PassWall1 事务代码复用到 PassWall2。版本、引用解析建议和拒绝矩阵见 `/tmp/immortalwrt-qemu-lab-20261120/cfi-plugin-uci-mapping.md` 与隔离 agent note。

### UPnP relay 与 OpenClash 入站绕行的真实内核证据

- 独立 ImmortalWrt 24.10.4 QEMU guest 中运行仓库 relay daemon 和真实 nft：新增映射后 TCP/UDP 规则生效；重复同步不改变 owned rule handles；超出 allowed range 的端口拒绝并清掉旧 DNAT；空映射重复同步保持零规则；恢复映射后规则恢复。真实 nft netlink batch 失败保留了旧 rule handles（16/17）。relay stop 清自有 table 并退出，start 后 PID 3198 重新持有有效规则，无 orphan instance/table。
- OpenClash v0.48.002/source-derived ingress 链实测：其 `mangle_prerouting` priority `-150` 早于 relay DNAT priority `-105`。lan_ac_traffic `RETURN` 因源 tuple 不匹配，计数为 0。路由器 WAN IPv4 `198.51.100.1` 在 OpenClash `localnetwork` 时 TCP/UDP 映射成功；别名 `198.51.100.3` 不在集合时代理 mark counter 增长且后端收不到请求。这是**本机 WAN IPv4 的条件性支持**，不是任意公网别名绕过。relay 当前目标/入口字段为 IPv4，IPv6 未支持。
- 本轮发现并修复 LuCI 旧按钮/翻译与 backend fail-closed 不一致：UI 不再暗示可生成/写入 lan_ac RETURN 规则或手动重启即可生效；相关 RPC 统一明确 unsupported。QEMU relay 的逐命令、规则句柄和流量记录见后附验收记录。
- 为验证能否让 relay 绕过 OpenClash 而在代理 mangle hook 前直接 DNAT，隔离 guest 比较了现有 `-105` 与实验 `-151`。`-151` trace 确认 DNAT 命中且目的地址已改写，但报文仍带 mark `0x1` 并进入 router input/fw4 reject，forward accept counter 为 0；localnetwork 模拟 RETURN 也未能解释实际路径。由于 hook/mark/route 链未闭合，该方案已撤销且不支持；guest 确认无实验表、规则、策略路由、namespace/veth、进程或 fixture 残留。复跑细节及限制见 `/tmp/upnp-relay-runtime-acceptance-20261120.md`。在匹配真实 OpenClash 版本、运行模式、内核与 WAN ingress 的环境补证前，不将 relay DNAT 优先级改为 `-151`。

UPnP QEMU 逐命令和清理记录现已归档至 `/tmp/upnp-relay-runtime-acceptance-20261120.md`。该记录特别标明 OpenClash 为源码链模拟而非 guest daemon/core，假 upnpc 不构成真实 IGD 交换；未捕获的 bootstrap/nft wrapper 命令明确标为未捕获。UI/RPC 收敛与回归测试现已完成，以上“正在修复”状态已结束。

### 本轮后续补验：完整 CFI 事务、HomeProxy 与 PassWall2（2026-11-20）

- **OpenClash CFI 完整事务入口已在隔离 guest 通过。** 在 ImmortalWrt 24.10.4 / OpenClash 0.47.156 / Mihomo `alpha-smart-g2e80ed7` guest 中，直接使用当前仓库 `transaction.sh`、provider/readback/helper 与 legacy renderer 执行 `prepare → apply → commit`。新建 VLESS/WS 连接经活动 selector+file provider 到 Xray 测试出口，返回 `BACKEND-19871-OK /txn-xray`。不可达候选新连接 HTTP 502，以及 1 秒超时候选两种失败都触发事务热回滚；provider 文件恢复为事务前快照、API GET 200，回滚后新连接恢复 HTTP 200。Mihomo PID/start ticks `17222/170528` 全程不变；没有 stop/restart。测试后活动主配置按 SHA256 `071953e889f5f369fc29f7c2161d98e03d70871cd4f72dfee8081c96ff56cb1c` 恢复并经 `/configs?force=true` 热加载，测试 provider/出口已清理。细节与复跑脚本见 `/tmp/openclash-runtime-isolated-20261120/acceptance-notes.md`。这证明应用无硬重启和热回滚；不证明订阅 downloader/parser、既有连接零中断或生产设备持久性。
- **HomeProxy 精确匹配环境现已运行。** 新建 ImmortalWrt 25.12.2 x86_64 guest，官方包 `luci-app-homeproxy 26.298.29275~c84adb9` + `sing-box 1.12.25-r1`，常驻 sing-box PID/start `8953/42654`。真实 SOCKS fixture 的混合入站经 HomeProxy 出站请求 HTTP 200。CFI 仓库 HomeProxy 事务实际执行 `prepare → apply → commit`，将 UCI address 更新并标为 `applyPending=true/serviceApplied=false`；事务后 PID/start、生成配置 SHA256 `98af88b16d7467a6c1a0666a0919ba7480a92ce831db13fd9993539de30e1448` 与真实 HTTP 200 均不变。后续事务改候选后由仓库事务回滚，UCI 和 ownership state 恢复、`rolledBack=true`，PID/config/hash/出站仍不变。测试完成将 lab UCI 恢复为域名、删除临时 ownership 文件及 guest SSH firewall 放行并复核 HTTP 200。结论是该精确组合 **配置持久化/事务回滚已验证，但代理运行态不热应用**；服务 reload 会影响运行，不在自动应用/回滚中调用。26.300.08316 仅保留源码/契约级限定，未在匹配核心环境实测。测试发现 OpenWrt 精简 jq 无 Oniguruma；已移除 JSON 校验对 `test()` 的依赖并加无正则库回归约束。
- **PassWall2 CFI 独立 adapter 已实现，支持面受限。** 仅精确 `luci-app-passwall2 26.11.11-r1`；递归核对全局根节点、`_shunt` 默认出口与实际分流引用，只有唯一活动 SOCKS 叶节点精确匹配目标域名、无 ACL/额外 SOCKS/链式节点/未知组/循环/歧义时才写入该叶节点地址。adapter 对服务只持久化，不 stop/restart；运行态应用标记待应用。使用上游 libuci CLI 的真实 UCI 合约已验证 prepare/apply/commit、二次事务回滚保留既有管理状态、并发用户改动冲突保留 journal，以及 ACL/链/坏引用 fail-closed；未能在目标 QEMU guest 运行同一 Bash 合约，因为该 guest 缺 bash 且额外依赖包源不可用。PassWall2 实际生成配置/核心新连接出站未验，因此只标“持久化组合有支持”，不标运行态热应用已支持。普通 PassWall1 与 PassWall2 不互相推断。
- **CFI 路径边界：** 自动在线旁路仅为 OpenClash 实测运行链中的“受控探测 UID + 精确候选地址 + TCP/443”短租约防火墙例外；现已实测 CFST TCPing 和 HTTPS HTTPing 的完整 `run_cfst` 调用。它不放宽代理全局流量或 DeepSeek/其他域名。能力探测失败时，显式 `stop_service=1` 会进入维护停服测量并恢复；`stop_service=0` 则失败关闭。CFI provider 应用/失败恢复无硬重启；匹配隔离 guest 的测速阶段没有停服，另有一条既有 HTTP 代理流在测量期间连续完成。单环境、单核心和单连接证据不代表所有连接、TUN/profile 或生产设备都零中断。
- **UPnP 路径边界再次明确：** relay 自身直接维护独立 nft DNAT batch，不改 OpenClash 配置，也不需代理热应用；该 relay 进程运行态增删/归零/重复同步/原子失败/启停已测。代理绕行不是 relay DNAT 的普遍副作用：PassWall2 仅 IPv4/TCP 返回规则已有真实流量证据；OpenClash 仅本机 WAN IPv4 属于运行态 `localnetwork` 时有链模拟证据，别名和 `-151` 前置 DNAT 不支持；HomeProxy fw4 入站路径曾有配置链证据，但在匹配活跃 core 下的 UPnP 真实流量仍待复测。不得声称 UPnP 绕行所有代理软件。
- 本轮本地复跑：CFI HomeProxy、PassWall 运行测速策略、OpenClash provider/bypass/service guard/v2 txn 定向测试通过；PassWall2 real-libuci 合约由测试提供程序构建的真实 `uci` CLI 执行通过。UPnP 五项运行热应用/防火墙边界合约及 JS 语法检查通过。HomeProxy/QEMU 测试后 UCI 与临时 firewall 规则已清理。CFI+UPnP 并发写者互撞、精确 HomeProxy 运行态 UPnP 绕行仍未实测。

| 能力组合 | 当前支持判定 |
|---|---|
| CFI + OpenClash 0.47.156 / Mihomo alpha-smart-g2e80ed7 | **file provider 事务热应用/失败热回滚与真实订阅更新后 provider/group 引用通过；CFI 不硬重启；OpenClash订阅更新自身会 stop/start；生产/旧连接零中断待验** |
| CFI + PassWall1 | **受限字段持久化；运行态应用待人工维护** |
| CFI + PassWall2 26.11.11-r1 | **受限、唯一 SOCKS shunt 叶节点事务已在目标架构/真实 libuci guest验证；运行中核心加载与出站待验** |
| CFI + HomeProxy 26.298.29275~c84adb9 + sing-box 1.12.25-r1 | **真实事务持久化/回滚通过；不热应用，运行态仍使用加载中的旧配置** |
| UPnP relay 自身 nft/fw4 | **隔离 guest 验证原子运行态同步；不调用代理服务** |
| UPnP + PassWall2 | **IPv4/TCP WAN 入站返回真实流量通过；UDP/IPv6待验** |
| UPnP + OpenClash | **只条件性支持本机 WAN IPv4 localnetwork；别名不支持；真实版本 WAN ingress 待验** |
| UPnP + HomeProxy | **匹配活跃 core 下手工等价 IPv4/TCP DNAT流量到后端通过；HomeProxy redirect链未命中，仓库 relay集成/专属RETURN命中未证明** |

### 后续验收更正（2026-11-20）

- **CFI + relay 并发写入已通过隔离 guest 验收。** 真实 CFI provider PUT=204 后由 wrapper 延迟 2 秒，仓库 relay CLI `--sync` 在 CFI 回读之前完成真实 nft batch，安装 TCP 49322 DNAT handle 3；provider GET=200、新代理流 HTTP 200，核心 PID/start `17222/170528`。显式 CFI rollback 恢复 provider 原 SHA/节点 `edge`，49322 relay rule 仍存在，回滚后新代理流仍 HTTP 200。marker 时间线、对照哈希、命令和清理见 `/tmp/openclash-runtime-isolated-20261120/acceptance-notes.md` 的 `deterministic overlap passed`。
- 并发实验使用 test-only fake `upnpc` 产生映射，不是实际外部 IGD 或公网 WAN ingress；它验证了当前仓库的 relay parser/sync/nft batch 与 CFI Mihomo provider 事务重叠、互不删除对方 owned state。失败超时跨组件互斥注入和真实路由器 ingress 仍未验。
- 最终清理已核对：relay 表/UCI/state、CFI fixture/provider/runtime、Xray、临时包与 journal 均已移除；原 OpenClash 主配置 SHA `071953e...ff56cb1c` 恢复并热加载，`TEST=sink`、测试 provider API 404，core PID/start未变。
- HomeProxy 精确包组合下的 UPnP packet-level复测未获得有效 WAN→DNAT→LAN backend：25.12.2匹配 guest 缺 `ip-full/netns/kmod-veth`，创建 veth 返回 `RTNETLINK ... Not supported`；另一次 COW 双网卡 QEMU未启动到 SSH/串口可用状态。因此没有发送有效映射流量或宣称 HomeProxy 运行态 UPnP 支持。该 COW尝试未改 base image/vars，也没有加规则或启动 backend。
- `openclash-document-boundary.sh` 后续用 GNU sed 与 Homebrew portable Ruby 4.0.9/Psych 5.3.1重跑通过；旧记录中 macOS BSD sed/Ruby 2.6 的失败只代表当时主机工具链限制。

### OpenClash 实际订阅更新器补验（2026-11-20）

- 使用 2224 隔离 guest 里的 OpenClash `0.47.156`，只添加一个 loopback HTTP `config_subscribe` section（`name=config` 精确命中），将订阅入口限制到本地校验过的 fixture。fixture 的 Clash YAML 与 file provider 都经匹配版 core `clash -t` 验证；main config 的 `proxy-providers` 与 `TEST.use`/`proxies` 引用有效，guest `router_self_proxy=0` 确保下载失败 fallback 不会改代理旁路配置。
- 实际运行 `/usr/share/openclash/openclash.sh config` 一次。日志确认成功下载/校验/替换选中源，随后进入 `dec_job_counter_and_restart` 并调用 OpenClash init `restart`；测试 guest 内明确 stop Mihomo、重建防火墙/DNS，再 start。Core PID/start 从 `17222/170528` 变为 `4290/928878`。这是订阅 updater 自身的行为证据，不属于 CFI provider apply/rollback；CFI 热应用仍不调用代理 restart。
- 更新后活动 main config 仍有 provider `type=file`、path `./proxy_provider/provider.yaml`，`TEST` 保持 `use:[subscription-fixture]` 与既有 `proxies:[update-sink]`。把测试 sidecar 放在 OpenClash 规范化的实际路径后，认证 provider PUT=204/GET=200，`TEST` group API含 `update-sink`/`sub-edge`；选择 `sub-edge` 后新的代理请求到真实 Xray fixture 返回 HTTP 200。首轮 sidecar 临时放错目录导致 provider API 200但空列表；修正到 canonical path 后上述全部通过，且未再次运行 updater。
- 结束时恢复 active config、选中源、OpenClash UCI、DHCP UCI、firewall UCI 五份原快照；经认证 `/configs?force=true` reload 204。dnsmasq 仍运行且 reload 成功，fw4 OpenClash chains 存在，原 UCI hash精确匹配，`TEST=sink`、fixture provider API 404，真实代理新连接 HTTP 200。发现并恢复了 init restart 对 `/etc/config/dhcp` 的改写，恢复后 hash 与 snapshot `cmp=0`。临时 source/订阅 UCI、provider/Xray/listener/package、job/lock/test目录均清除。完整日志、哈希和命令见 `/tmp/openclash-runtime-isolated-20261120/acceptance-notes.md` 的 subscription updater 段。
- 此实测只覆盖精确 OpenClash 版本和受控单订阅 fixture；说明引用在订阅文件替换/restart 后仍有效，不证明真实 CDN订阅内容、其他转换器或生产订阅模板持久性，也不证明订阅更新无中断。

**最终并发判定：CFI provider事务与 relay实际 nft batch 的真实重叠、CFI回滚后 relay规则保留、新连接仍可用，均已在隔离环境证实；跨组件故障注入和 HomeProxy真实UPnP映射仍待验。**

### 最终工作区与隔离环境复核（2026-11-20）

- CFI 定向回归组（OpenClash provider/bypass/service-guard/v2 transaction、新增完整 CFST runner、HomeProxy persistence、PassWall 测速策略、PassWall2 real-libuci 合约、legacy sync/stop-failure）退出码为 0；UPnP fw4 timeout、nft atomic rebuild、OpenClash unsupported boundary、enable transition、runtime hot-apply 合约均通过。`git diff --check` 在 CFI、UPnP、SWrt 验收报告均通过。测试输出中 legacy sync 脚本的临时 `services.log` grep 有两条 stderr，但对应断言通过且总退出码为 0。
- OpenClash 隔离 QEMU 2223/2224 已在 guest 内执行关机；本轮新建 CFI COW QEMU 2225 也已由 guest 关机，CFI candidate rule/set 清零、fixture server 停止。CFI backing qcow2 SHA256 前后均为 `fa34273ad4ae33639643011eb37c8af99bb9b08343efbaec953b96f83f7b5bed`。
- 前述不可控的 UPnP QEMU PID 72521/2222 在本轮复核时已自然退出；`qemu-system-aarch64`、2222/4444 与相关 fixture 端口均无监听。没有向该实例发宿主信号，也未修改其镜像。
- `spec.md`、`tasks.md` 原始历史快照保持未修改。没有部署生产设备、刷机、推送或合并。

### 最终能力边界

- “CFI OpenClash provider 应用无硬重启”已在匹配版本隔离 guest 验证，包含真实出站、失败/超时热回滚、订阅更新后的 provider/group 引用，以及与 relay `--sync` 并发。当前仓库完整 CFST runner 的 TCPing 和 HTTPS HTTPing 测量段也在匹配活动核心下无停服运行；一条既有 HTTP 代理流跨越测量前后完整收到 18 个数据块。生产/其他拓扑和更广范围的连接零中断仍待验。
- CFI + PassWall1、PassWall2、HomeProxy 当前都不宣称运行态热应用：已验证的范围是受限字段持久化、事务所有权/回滚及 `applyPending` 告知；实际核心新出站切换未验，需人工应用或后续实现经版本验证的无重启机制。PassWall2 与 HomeProxy 不能由 PassWall1 的证据类推。
- UPnP relay 自身可运行态管理自有 nft DNAT，覆盖映射增删、归零、重复同步、原子失败和启停；不调用 OpenClash/PassWall/HomeProxy 服务。代理侧绕行逐组合判定：PassWall2 IPv4/TCP 有真实入站流量证据；OpenClash 仅本机 WAN IPv4 落在运行态 `localnetwork` 条件时有链证据；HomeProxy 活跃 core 下仅证明手工等价 IPv4/TCP DNAT 到达后端，未证明仓库 relay 集成或 HomeProxy 专属 bypass 链命中。UDP、IPv6、其他入口/别名组合均按矩阵待验或不支持。
- 因而“应用无硬重启”对 CFI OpenClash provider 和 relay 自身成立；CFI OpenClash 测速阶段在上述版本/模式/已识别链条件下无停服，其他组合仍须按 fail-closed/显式维护分支处理。无法宣称全设备“全程无停机”或普遍“已有连接零中断”。外部 IGD/WAN ingress、生产持久性和未列出组合要在对应网络/固件版本复测后才能提升支持等级。

### HomeProxy 匹配运行核心下的 IPv4/TCP 入站路径补验（2026-11-20）

- ImmortalWrt 25.12.2 x86_64 / kernel 6.12.103；HomeProxy `26.298.29275~c84adb9` + sing-box `1.12.25-r1` 活跃，PID/starttime `3329/1554`。为建立 namespace 流量拓扑，在专用 COW overlay 安装精确 ABI 的 `kmod-veth-6.12.103-r1`、`ip-full-6.18.0-r2` 及依赖；核心在安装和流量测试前后身份不变，HomeProxy UCI 与生成 JSON SHA 不变。
- 隔离 WAN namespace 请求 `9.9.9.2 → 9.9.9.1:2223`，测试 NAT priority `-105` 将流量转发至独立 backend `192.168.50.2:55005`。真实 TCP/HTTP 返回 `HomeProxy-DNAT-backend`，wget=0；DNAT counter=1 packet/60 bytes，fw4 临时 forward accept counter=15 packets/853 bytes。
- nftrace 显示 `fw4 mangle_prerouting → upnptest prerouting DNAT → fw4 prerouting/filter → forward`；本次包未进入 HomeProxy `fw4 dstnat` / `homeproxy_redirect_lanac` 路径，HomeProxy redirect/local-address counters未变化。因此只证明**活动 HomeProxy core 运行时，这个 IPv4/TCP WAN DNAT tuple 到达 backend 且未被 HomeProxy redirect 截获**；不是 HomeProxy RETURN 规则命中证明，也没有在该 guest 运行仓库 relay daemon。不能外推 UDP、IPv6、其他接口/规则组合或 relay 完整生命周期。
- 已清理临时 nft table/forward rule、namespace、veth、server；丢弃 COW overlay/vars copy，QEMU/临时文件服务器停止。overlay 内曾将 `net.ipv4.ip_forward=1`，base image/vars 哈希未改。完整包 hash、trace/counter 和清理输出见子任务 note `/tmp/upnp-relay-runtime-acceptance-20261120.md`。

**HomeProxy UPnP 最终判定：匹配活跃 core 的 IPv4/TCP 入站 DNAT 机制流量通过，但未由仓库 relay 生成该 guest 规则，也未证明 HomeProxy 专属 bypass RETURN 链命中；仅按该包/内核/tuple 的机制支持记录，真实 relay 集成及其他协议保持待验。**

### HomeProxy sing-box 信号重载边界复核（2026-11-20）

- 对确切运行版 sing-box `1.12.25` 检查上游 tag 源码 [`cmd/sing-box/cmd_run.go`](https://github.com/SagerNet/sing-box/blob/v1.12.25/cmd/sing-box/cmd_run.go)：SIGHUP 处理先 `check()` 新配置；检查失败则继续用当前实例，成功后调用旧实例的 `cancel()`/`Close()`，随后循环重新 `create()` 新实例。操作系统进程 PID可能保持，但代理实例会关闭后重新构建，既有连接和新实例间隙不能保证无中断；不能把它当作文件型 provider 那种原位热刷新。
- 精确 HomeProxy guest 已测 `/etc/init.d/homeproxy reload` 会执行服务 `stop; start`，并由 init 生命周期撤销/重建自身 fw4 链、集合、策略路由及 DNS 配置；UCI → 生成 sing-box JSON → HTTP egress 在重载期间出现一次 HTTP 000/拒绝连接。故 CFI 自动应用/失败恢复不发送 SIGHUP，也不调用 HomeProxy reload；继续标记为 `applyPending=true`，直到有插件/core提供并通过真实流量证明的安全原位 API。
- 因此目前“HomeProxy 更新后进程 PID未变”只意味着 CFI 未触碰运行实例，**不代表新地址已应用**。用户显式启用维护停服时会恢复 HomeProxy，但那是维护启动且存在中断，不是运行态热应用。

### dns-guard 生产阈值故障窗口补验（2026-11-20）

- 在 2224 ImmortalWrt guest 中运行当前 YunWrt `openclash-dns-guard.sh` 实际脚本副本，注入 primary `653` 失败、fallback `953` 一直成功，阈值采用当前配置 `probe_interval=15s / alert_after=90s / degrade_after=140s`。只把路径重定向到临时测试目录，并用临时 UCI/nslookup/curl/dnsmasq-reload shim 隔离外部动作；`suspend_watchdog=0`、`manage_ip_rule=0`。真实 guest `/etc/config/dhcp`、OpenClash、防火墙和 DNS 运行配置未触碰。
- guest 起始于 `21:20:00`，90 秒时同一故障事件只产生一次 alert；`21:22:30`（elapsed≈150s，653 failed 11 次、953 ok）切到 `127.0.0.1#953` fallback，符合 `140s + 15s poll` 且早于 180s上限；alert+degrade共2条。置 recovery marker 后角色回 primary、原始 `server=127.0.0.1#653`、`noresolv=1`、`resolvfile` 精确恢复，只追加一次 recovery通知，总通知数3。测试脚本断言整体通过。
- EXIT trap清除测试目录/guard，另核对无守护进程残留；真实 guest live DNS仍是原 `127.0.0.1#7874`，OpenClash core 与 DHCP/firewall hashes未变。此项证实真实脚本在 Linux guest 内按配置阈值执行的故障注入时间边界和通知去重；resolver、dnsmasq与Bark由 shims模拟，仍不等同于真实外网DNS/硬件故障。

### PassWall2 目标架构事务补验（2026-11-20）

- COW guest：ImmortalWrt 24.10.4 `armsr/armv8`、kernel 6.6.110、PassWall2 `26.11.11-r1`。把当前仓库 adapter/contract 放入目标架构 Linux guest，以实际 `/sbin/uci`、临时 `-c/-t` 配置与 delta 目录执行 `tests/passwall2-persistence-contract.sh`；6 个事务/配置/测量策略用例全部通过（`CONTRACT_RC=0`）。覆盖 prepare/apply/commit/rollback、ownership 状态恢复、用户外部字段编辑冲突保留、活动节点图 fail-closed、无 init 服务动作和活动代理测量门禁。
- 首次运行的测量守卫用例因 test stub 放在 source 前、被被测脚本函数重定义覆盖而失败；将两项 stub 移到 source 后重跑通过。这是测试 harness 修正，不是 adapter 源码改动。
- 测试使用独立 COW 和临时 UCI fixture，没有改 guest 活动 UCI 或启动/停止 PassWall2 服务；COW VM、overlay、复制 backing、上传 payload、临时 HTTP server 均已清理。完整输出见 `/tmp/passwall2-guest-txn-evidence-20261120.md`。
- 这证实 PassWall2 的目标架构 libuci 事务持久化/回滚，不证明正在运行的 PassWall2 核心重新载入候选或实际出站切换；该组合仍显示 `applyPending=true`。

### CFI 完整 CFST 测量阶段无停服补验与 runner 修复（2026-11-20）

- 用匹配版本的 OpenClash guest 原盘创建新的 qcow2 COW overlay，运行 ImmortalWrt 24.10.4 / kernel 6.6.110 / OpenClash 0.47.156 / Mihomo `alpha-smart-g2e80ed7`。active core `4176/1940`，OpenClash init/fw4 output 链在运行。仅将当前仓库 `cf-ip-auto-v2` 与库复制进 guest overlay；活动 profile 未改写，选择组为 `TEST=sink`。backing image SHA256 在测试前后相同：`fa34273ad4ae33639643011eb37c8af99bb9b08343efbaec953b96f83f7b5bed`。
- 用仓库真实 `stop_service_for_measurement` 与 `run_cfst` 函数、真实官方 [CloudflareSpeedTest v2.3.5](https://github.com/XIU2/CloudflareSpeedTest/releases/tag/v2.3.5)，候选仅 `104.16.132.229`、HTTPing、每 IP 1 次延迟/1 个下载候选、`dt=6s`。`stop_service=false`；函数日志明确选择低权限 `uid=65534/gid=1` 候选 TCP/443 例外，test wrapper 记录 `stop_calls=0` 且 `CFIP_STOPPED_MODE` 为空。`run_cfst` 返回成功，解析到 `104.16.132.229,1,1,0.00,223.78,0.29,AMS`。OpenClash TCP redirect 计数测试前后保持 `6 packets / 360 bytes`；进程 identity 仍为 `4176/1940`；退出时 `cfip_` 规则/集合无残留。
- 同一 CFI bypass helper 下分别执行两轮实际 CFST 对照：direct candidate exception 的轮次不增加 OpenClash redirect 计数；关闭 exception 后相同 TCP/443 candidate 的 HTTPing/下载流量被 fw4 redirect 到本地核心，计数每轮增加两个。每次单样本下载速度波动明显，因此只把这个对照用于确认路由路径，不当作可复现吞吐基准。运行证据和 CFST CSV 留在 `/tmp/openclash-runtime-isolated-20261120/cfst-online-exact-fullrun.log` 及相邻 `cfst-online-*` 文件。
- 额外在 HTTP 代理入站上持续打开一个 18 秒流，测试前、CFST 测量期间和之后继续读取；fixture body 的 chunk ID `0–17` 各到达一次，服务端时间从 `13:53:31.842712Z` 至 `13:53:48.902525Z`。CFST 成功，核心 PID/start 不变，临时例外按 owner 清理。它证明这个隔离 HTTP proxy 既有连接跨越本次测速仍完整结束，不证明所有 TUN/UDP/上游代理连接或生产拓扑零中断。stream body 与精确复跑脚本见 `/tmp/openclash-runtime-isolated-20261120/cfst-online-proxied-existing-stream.txt`、`cfst-online-exact-fullrun.sh`。
- 实测还发现此前 `run_cfst` 把 shell 函数名 `cfip_openclash_measurement_command` 交给外部超时 runner，runner 将其作为二进制启动，导致 CFST 未运行。已改为在原有有界超时内直接调用 `start-stop-daemon -S -c nobody:daemon -x probe-exec -- cfst ...`，并在启动前/结束后检查绕行规则与 OpenClash identity；HTTPing 路径扩展为与 TCPing 共用精确候选 TCP/443 例外，`udp/其他协议`仍拒绝。新增 `tests/openclash-cfst-bypass-runner.sh` 并接入 CI；runner 与 bypass 合约测试通过。
- 此项使匹配环境的 **CFST 测量阶段无停服**由推断变为运行态证据。整次 `cf-ip-auto-v2 --run` 的输入获取、候选策略、provider 提交虽各有独立证据，但未在一条生产等价 full-run 里合并验收；无法识别活动链/凭据、缺 helper、协议不支持时，`stop_service=0` 会失败关闭，显式 `stop_service=1` 仍是可见维护停服路径。

### HomeProxy 真实 relay 流量及 UCI deny-list 修复补验（2026-11-20）

- 当前仓库 relay 之前用 `uci get upnp_nat_relay.default.port.0` 读取 UCI list；HomeProxy 匹配 guest 的真实 `/sbin/uci` 证实该路径返回失败，而 `.default.port` 返回空格分隔的整个列表。现已改为整表读取；新增 `tests/uci-deny-port-list-contract.sh` 并接入 real-LuCI workflow。契约验证自定义 deny ports 生效且列表为空时恢复安全默认值。
- 在 HomeProxy COW guest 用真实 UCI 与当前仓库 relay CLI 执行 `--sync`：默认 deny 列表中 443/8443 产生 `accepted=2/rejected=4`；删去默认列表项并配置自定义 ports 后为 `accepted=4/rejected=2`。证明了真实 UCI 列表到 relay allow/deny 解析的闭环。
- 真实 namespace 流量通过 TCP 55005 和被加入映射的 TCP 8443 到达后端 HTTP 服务并返回 200；8443 同时在 HomeProxy common routing set 中，relay DNAT 先于 HomeProxy NAT redirect，HomeProxy redirect 计数不变。非 common 端口 UDP 55006 到达后端。UDP 443 命中 HomeProxy TPROXY hook，HomeProxy TPROXY 计数增长而后端计数不变；因此不能宣称所有 UDP 绕行。IPv6 映射与 relay 的 `dnat ip` 规则不匹配，随后被 router input reject；IPv6 仍不支持。
- 整体结论只对该 HomeProxy/sing-box guest、IPv4、TCP relay DNAT tuple 有运行态证据；relay 自有防火墙规则无需代理服务重启。IPv4/UDP common 端口和 IPv6 不在当前支持面，尚不能宣称 HomeProxy 全协议绕行。测试结论不等于外部真实 IGD/WAN ingress 或生产设备验收。
- 清理复核：relay nft 表、测试 fw4 规则、namespace/veth、HTTP fixture、relay 临时配置和 SSH 临时密钥均已移除；HomeProxy UCI SHA256 前后未变（末尾 `c8f4d8ae`），guest 经 QEMU monitor `system_powerdown` 后退出，SSH/console 端口无监听。COW backing image SHA256 前后未变（末尾 `d57f9fc`）；Dropbear interface 恢复 `lan` 后因该孤立 guest 无 LAN 地址曾提示无可监听地址，随后 guest 正常关机。完整证据 `/tmp/upnp-homeproxy-integration-20261120/evidence.md`，流量追踪为 `tcp-trace.log`、`v6trace.log`。
- 最终回归：UPnP `fw4-detection-timeout.sh`、`nft-atomic-rebuild.sh`、`openclash-inbound-bypass-unsupported.sh`、`reload-enable-transitions.sh`、`runtime-hot-apply-contract.sh`、`uci-deny-port-list-contract.sh` 与 `git diff --check` 均通过。CFI 本机可执行的 OpenClash transaction/bypass/CFST、HomeProxy 持久化、PassWall 测速策略与 ShellCheck 检查通过；PassWall2 real-libuci 合约需要 `UCI_REAL_BIN`，本机未构建，因此本机批次未跑该项；其目标架构 guest 的真实 libuci 测试另有通过证据。

### 本轮最终支持判定

| 子系统 | 已运行态支持 | 明确边界 |
|---|---|---|
| CFI → OpenClash provider | 精确版本隔离 guest 中当前仓库 transaction runner 热应用、失败/超时热回滚、实际新连接出站、订阅更新后引用仍有效；Mihomo 身份不变且无 CFI 代理硬重启 | 生产设备未写入；订阅更新自身会 stop/start；既有连接零中断只观察到一条 HTTP 流；其他 profile/TUN/核心仍待测 |
| CFI 独立版 → OpenClash provider | `cf-openwrt-auto.sh` 可将候选交给打包 v2 `--apply-selected`；成功 hotApplied，错误活动 config path 在写入前拒绝，失败候选事务 owned hot rollback | 要安装打包 v2；UCI 目标域/后缀/transport 和活动 `-f` 配置须匹配；standalone在线测速无已验证旁路，默认运行时拒绝，显式维护停服时需先恢复并健康核验再委托。成功出口实测由 test-only post-probe 走本地 Xray，生产候选 HTTPS probe仍待单独验收 |
| CFI → OpenClash 测速 | TCPing 和 HTTPS HTTPing 完整 CFST 测量段通过候选 IP + TCP/443 临时防火墙例外；无 OpenClash redirect 增量，CFST 实际运行成功 | 仅匹配 guest 的精确链/核心及已支持协议；bypass 无法证实时，默认失败关闭，用户显式维护停服仍会中断。此例外只对受控候选探测生效，不改写 DeepSeek/其他域名的路由策略 |
| CFI → PassWall1/2、HomeProxy | 受限配置事务持久化与回滚，不自动触碰代理服务 | 当前没有经流量证明的无停机原位应用 API；保持 `applyPending`，不得显示运行态已切换 |
| UPnP relay 自身 | 运行中读取配置，真实 nft batch 原子增删/归零/重复同步；UCI list、端口、协议及下游目标策略受限；不重启代理 | 本轮测试不等于外部真实 IGD/WAN ingress；目标固件仍需复测 |
| UPnP + HomeProxy | 匹配隔离 guest 下仓库 relay 实际 IPv4/TCP DNAT 到达后端；核心身份与配置 hash 不变 | HomeProxy TPROXY 可先截获 UDP common 端口；IPv6 relay DNAT 未实现/未通过；仅按已测 IPv4/TCP tuple 支持 |
| UPnP + PassWall1 | 暂无运行态支持结论 | 源码显示 nft backend 有 IPv4 WAN-IP 前置 RETURN，但没有 PassWall 活跃规则计数或真实 relay 流量证据；当前隔离资产缺可启动插件依赖和代理核心，待提供同版本运行包复测 |
| UPnP + PassWall2 | IPv4/TCP WAN 目标 RETURN 真实入站流量通过 | UDP、IPv6 及其他插件版本/模式待测 |
| UPnP + OpenClash | 匹配 guest 内 relay 自有 DNAT 的 TCP 主地址/alias 请求都到达后端；UDP 主地址/alias 报文到达 downstream namespace；核心 PID 与 OpenClash UCI 不变 | `lan_ac_traffic` 对入站映射不支持并 fail-closed。测试 guest 的 OpenClash 活动规则集没有 TPROXY action，不能证明实际启用 TPROXY 的 UDP 绕行；当前拓扑以 veth namespace 模拟 WAN ingress，不替代生产物理 WAN |

“不调用硬重启”已有 CFI OpenClash provider 和 UPnP relay 自身的实现/隔离运行态证据；“全程无停机”仅对匹配条件下的 CFI 测速阶段及无代理重载的 relay DNAT 更新成立，不能外推全部事务/组合；“已有连接零中断”仅有一条 OpenClash HTTP 连接的单样本连续流证据。

### OpenClash 活动 guest 的 relay WAN-ingress 路径补验（2026-11-20）

- 在 OpenClash 0.47.156 / Mihomo `alpha-smart-g2e80ed7` ARM64 COW guest 中启动实际 daemon/core（PID/start `4177/1937`），通过 veth namespaces 模拟 WAN ingress，执行当前仓库 relay `--sync` 安装独立 `inet upnp_nat_relay` DNAT。TCP 主地址 `10.0.2.15:55005` 与 alias `198.51.100.99:55007` 的首包均在 relay `prerouting` priority `dstnat - 5` 命中并 DNAT 到 downstream `10.33.0.2`，测试 forward rule 放行后返回真实 HTTP body。nft trace 显示首包未进入 OpenClash TCP dstnat child；规则次序证实 relay 可不改 OpenClash UCI、直接对防火墙流量做 DNAT。
- UDP 主地址 `10.0.2.15:55006` 在 OpenClash mangle 先命中 `ip daddr @localnetwork return` 后再进入 relay DNAT；UDP alias `198.51.100.99:55008` 则经过 OpenClash `openclash_upnp` child（该 guest 的链为空）再进入 relay DNAT。两条实际 datagram 到达 downstream namespace PREROUTING 计数，但该 guest 没有 UDP 应用监听，因此此项只证明报文到达，不证明 UDP 应用层转发成功，也不证明有活动 TPROXY 时的代理绕行。
- OpenClash 自有 setup/generate/sync 三个 `lan_ac_traffic` 入口均返回 `openclash_inbound_bypass_unsupported`；UCI hash不变且无相关 section。测试期间真实 HTTP proxy 请求为 200、core PID/start ticks 不变。`lan_ac_traffic` 的源地址/源端口规则不能表达此 DNAT 入站路径，故保持 fail-closed。
- 该 guest 只有 `br-lan` 地址 `10.0.2.15`，并非实际 WAN 设备；测试入口是 veth，因此结果限定为隔离 hook/route 拓扑。没有更改代理配置或注入 TPROXY 规则来制造支持结论。测试表、fw4 规则、namespace/veth、fixture 和临时包均清理，guest 正常关机，QEMU 端口关闭且 backing image SHA 未变。trace 与清理证据见 `/tmp/upnp-openclash-ingress-20261120/evidence.md`、`nft-trace-key-paths.log`、`nft-trace-full.log`。

### 独立 CFI CLI 委托 OpenClash provider 事务补验（2026-11-20）

- 独立 `cf-openwrt-auto.sh` 只生成 root 私有 selected JSON 并委托 `/usr/bin/cf-ip-auto --apply-selected`；provider 解析、writer lock、事务备份/写入/readback、实际出站验证与所有权回滚都归打包 v2。入口在锁内核对候选结构/IP、UCI目标域/节点后缀/transport，并核对活动 core `-f` 路径。包缺失/配置不一致失败关闭。独立在线测速没有已验证防火墙 bypass，核心活动时默认拒测；用户显式允许维护停服时必须先恢复由本次任务拥有的停服并验证 PID/controller 后才提交 provider。
- 匹配 ImmortalWrt/OpenClash guest 中使用当前仓库 v2 CLI、helper、legacy renderer 实测成功委托：结果 `hotApplied=true`、`applicationMethod=file_provider_put`、`restartCount=0`；new proxy request经 `TEST[edge [CF-1]]` 到本地 Xray/HTTP fixture 返回 `CFI-BRIDGE-EGRESS-OK`，Mihomo PID/start `4155/1935` 不变。此成功轮为本地节点使用 test-only post-probe适配了不能代表公网 CDN TLS的VLESS fixture；它证明事务热刷新和实际新连接出站，不证明生产 HTTPS候选后验探测。
- 不可达候选失败轮由事务真实热回滚：provider bytes恢复至 preimage，authenticated API GET仍为200，返回 `rolledBack=true/recoveryRequired=false`，PID/start不变。活动 config path不匹配轮在写 provider前拒绝，报告 `nodeFileWritten=false`，provider bytes与 core identity不变。原主配置恢复原始SHA `071953e889f5…ff56cb1c`，OpenClash UCI恢复字节一致，TEST selector恢复 `sink`；CFI UCI、provider/Xray/uhttpd/IPK测试件已清除。
- 两个新增 bridge/standalone合同已接入 CI；旧 standalone fail-closed测试已改为验证 v2 委托要求，打包 legacy binary的直写/整配置恢复依旧 fail-closed。本机 focused regression、ShellCheck、`bash -n` 和 `git diff --check` 退出0。guest已 poweroff，SSH/串口/fixture端口关闭；COW overlay保留约30 MiB供复跑，backing guest SHA未变。日志与JSON见 `/tmp/openclash-standalone-guard-20261120/evidence/README.txt` 及其 `cfi-bridge-evidence/`。

### PassWall1 relay 绕行环境可用性探查（2026-11-20）

- 尝试在 ImmortalWrt 24.10.4 aarch64/kernel 6.6.110 独立 COW guest 中运行当前 relay 与 PassWall1。guest 有 `CAP_NET_ADMIN`，匹配 `kmod-veth 6.6.110-r1` 成功加载并可创建/删除 veth；但是现有本地资产只有 PassWall LuCI `26.11.11-r1` 包/源码，没有足以启动运行栈的插件依赖及 Xray/sing-box 等代理 core。
- 为避免手工构造 OpenWrt PassWall nft 规则后误称真实插件已验收，本轮没有运行 PassWall init/backend脚本、没有生成 PassWall 活动链、没有发送 TCP/UDP relay 流量，也没有获取 hook 计数。因此仍只保留源码的 IPv4 WAN-IP 前置 RETURN 推断；该组合**不列为已支持**。
- COW guest、vars、key、临时解包目录已清理并关机；host端口关闭，backing guest SHA 未变（`fa34273ad4ae33639643011eb37c8af99bb9b08343efbaec953b96f83f7b5bed`）。复测需提供与目标 firmware/kernel ABI 匹配的完整 PassWall1 运行包 feed及代理核心（至少插件依赖+所选 core），再按现有 UPnP veth/WAN flow合同验证活动 `psw_wan` RETURN 计数、后端连接、协议/端口边界和核心 PID；此前本地隔离测试日志未形成可采纳的 PassWall1 flow 证据。

### 实机 OpenWrt CFI file provider 原位刷新（2026-11-21）

- 实机 OpenWrt 25.12.5 x86/64、kernel 6.12.94；活动 `clash_meta` PID/start ticks `28382/58572999`，`-f` 配置 `/etc/openclash/yunshu.yaml`。活动配置引用 file provider `本地节点`，provider 路径 `/yunshu/clash/clash-proxies.yaml`；代理组 `自建节点` 当前指向 `自用cf负载均衡`，该 Smart 组包含四个 CFI 节点。运行期间没有更改 OpenClash UCI、代理组选择或主配置。
- 实机已安装的 CFI 包缺 `ruby-json`。首次执行在写文件前失败关闭；配置解析还发现主 YAML 中有数值端口形式的 Symbol。仓库已补 `ruby-psych`/`ruby-json` 运行依赖，并把解析许可限制在 YAML 中实际出现的 1–65535 数值端口 Symbol。实测使用当前仓库 staging 代码，并从设备已配置的 OpenWrt feed 将 `ruby-json-3.4.9-r1` 仅下载、解包到 `/tmp` 供本次进程加载；未安装系统包。因未部署新 CFI 包，设备当前已安装包仍未获得此依赖/代码修正。
- 通过当前候选顺序的逆序 `104.18.206.21,104.18.246.192` 执行 CFI `--sync openclash`，结果 `hotApplied=true`、`applicationMethod=file_provider_put`、`restartCount=0`；认证 API readback 保持五个 provider 节点名。随后经当前 `自建节点` 组向 Cloudflare trace 发起 Mihomo delay 新连接，API HTTP 200、285 ms。该请求证明这条活动代理组路径可新建出站连接；API 仅返回延迟，不暴露实际命中的 CF-1/CF-2 或出口 IP，因此不作为精确出口地址证明。
- 使用同一事务恢复原候选顺序，结果同样为 `file_provider_put` / `restartCount=0`。provider 文件 SHA 恢复为基线 `9a6b355369db1cd9859eff81a0681efe2a9819aa9ac6b95ff7d6ab1c723a21e8`；CF-1=`104.18.246.192`、CF-2=`104.18.206.21`。活动主配置 SHA 与测试前一致（`88de12cfa963cb1a51030782ebe803b2057349a32136f1141a20d2c544638a25`），核心 PID/start ticks 未变；`/etc/config/openclash` 与 `/etc/config/cf_ip` 均和快照逐字节相同，事务锁已释放。provider 目录保留本次事务生成的两个备份（00:36 的原 provider、00:39 的逆序 provider）以及一份测试前已有备份，未删除任何备份。
- 本轮证据支持“此实机核心/配置上的 provider PUT 可应用、无需硬重启、一次新连接可出站”。没有进行订阅更新；没有测量连续流或已有连接，因此不判定全程无停机或已有连接零中断。下一步需构建包含新增 Ruby 依赖的 CFI 包并另行验收安装版；当前实机已安装包仍不能独立复现本轮 staged-code 结果。
