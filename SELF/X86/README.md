# SELF/X86 — x86 虚拟机构建 Profile

本目录是 SWrt 面向 **x86 虚拟机** 的 SELF 构建差异层。上游 OpenWrt 可不断替换/更新，
而 SWrt 的差异尽量集中在 `SELF/` 中。

## 目标平台

- **第一优先级：Hyper-V**（VHDX + EFI + Synthetic NIC）
- **第二优先级：KVM/PVE**（virtio-net / virtio-blk-scsi / raw / qcow2）
- 不主动专项开发：VMware / VirtualBox / Xen / ESXi（若上游自带支持则保留）

## 目录约定

```text
SELF/X86/
├── config_self.seed          # x86/64 目标配置种子
├── 02_target_only_self.sh    # x86 目标专用构建步骤
├── common/files/             # 所有 x86 VM 通用文件（如网络首次初始化）
│   └── usr/bin/swrt-vm-perf  # 只读 VM 网络性能诊断工具（Hyper-V/KVM）
├── hyperv/                   # Hyper-V 专用（VMMQ/vRSS 说明，见 hyperv/README.md）
├── kvm/                      # KVM/PVE 基础适配（预留，raw/qcow2）
└── tests/
    └── vm-smoke-test.sh      # QEMU 启动 smoke test（EFI→init complete + ≥2 NIC）
```

> 分层是方向，不强求搬移所有旧文件。当前以最小改动为主，`hyperv/`、`kvm/`
> 目录为未来扩展预留。

## 默认安全策略

- **kernel ABI（.vermagic）**：由当前实际 kernel config/source 自动生成，绝不强制覆盖
  官方 release vermagic。所有 kmod 与 firmware 同一次构建产生。
- **CPU mitigations**：默认保持内核开启（生产构建不注入 `mitigations=off`）。
- **x86-64 指令集**：默认 `generic x86-64`，不启用 x86-64-v2/v3，保证 VM 迁移兼容。

## Firmware 格式

- Hyper-V 主产物：`combined-efi.vhdx`
- ext4-EFI 镜像：`ext4-combined-efi.img`
- generic rootfs：`generic-rootfs.tar.gz`
- 可选：`qcow2`（由 raw/ext4 通过 `qemu-img convert` 生成，供 PVE/KVM）

## WAN/LAN 初始化原则

- 由 `common/files/etc/uci-defaults/99-x86-vm-network` 在**首次启动**执行（uci-defaults
  运行后自删除，不会每次开机重复写入）。
- **不覆盖已有用户配置**：若 `/etc/config/network` 已存在可用的 lan/wan 则跳过。
- 支持通过 `/etc/network.env` 指定 `WAN_MAC` / `LAN_MAC` 做稳定 MAC 映射
  （示例见 `SELF/X86/network.env.example`，不随 firmware 分发）。
- MAC 比较统一转小写；`WAN_MAC`/`LAN_MAC` 不能解析为同一接口。
- 显式指定 MAC 但找不到时，不静默回退到其它接口。
- 未指定时回退到“第一块物理网卡 = WAN、第二块 = LAN”，仅对全新安装生效。

## 性能功能定位

| 功能 | 状态 | 说明 |
|------|------|------|
| BBR3 | 保留 | 影响 VM 自身 terminate 的 TCP，不强行增加额外 TCP 魔改 |
| SFE   | 保留能力 | 默认不写死开启，可由 LuCI 手动控制 |
| LRNG  | 保留（可选增强） | 视为 optional enhancement，非 x86 VM 核心优化 |
| nftables flow offload | 保留 | 由 firewall4/LuCI 控制 |
| x86-64-v2/v3 | 默认关闭 | 保持 generic，保证跨宿主机迁移 |
| swrt-vm-perf | 只读诊断 | `usr/bin/swrt-vm-perf`，观察 RSS/queue/offload/IRQ/softnet，绝不自动修改 |
| hv_netvsc ring | 默认 1024/1024 | 仅对 `hv_netvsc` 生效，供 Hyper-V; 其它驱动保持默认; 不超出 driver maximum |
| ip_local_port_range | 默认 10240-65535 | `sysctl.d/90-swrt-x86-network.conf`，利于本机主动连接（OpenClash/DDNS/下载器） |
| Packet Steering | auto | 复用 OpenWrt 自带 packet_steering; Hyper-V single-RX-queue + 多 vCPU 时自动启用，multiqueue/vRSS 时不叠加 RPS |

## VM 网络性能定位

- **默认只读**：`swrt-vm-perf` 只观测并报告，不修改任何参数。它枚举真实 Ethernet NIC，
  输出 Hyper-V/KVM 检测、queue/RSS、offload、IRQ/softirq 分布、RPS/XPS mask、
  softnet 拥塞、ring/coalescing、当前 forwarding fast path 与 conntrack 信息。
- **Packet Steering auto**：`/etc/config/swrt-vm-performance` 的 `packet_steering` 默认
  `auto`。仅当 Hyper-V + vCPU>1 + hv_netvsc single-RX-queue 时，复用 OpenWrt 自带的
  `packet_steering`（RPS）; 若 hv_netvsc 已多队列（vRSS/VMMQ 生效）则不叠加 RPS。
  用户可显式 `on` / `off` 覆盖。
- **不默认开启**：irqbalance、超大 backlog、64MB socket buffer、busy_poll、
  tx_queue_len=10000、全局 FQ、强制 BBR3、强制 SFO/SFE 均保持测试驱动，只有在
  `swrt-vm-perf` 检测到真实瓶颈（例如单 CPU NET_RX 饱和、softnet drops）且宿主侧
  VMMQ/vRSS 已排查后才考虑 A/B。
- **Hyper-V 优先处理宿主侧**：Host NIC RSS → vSwitch VMMQ → VM vRSS → hv_netvsc
  multi-queue → OpenWrt。宿主侧配好 vRSS/VMMQ 后，guest 内的 Packet Steering auto
  会自动不叠加 RPS。具体检查命令见 [hyperv/README.md](hyperv/README.md)。
- **fast path 互斥**：Baseline / Software Flow Offload / Hardware Flow Offload /
  Shortcut-FE 由 LuCI 互斥选择，不强制叠加。性能对比建议按
  `None → Software Flow Offload → Shortcut-FE` 逐一实测，不以理论宣称快慢。

## Source Lock

见 `SELF/sources.lock`。normal/nightly 可自动跟随上游；release 构建使用已验证的
commit/ref lock 以保证可复现。

## 构建验证

- `bash -n`：静态检查 SELF 脚本语法。
- `make defconfig`：验证 `config_self.seed` 可正常展开。
- QEMU smoke test：`SELF/X86/tests/vm-smoke-test.sh`，验证 EFI 启动 → init complete，
  并强制要求 boot log 中识别到至少 2 块网卡。
- overlay 校验：`02_target_only_self.sh` 合并 `PATCH/files` + `SELF/X86/common/files`
  后，确认最终层级为 `files/etc/...`（非 `files/files/...`），且关键文件存在。
- x86 kernel 校验：`02_target_only_self.sh` 直接读取 `target/linux/x86/Makefile` 的
  `KERNEL_PATCHVER`，不再依赖 rockchip target。