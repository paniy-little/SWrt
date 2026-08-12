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
├── hyperv/                   # Hyper-V 专用（预留，VHDX 等）
├── kvm/                      # KVM/PVE 基础适配（预留，raw/qcow2）
└── tests/
    └── vm-smoke-test.sh      # QEMU 启动 smoke test
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
- 支持通过 `/etc/network.env` 指定 `WAN_MAC` / `LAN_MAC` 做稳定 MAC 映射。
- 未指定时回退到“第一块物理网卡 = WAN、第二块 = LAN”，仅对全新安装生效。

## 性能功能定位

| 功能 | 状态 | 说明 |
|------|------|------|
| BBR3 | 保留 | 影响 VM 自身 terminate 的 TCP，不强行增加额外 TCP 魔改 |
| SFE   | 保留能力 | 默认不写死开启，可由 LuCI 手动控制 |
| LRNG  | 保留（可选增强） | 视为 optional enhancement，非 x86 VM 核心优化 |
| nftables flow offload | 保留 | 由 firewall4/LuCI 控制 |
| x86-64-v2/v3 | 默认关闭 | 保持 generic，保证跨宿主机迁移 |

## Source Lock

见 `SELF/sources.lock`。normal/nightly 可自动跟随上游；release 构建使用已验证的
commit/ref lock 以保证可复现。

## 构建验证

- `bash -n`：静态检查 SELF 脚本语法。
- `make defconfig`：验证 `config_self.seed` 可正常展开。
- QEMU smoke test：`SELF/X86/tests/vm-smoke-test.sh`，验证 EFI 启动 → init complete。