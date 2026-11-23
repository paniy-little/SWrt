# 任务分解

## Phase 0 — 取证（已完成，只读）

- [x] T0.1 恢复设备 SSH（口令已重新载入）
- [x] T0.2 取原文：`/usr/sbin/openclash-dns-guard.sh`（373 行，sha256 `faf8bc1a…1072`）、`/etc/init.d/openclash-dns-guard`、`/etc/config/openclash-dns-guard`
- [x] T0.3 确认通知通道：Bark（`api.day.app`，group=openwrt，timeSensitive），失败入队、653 恢复后补发
- [x] T0.4 确认探测方式：`nslookup github.com` 打 653/953，非进程检测
- [x] T0.5 确认降级实现：dnsmasq 上游切 953 / WAN；`ip rule` fwmark 0x162 table 354 pref 1888 增删
- [x] T0.6 确认 procd：`respawn 0 5 0`，`S96`/`K10`，当前 pid 6430
- [x] T0.7 日志取证：7 次降级，每次 52–101s，2 条 Bark；cron `30 7,18,23 * * * cf-ip-auto --run` 逐次命中
- [x] T0.8 发现 `suspend_watchdog` 无效、`ORIG_*` 未用于恢复、无防抖

## Phase 1 — 通知与降级改造（设备侧，待批准）

- [ ] T1.1 备份 `/usr/sbin/openclash-dns-guard.sh`（带时间戳）
- [ ] T1.2 `/etc/config/openclash-dns-guard` 增加 `alert_after/degrade_after/lock_ttl/notify_cooldown`
- [ ] T1.3 状态文件增加 `ALERT/FIRST_FAIL/INCIDENT_ID/LAST_NOTIFY`
- [ ] T1.4 触发条件从 `FAIL_CNT>=2`+开机 90s 改为“持续时长”判定
- [ ] T1.5 新增 ALERT 阶段（只通知，不动 DNS/路由）
- [ ] T1.6 降级阈值 180s；维护锁 `/var/run/openclash-maint.lock`（TTL 600s）时全程静默
- [ ] T1.7 `notify_once`：incident 去重 + 30min 冷却 + 发送失败重试 3 次
- [ ] T1.8 恢复用 `ORIG_SERVER/ORIG_NORESOLV/ORIG_RESOLVFILE` 回填，`dnsmasq reload` + 回读校验；失败保持降级
- [ ] T1.9 `repair_if_reverted` 日志节流
- [ ] T1.10 cf-ip 事务接锁（begin/end + trap）
- [ ] T1.11 `sh -n` + `/tmp` 副本干跑 V2–V7
- [ ] T1.12 输出 diff 与干跑日志，等用户点头再落设备

## Phase 2 — cf-ip 免重启（仓库侧 + 本地验证）

- [ ] T2.1 决策记录 D11（含 `PUT /configs?force=false` 证伪）
- [ ] T2.2 `cf-ip-auto-legacy` 增加 `--render-provider`
- [ ] T2.3 `openclash-transform.sh` provider 分支 + 回落
- [ ] T2.4 `common.sh` 增加 `cfip_openclash_apply_provider`
- [ ] T2.5 本地 docker mihomo 验证 provider 刷新、TUN 未重建
- [ ] T2.6 验证 OpenClash 07:00 订阅更新是否覆盖 provider 段（C4）
- [ ] T2.7 同步非 r11 副本 + 契约测试

## Phase 3 — 交付

- [ ] T3.1 汇总报告
- [ ] T3.2 遵守约束：不刷机、不推送、不合并，未批准不动设备
