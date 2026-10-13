# Shortcut-FE v2 (SFE v2) 通用移植指南

本目录包含高通 Shortcut-FE v2（基于 QSDK 14.0，`NHSS.QSDK.14.0.r1-00011-O`）针对通用平台（x86/ARM/MIPS）与标准 Linux 内核（5.15 ~ 6.12）的移植包与相关文档。

不依赖 NSS、PPE 或任何高通专用硬件芯片，在原生内核上即可作为外部模块运行。

内核增强（`netif_receive` 入口钩子、PPPoE 下行卸载、多注册者 conntrack 事件）由 `PATCH/kernel/sfe/952`、`953` 提供，勾选本包即生效，不需要改内核配置：

* `952` 定义 `CONFIG_NF_CONNTRACK_CHAIN_EVENTS`，`953` 定义 `CONFIG_SHORTCUT_FE`（入口钩子、skb 快转标记位、`br_dev_update_stats`）。两份必须成对，`953` 对 `nf_conntrack_ecache.c` 的改动依赖 `952` 先落地。
* 包的 `KCONFIG` 以 `符号=值` 形式列出这两个符号。构建时 `Kernel/Configure` 通过 `package-metadata.pl kconfig` 把已选包的这些条目写入 `.config.override`，再由 `kconfig.pl` 并入内核 `.config`，所以选中 `kmod-shortcut-fe-cm`（自动带上 `kmod-shortcut-fe`）就够了。
* `符号=值` 表示"请打开"，裸符号 `CONFIG_FOO` 表示"检查已打开"（`include/kernel.mk` 的门禁），不要混用。
* 补丁会改动 `struct sk_buff`，开关需要内核与全部模块一起重编。
* SDK 构建例外：内核预编译、`.config` 冻结，`Kernel/Configure` 不重跑，用 SDK 编出来的模块走的是原生内核路径。

运行期二选一：有 `athrs_fast_nat_recv` 就走 `netif_receive` 钩子，没有则回退 Netfilter，`dmesg | grep "sfe:"` 可以看到当前走哪条。

---

## 1. 目录结构

`@text
PATCH/sfe-v2/
├── package/             # OpenWrt 源码包（提供 kmod-shortcut-fe 与 kmod-shortcut-fe-cm）
│   ├── Makefile         # 编译规则，集成防火墙配置与 ucitrack 联动
│   ├── patches/         # 单一兼容补丁（001-openwrt-generic-compat.patch）
│   ├── src/             # 连接管理器、兼容层与桩头文件
│   │   ├── sfe_cm.c     # 控制面（conntrack 同步、时钟嘀嗒续期、规则下发）
│   │   ├── sfe_cm.h
│   │   ├── sfe_compat.h # 通用内核兼容层与模块内联 dev_fast_xmit
│   │   ├── asm/unaligned.h
│   │   └── linux/       # debug_mem_usage.h / if_trustsec.h
│   └── files/           # 运行时服务与工具
│       ├── etc/init.d/shortcut-fe             # 防火墙按需启停脚本
│       ├── usr/bin/sfe_dump                  # 状态与统计导出工具
│       └── usr/share/ucitrack/shortcut-fe.json # LuCI 防火墙热重载配置
└── README.md            # 本文件
`@

---

## 2. 补丁与设计精简说明

原版 QSDK SFE 针对高通私有内核及 NSS 硬件设计，移植到通用内核只需解决通用兼容与控制面挂接。

### 2.1 单一补丁（`001-openwrt-generic-compat.patch`）
* **Makefile**：
  * 去除 `-Werror`，去除依赖私有内核导出的 `sfe_pppoe_mgr.o`；
  * 添加 `shortcut-fe-cm.o` 一同编译；
  * 编译参数加入 `-include $(src)/sfe_compat.h`。通过预包含头文件与 `-I$(src)`，Linux 6.11+ 的 `asm/unaligned.h` 移除及内核函数签名变更全部在垫片中处理，上游业务代码零修改。
* **双模 Ingress 接收钩子**：
  * 在 `sfe.c` 初始化时动态探测 `athrs_fast_nat_recv`。若内核有相关补丁则挂接函数指针获得极致性能；若为通用内核，则自动回退至 Netfilter `NF_INET_PRE_ROUTING`（优先级 `NF_IP_PRI_FIRST` / `NF_IP6_PRI_FIRST`），做到全平台通用且开箱即用，卸载时用 `__symbol_put` 归还符号引用。
* **数据面（sfe_ipv4_*.c / sfe_ipv6_*.c）**：
  * 彻底剔除访问 PPE 硬件虚表（`xmit_dev->offload_ops`）的私有代码，纯软直发。
* **导出按设备清理接口**：
  * `sfe_ipv4_destroy_all_rules_for_dev()` / `sfe_ipv6_destroy_all_rules_for_dev()` 由引擎内部函数改为导出，供 `shortcut-fe-cm.ko` 调用；
  * 匹配范围从 `original_dev`/`reply_dev` 扩到两个方向的 `top_interface_dev`，否则 PPPoE 与 bridge 场景在设备下线时清不掉规则。
* **PPPoE 的 top/bottom 设备**：
  * `dev_fill_forward_path()` 的 `DEV_PATH_PPPOE` 那一跳给的是 `pppoe-wan`，物理出口是紧随其后的 `DEV_PATH_ETHERNET` 跳（`eth0`/`eth0.x`）。连接管理器取物理设备作接口、`pppoe-wan` 记作 top，并置 `SFE_RULE_CREATE_FLAG_USE_FLOW/RETURN_BOTTOM_INTERFACE`；把 `pppoe-wan` 当接口会让 ppp 再封装一次。

### 2.2 模块内联 `dev_fast_xmit`（免内核补丁直通网卡）
`create-rule` 对两个方向都置 `SFE_RULE_CREATE_FLAG_FLOW/RETURN_TRANSMIT_FAST`，实现放在 `sfe_compat.h`：
* 直接锁网卡 TX 队列并调用 `netdev_start_xmit()`，绕过 Linux qdisc 调度层；
* TX 队列繁忙或停止时返回 `false`，回退 `dev_queue_xmit()`；
* 引擎在建流时校验一次可行性（GSO/VLAN/XFRM/socket validate），不通过则该方向不再走快路径；垫片在每包上补做 `validate_xmit_skb()` 会处理的那几项判断，避免流内状态变化被漏过。

代价：加速流绕过 qdisc，SQM/cake 整形、`tc` egress 过滤与 qdisc 统计对它们不生效，未加速的流不受影响。

### 2.3 连接同步与 conntrack 续期
快速路径不经过 conntrack，计数与超时需要连接管理器定期补。引擎不带周期性遍历，批量同步由 CM 主动发起（`SFE_TX_CONN_STATS_SYNC_MANY_MSG`），`sfe_cm.c` 里每个协议族各有一个 delayed work：
* 每 1000 ms 起一轮，兼作丢响应的兜底；
* 引擎回报 `many->next != 0` 时，1 jiffy 后续跑下一批，批间让出一次调度；
* 同步时把 `ct->timeout` 续期并累计 `nf_conntrack_acct`，加速流不因 conntrack GC 掉出快路径，流量统计也才有数；
* `inc_ticks` 是毫秒，`ct->timeout` 是 jiffies，换算用 `msecs_to_jiffies()`。

引擎每批约 36 条，最大连接数 4096，一轮全表约 1~2 秒，后台开销可忽略。

### 2.4 设备失效清理
`NETDEV_DOWN`、`NETDEV_UNREGISTER`、`NETDEV_CHANGEADDR`、`NETDEV_FEAT_CHANGE` 以及地址删除（inet/inet6 notifier 的 `NETDEV_DOWN`）都会按设备清理规则。规则里缓存了设备、MAC、PPPoE 会话号和建流时的网卡特性，设备状态一变这些信息就不能再用。

### 2.5 conntrack 事件
* 内核带 `952`（`CONFIG_NF_CONNTRACK_CHAIN_EVENTS`）时注册到 notifier chain，多注册者互不干扰。
* 原生内核只有单槽：`sfe_cm.c` 接管槽位，并把事件转发给原持有者（通常是 ctnetlink），每轮同步 reclaim 一次，卸载时归还。`exp_event` 要一起接管，内核调用它时不判空。

### 2.6 语义边界
* 加速流不再经过 netfilter：建连期生效的规则（SYN、MSS clamp、ALG、NAT 判定）照常，按包生效的规则（计数器、`limit`、`log`、改 mark/dscp、每包 redirect）不生效。
* egress qdisc 对加速流同样不生效（见 2.2）。
* CM 不记录"是否已加速"，未被快路径吃掉的包会再请求一次规则；引擎在建规则入口先查重，命中只更新 TCP 窗口状态并返回 `SFE_RET_CONN_ALREADY_EXISTS`。
* 只处理 `init_net` 与默认 conntrack zone；规则表上限 4096/族，超限后退回慢路径。

---

## 3. PPPoE 加速

ppp netdev 是单队列且 `tx_queue_len = 3`，上行所有包排在 `ppp_xmit_lock()`（`spin_lock_bh(&ppp->wlock)`）后面，下行是 `pppoe_rcv → ppp_input` 一条 upcall。SFE 让加速流不再进 ppp。

### 数据面
* 上行（Encap）：LAN 进来的 IP 帧由 SFE 压入 PPPoE + PPP 头（`sfe_pppoe_add_header()`，纯 `skb_push`）并填 BRAS MAC，直接从物理网卡发出。
* 下行（Decap）：收到 `ETH_P_PPP_SES`(0x8864) 原始帧时校验 session id 与对端 MAC，剥掉 8 字节 PPPoE/PPP 头，按普通 IP 帧发往 LAN。

### 与钩子位置的关系
* 开了 `CONFIG_SHORTCUT_FE`（953 的 `netif_receive` 钩子）：上下行都能加速。
* 原生内核（netfilter `PRE_ROUTING` 钩子）：钩子看到的已是 `pppoe-wan` 上的裸 IP 包，只有上行能被接管，下行回到内核慢路径（不影响通断）。

| 方向 | 原生内核 | `CONFIG_SHORTCUT_FE=y` |
| --- | --- | --- |
| 上行 LAN→WAN | 直发物理网卡，绕开 ppp | 同左 |
| 下行 WAN→LAN | 走内核慢路径 | 自己剥头进 LAN |

### 控制面
QCA 原版用的 `pppoe_channel_addressing_get` 是私有导出符号，标准内核没有，这里用 5.13+ 的 `dev_fill_forward_path()`：
1. 入/出设备是 `ARPHRD_PPP`（`pppoe-wan`）时 `dst_neigh_lookup()` 没有 ARP 邻居，走虚拟设备分支；
2. `dev_fill_forward_path(dst->dev, NULL, &stack)` 回溯出 `DEV_PATH_PPPOE`（Session ID + BRAS MAC）与其后的 `DEV_PATH_ETHERNET`（物理出口）；
3. 接口设备取最后一跳、`pppoe-wan` 作 top 并置 `USE_FLOW/RETURN_BOTTOM_INTERFACE`；
4. 回溯失败则放弃加速，交回内核慢路径。

---

## 4. 运行时使用与排查

### 防火墙与 LuCI 集成
* **LuCI 界面**：“网络” -> “防火墙” -> “常规设置” 中，分流模式选择 `Shortcut-FE v2 flow offloading` 并保存应用。
* **按需加载与热重载**：模块取消了开机自动盲载。通过 `ucitrack` 联动，当防火墙配置启用 `shortcut_fe` 时自动 `modprobe` 对应模块，禁用时自动 `rmmod` 卸载。

### 命令行排查
`@bash
# 手动控制服务
/etc/init.d/shortcut-fe start
/etc/init.d/shortcut-fe stop

# 查看运行状态与连接统计
/usr/bin/sfe_dump
/usr/bin/sfe_dump ipv4
/usr/bin/sfe_dump ipv6

# 查看模块加载
lsmod | grep shortcut
`@
