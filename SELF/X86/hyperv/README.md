# SWrt x86 VM on Hyper-V — VMMQ / vRSS 性能检查

> 本文面向在 **Windows Server / Hyper-V** 上运行 SWrt x86 VM 的维护者。
> 目标是让 guest 真正看到多队列 / RSS 效果，而不是在 guest 里用 RPS 去掩盖宿主侧
> 没配置好的问题。所有命令默认**只读**。

## 性能处理链路（从宿主到 guest）

```text
Host NIC RSS
       ↓
Hyper-V vSwitch VMMQ
       ↓
VM vRSS
       ↓
Linux hv_netvsc multi-queue
       ↓
OpenWrt networking
```

> 原则：**先在宿主侧把 RSS / VMMQ / vRSS 配好**，再回到 guest 用 `swrt-vm-perf`
> 验证。不要在 guest 里默认开启 RPS 去掩盖宿主侧的缺失。

## 1. 查看 VM 网络适配器的 vRSS / VMMQ 配置

在宿主机 PowerShell（管理员）：

```powershell
Get-VMNetworkAdapter -VMName "<VMName>" |
    Format-List Name, VrssEnabled, VmmqEnabled, VmmqQueuePairs, VmqWeight
```

## 2. 查看宿主 NIC 的 RSS

```powershell
Get-NetAdapterRss
```

确认宿主物理网卡已启用 RSS，且队列 / 处理器足够。

## 3. 确认网卡与 VM 支持后，开启 vRSS / VMMQ

> 仅在网卡与宿主配置支持时执行；不自动猜测 queue pair 数，不强制 SR-IOV。

```powershell
Set-VMNetworkAdapter -VMName "<VMName>" -VrssEnabled $true -VmmqEnabled $true
```

开启后回到 guest 验证是否出现多队列：

```bash
swrt-vm-perf
```

关注输出中的：

- `driver: hv_netvsc`
- `RX queue count` / `TX queue count`（期望 > 1）
- `ethtool -l` / `ethtool -x`（combined / RSS 生效）
- `[OK] multiple RX queues detected` / `[OK] network work is visible on multiple CPUs`

## 4. 注意事项

- **不牺牲 VM 迁移兼容性**：VMMQ 是否真正有效仍取决于 host NIC / vSwitch / host 配置，
  本轮不强制固定 queue pair 数，也不自动配置 SR-IOV。
- **不要用 guest RPS 掩盖宿主问题**：只有当 `swrt-vm-perf` 显示单 RX queue + 单 CPU
  NET_RX 饱和、且宿主侧 VMMQ / vRSS 已排查无问题后，才值得做 RPS A/B。
- **只报告，不自动修改**：上位机辅助脚本 `hyperv-perf-check.ps1` 默认只读，只有显式
  传入 `-EnableRecommended` 才会执行 `Set-VMNetworkAdapter` 开启 vRSS/VMMQ。

## 官方依据

- https://learn.microsoft.com/en-us/windows-server/administration/performance-tuning/role/hyper-v-server/linux-virtual-machine-considerations
- https://learn.microsoft.com/en-us/powershell/module/hyper-v/set-vmnetworkadapter

## 附：guest 内快速一页式诊断

```bash
swrt-vm-perf
```

该工具只读，输出 VM/CPU 基础信息、Hyper-V 检测、NIC 枚举、queue/RSS、offload、
IRQ/softirq 分布、RPS/XPS mask、softnet 拥塞、ring/coalescing、forwarding fast path
与 conntrack 状态，帮助判断瓶颈发生在宿主侧还是 guest 侧。