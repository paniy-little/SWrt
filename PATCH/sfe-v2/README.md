# Shortcut-FE v2 (SFE v2) 通用移植指南

本目录包含高通 Shortcut-FE v2（基于 QSDK 14.0，commit: `56ba014`）针对通用平台（x86/ARM/MIPS）与标准 Linux 内核（5.15 ~ 6.12）的移植包与相关文档。

不依赖 NSS、PPE 或任何高通专用硬件芯片，完全在 CPU 上通过软件快速路径转发。

---

## 1. 目录结构

```text
PATCH/sfe-v2/
├── package/             # OpenWrt 源码包（可直接拷入 package/ 或 feeds）
│   ├── Makefile         # 编译规则，生成并通过 AUTOLOAD 自动加载 kmod-shortcut-fe 与 kmod-shortcut-fe-cm
│   ├── patches/         # 单一最简兼容补丁（001-openwrt-generic-compat.patch）
│   ├── src/             # 连接管理器与兼容层源码
│   │   ├── sfe_cm.c     # 简化版控制面（负责 conntrack 同步与规则下发）
│   │   ├── sfe_cm.h
│   │   └── sfe_compat.h # 内核跨版本（5.15 - 6.12）通用兼容垫片
│   └── files/           # 运行时辅助服务
│       └── etc/init.d/shortcut-fe # 极简启停与 conntrack 参数配置脚本
├── kernel-patches/      # 内核补丁（可选性能增强）
│   └── 6.12/
│       ├── 999-optional-dev-fast-xmit-optimization.patch
│       └── README.md
└── README.md            # 本文件
```

---

## 2. 补丁精简说明（为什么只有 1 个 Patch）

原版 QSDK SFE 针对高通私有内核设计，移植到通用内核只需解决三件事：
1. 关掉 `-Werror` 避免不同编译器阻断构建；
2. 剔除高通专有硬件（PPE）和私有内核驱动（`sfe_pppoe_mgr`）；
3. 注入跨内核版本的 API 垫片。

因此我们将补丁收拢为单个 `001-openwrt-generic-compat.patch`（198 行）：

* **Makefile**：
  * 去除 `-Werror`；
  * 去除依赖私有内核导出的 `sfe_pppoe_mgr.o`；
  * 添加 `shortcut-fe-cm.o` 一同编译；
  * 编译参数加入 `-include $(src)/sfe_compat.h`。通过预包含头文件，所有函数签名变更（如 Linux 6.12 的 `ip_route_output` 新增 scope 参数）全部在垫片中重写，上游业务代码零修改。
* **sfe.c**：
  * 原版 `extern bool fast_tc_filter;` 依赖高通私有内核定义，补丁中改为本地变量 `bool fast_tc_filter = false;`。
* **数据面（sfe_ipv4_*.c / sfe_ipv6_*.c）**：
  * 原生代码直接访问了 `xmit_dev->offload_ops`（PPE 芯片的硬件加速虚表），标准内核的 `struct net_device` 根本没有此成员，补丁用 `#ifdef CONFIG_NF_FLOW_COOKIE` 隔离开。

---

## 3. PPPoE 加速机制与实现

### 数据面
SFE v2 数据面（`sfe_pppoe.c`、`sfe_ipv4_tcp.c` 等）原生支持 PPPoE 解包（Decap）与封包（Encap）：
* **下行方向（Decap）**：WAN 网卡收到 `ETH_P_PPP_SES`（0x8864）报文时，SFE 匹配 session id 后直接剥除 PPPoE/PPP 报头（8 字节），恢复为标准 IP 帧发往 LAN。
* **上行方向（Encap）**：LAN 发往外网的数据帧，SFE 直接压入 PPPoE 头并填入 Session ID，外层填上以太网 MAC，直接调用物理网卡接口发出。绕过了 Linux 内核 PPP 驱动的单线程队列与 channel 互斥锁。

### 控制面（如何识别 PPPoE 接口）
原版 QCA ECM 使用了高通给 PPP 驱动打的私有导出函数 `pppoe_channel_addressing_get`。在标准 Linux 内核中该函数不存在。

本方案在 `sfe_cm.c` 中通过 Linux 5.13+ 内核原生公开的 `dev_fill_forward_path()` 实现：
1. 当连接的入/出设备为 `ARPHRD_PPP`（即 `pppoe-wan`）时，`dst_neigh_lookup` 无 ARP 邻居，走虚拟设备解析分支；
2. 调用 `dev_fill_forward_path(dst->dev, NULL, &stack)`，内核会自动回溯出底层真实物理网卡（如 `eth0`）、Session ID 以及对端 BRAS 服务器的 MAC 地址；
3. 将上述参数下发给 SFE 规则表，打上 `PPPOE_ENCAP` / `PPPOE_DECAP` 标记，建立双向硬件无关的纯软加速流；
4. 若回溯失败（如非常规隧道），连接管理器放弃加速，交由内核慢速路径正常路由，不影响网络通断。

---

## 4. 运行时使用与排查

### 加载与启用
```bash
# 启动服务
/etc/init.d/shortcut-fe start

# 查看内核模块是否加载
lsmod | grep shortcut_fe
# 应看到 qca_nss_sfe 和 shortcut_fe_cm
```

### 调试与统计
```bash
# 查看快速转发引擎统计（发包数、丢包数、PPPoE 封装/解封装数等）
cat /sys/sfe/debug_dev/ipv4_stats
cat /sys/sfe/debug_dev/ipv6_stats

# 查看连接管理器异常计数（若有连接未被加速可在此排查）
cat /sys/sfe_cm/exceptions
```

---

## 5. 内核补丁说明

* **默认无需打内核补丁**：
  在标准 Linux 6.12 内核上，所有必要依赖（路由、conntrack、`dev_fill_forward_path`）均已具备，SFE v2 可直接作为外部内核模块编译加载。
* **可选补丁（`kernel-patches/6.12/`）**：
  提供了一个可选的 `dev_fast_xmit` 补丁。作用是在发包时尝试获取网卡硬件发送锁并直接调用驱动的 `ndo_start_xmit`，跳过 Linux qdisc 队列调度。如果不打该补丁，`sfe_compat.h` 会自动回退调用标准的 `dev_queue_xmit`，完全不影响功能与稳定性。
