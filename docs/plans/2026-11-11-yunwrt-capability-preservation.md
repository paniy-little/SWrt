# YunWrt 功能保全审计（2026-11-11）

- 状态：待评审（未执行）
- 数据基线：设备实测清单 `openwrt-inventory-2026-11-11.json`（2026-11-11，494 KB）
- 关联文档：[2026-11-11-yunwrt-migration.md](./2026-11-11-yunwrt-migration.md)
- 约束：不建仓、不推送、不动现有 SWrt 仓库；只在 `/tmp` 只读快照与公开索引上核实。

---

## 0. 结论摘要

1. **设备实际在跑的功能，除 SFE / fast-classifier / fullconenat 外都能保住**。SFE 来自被内核补丁改过的自编译内核，ImageBuilder 无法复现；**2026-11-11 定案：SFE 与 fast-classifier 不保留，由 FlowCone + `kmod-nft-offload` 替代，这是交付决定，不是待验证假设**（4.3）。不做性能验证，不保留源码构建选项。
2. **`luci-app-wireguard` 不在设备上**（seed 里有、设备没装），不需要保全，之前的担心作废。
3. **`banip` 也不在设备上**（seed 里有、设备没装）。之前把它写成"在跑"是错的。
4. **107 个"来源未知"的包，全部可归到官方 core / packages / luci 的旧版本**，不是来路不明；真正需要单独追的只有 **29 个第三方源包**，其中 15 个是旧树自建或依赖。
5. 优化后首版 **14.5 人日**（关键路径 13.0 天，P3b 与 P3 并行）；最小可用版 **9.1 人日**；另有 0.5 人日非阻塞验收放到首版后。明细见 9.2。

---

## 1. 数据与判定口径

### 1.1 三方来源

| 来源 | 内容 | 用途 |
|---|---|---|
| ① 设备实测清单 | 481 个已装包、46 个服务、85 个 socket（43 端口）、210 个内核模块、26 个 UCI 配置 | 判定"实际是什么" |
| ② `SELF/X86/config_self.seed` | 371 行、125 个 `CONFIG_PACKAGE_*=y`、42 行 `is not set`（包级 20）、sha256 `5f6e14f6…`；`OpenWrt-Matrix.yml:230` 唯一生成 `.config` 的来源（钉死过程见 1.4） | 判定"当初想要什么" |
| ③ `SEED/X86/config.seed`（230 行、149 个 `=y`，**origin HEAD 已删除**）、`profiles/packages.{common,x86_64}`、OpenWrt-Add `apps/` | 历史默认（x86 构建从未读取，见 1.4）、新架构与新版可得性 | 判定"新版能不能给" |

### 1.2 判定标签

| 标签 | 含义 | 处理 |
|---|---|---|
| **设备实测** | 清单里有包、有配置、有进程/端口 | 以实测为准 |
| **seed 意图** | seed 里有，设备上没有 | 先查原因，再决定是否恢复 |
| **两者一致** | seed 有、设备也有 | 直接进入保全表 |
| **冲突（以实测为准）** | seed 说要有、设备没装或没跑 | 不当作"在跑的能力"，单列 |

### 1.3 本轮修正的旧结论

| 旧结论 | 实测事实 | 影响 |
|---|---|---|
| banip 在运行 | **未安装**（无包、无 `/etc/config/banip`、无进程） | 不列入运行能力；恢复与否需用户确认 |
| `luci-app-wireguard` 在用 | **未安装**，设备用的是 `luci-proto-wireguard` | 删除该项保全工作量 0.3 人日 |
| mosdns 监听 53/5335 | 实测 `127.0.0.1:953`，dnsmasq 的 `server=127.0.0.1#653`（OpenClash） | 验收断言改为 953 |
| SFE 只是"官方没有" | 设备内核被 `952/953` 两个补丁改过，带 4 个非官方 kmod，`DISTRIB_TAINTS=no-all override` | 从"换掉"升级为架构级取舍 |
| `upnp_nat_relay` 在跑 | 服务 **enabled 但未运行**；`miniupnpd` 根本没装 | 不算运行能力；当前是坏的 |
| `nikki` 不可用 | OpenWrt-Add `apps/OpenWrt-mihomo` 已发布 `nikki`、`luci-app-nikki` | 备选方案成立 |
| 三个自研 app 都没发布 | `upnp-nat-relay` 已在自有 feed；`nginx-manager`、`cloudflare-ip` 未发布 | 工作量保留 1.0 |

### 1.4 基准钉死：x86 的包集合到底由哪份 seed 决定

#### 1.4.1 执行链与行号（origin/25.12 @ `ddc7422`）

入口 `.github/workflows/X86-OpenWrt_self.yml:38` → `uses: ./.github/workflows/OpenWrt-Matrix.yml`。该文件 blob 为 `67e6899`，在本地 HEAD `884ea30` 与 origin HEAD `ddc7422` 上**完全相同**。

| 顺序 | 位置 | 动作 | 对 `.config` 的影响 |
|---|---|---|---|
| 1 | `OpenWrt-Matrix.yml:120-145` | `seed=${{ matrix.name }}` → `X86`，写入 `GITHUB_ENV` | 只定义变量 |
| 2 | `OpenWrt-Matrix.yml:208-216` | 拷 `SCRIPTS/X86/.`、`SELF/X86/.` 到仓库根，执行 `01_get_ready_self.sh` | 无 |
| 3 | `OpenWrt-Matrix.yml:218-225` | 在 `openwrt/` 下拷 `SCRIPTS/.`、`SELF/.`，跑 `02_prepare_package_self.sh` / `02_target_only_self.sh` / `04_remove_upx.sh` | `02_prepare_package_self.sh:304` 执行 `rm -rf .config`，清掉旧配置；其余是 feed/内核片段 |
| 4 | **`OpenWrt-Matrix.yml:230`** | `cp -rf ../SELF/${seed}/config_self.seed .config` | **唯一生成 `.config` 的一行** |
| 5 | `OpenWrt-Matrix.yml:247-250` | `make defconfig` | 展开默认值、校验依赖；`# ... is not set` 阻止默认值重新打开 |
| 6 | `OpenWrt-Matrix.yml:283-287` | `cp .config final.config` 并上传 artifact | 固化结果 |

`SEED/` 在整条链上**从未被读取**：`git grep 'SEED/'` 在 HEAD 只命中 `SELF/README_self.md` 的迁移映射表；即使回到 `ea88a4d`（设备构建所处版本），该 workflow 也已是 `using SELF`（第 208 行注释、第 230 行路径）。`SEED/X86/config.seed` 是更早一代的默认配置，随后被 `SELF/X86/config_self.seed` 取代。

#### 1.4.2 文件实证

| 文件 | 存在范围 | 行数 | `CONFIG_PACKAGE_*=y` | `=m` | `is not set` 行（包级） | sha256 |
|---|---|---|---|---|---|---|
| `SELF/X86/config_self.seed` | 本地 HEAD `884ea30`、`ea88a4d`、origin HEAD `ddc7422` | 371 | **125** | 0 | 42（20） | `5f6e14f63afca6c7659363d6715e802b9fa660e1c33b08198aad6ebd59471567` |
| `SEED/X86/config.seed` | 仅 `ea88a4d` 及更早；origin HEAD 已删除 | 230 | 149 | 0 | 23（12） | `eef65b7dc30c1f5b2f06ebbc592ebd81e3aa1021d0906f86e41dbaf1d648691f` |

足以区分两者的特征包：

- **只有 SELF 有（57 个）**：`luci-app-openclash`、`luci-app-cloudflare-ip`、`luci-app-nginx-manager`、`luci-app-upnp-nat-relay`、`luci-app-banip`、`banip`、`luci-app-wireguard`、`kmod-shortcut-fe-cm`、`kmod-fast-classifier`、`kmod-ipt-nat6`、`nat6`。
- **只有 SEED 有（81 个）**：`dae`、`frpc`、`frps`、`autocore-x86`、`luci-app-dae`、一批 `kmod-crypto-*`。
- **两份都有：68 个**（`luci-app-mosdns`、`luci-app-homeproxy`、nginx/uwsgi 栈等）。

#### 1.4.3 判据：设备站在哪一边

| 判据 | `SELF/X86` | `SEED/X86` |
|---|---|---|
| 设备覆盖率（seed 的 `=y` 包在 481 个已装包中命中） | **119/125 = 95.2%** | 78/149 = 52.3% |
| seed 有、设备无 | **6 项**（`banip`×3、`luci-app-wireguard`、2 个非包符号） | 71 项（含 `dae`、`frpc/frps`、`autocore-x86`） |
| 特征包核对 | 设备有 OpenClash、cloudflare-ip、nginx-manager、upnp-nat-relay、`shortcut-fe-cm`、`fast-classifier`、`nat6` | 设备**没有** `dae`、`frpc/frps`、`autocore-x86` |

三条判据同向，且 `SELF/README_self.md` 自述 SELF 是"自用版"、SEED 是"上游默认"。

#### 1.4.4 版本一致性

- origin/25.12 tip：`ddc7422`（2026-11-11T13:28:21Z，`Sync files from source repository`）。
- 本地工作树 HEAD：`884ea30`，落后 `origin/25.12` **31 个提交**。
- 但影响基准的三样东西在 31 个提交里**没有变**：workflow blob `67e6899`、SELF seed blob `ecc5bd9`（sha256 `5f6e14f6…`）、本地/origin/`ea88a4d` 三处一致。因此对照不受落后 31 提交影响。
- 设备是 `25.12.5` / `6.12.94`，对应 `ea88a4d` 那一代完整旧树（该提交为 2026-10-27）。

#### 1.4.5 feed 与 `profiles/` 那一侧

| 路径 | 谁读它 | 是否在 x86 构建路径上 |
|---|---|---|
| `SCRIPTS/` | 活跃 workflow `OpenWrt-Matrix.yml:212-213,221` | 是（但 origin HEAD 已删除，见下） |
| `SELF/` | 同 workflow `:214-215,222,230` | **是，决定全部包集合** |
| `SEED/` | 无人读（仅 README 映射表提及） | 否 |
| `profiles/packages.common` + `profiles/packages.x86_64` | `scripts/build.sh:137`（YAOF ImageBuilder 入口） | **否**：没有任何活跃 workflow 调用 `scripts/build.sh`，它只出现在 `workflows-templates/OpenWrt-Matrix.yml:118-119`（模板，未启用） |

**已删除仍被引用（origin HEAD `ddc7422` 实测）**：

| 被引用路径 | 引用位置 | 现状 |
|---|---|---|
| `SCRIPTS/` | `OpenWrt-Matrix.yml:212,213,221` | 已被 `sync-files.yml` 的 `rsync --delete` 删除（YAOF 无此目录）→ `cp` 失败，构建中止 |
| `PATCH/` | `SELF/02_prepare_package_self.sh:51,53,55,73,77,102,106,111-112,115,119,124,138,178-180,202-203,206,226` | 同上已删除 → 构建中止 |
| `openwrt_ma/` | `02_prepare_package_self.sh` | 不是删除项，由 `01_get_ready_self.sh:139` 运行时克隆 |
| `SEED/` | 仅 README | 无害 |

也就是说，origin HEAD 的"新架构"只落地了一半：YAOF 的 ImageBuilder 入口（`files/ profiles/ scripts/`）已经同步进来，但**活跃 workflow 仍是旧源码构建**，且它依赖的 `SCRIPTS/`、`PATCH/` 已被同步任务删掉。设备那一版镜像来自删除发生前的 `ea88a4d`。

#### 1.4.6 直白结论

**是。三方对照的 seed 基准就是 `SELF/X86/config_self.seed`，不是 `SEED/X86/config.seed`。** 判据：① 它是 `OpenWrt-Matrix.yml:230` 唯一写入 `.config` 的文件；② 设备对它的覆盖率为 95.2%，对 `SEED` 只有 52.3%；③ 区分性特征包（OpenClash、cloudflare-ip、nginx-manager、upnp-nat-relay、`shortcut-fe-cm`、`fast-classifier`）全部落在 SELF 侧。原差额表无需重算，结论不变；仅需把 2.4 的"42 项 zap"精确为"42 行 `is not set`，其中包级 20 项"，并注意 origin HEAD 当前不可构建。

---

## 2. 三方差额表

> 基准已由 1.4 钉死为 `SELF/X86/config_self.seed`（sha256 `5f6e14f6…`）。下表据此逐项复核，结论未因基准复核而变化。

### 2.1 seed 有、设备无（6 项）

| 包 | 判定 | 原因（推断/实测） | YunWrt 是否恢复 |
|---|---|---|---|
| `banip` | seed 意图 | 推断：镜像构建早于 seed 更新，或装包失败。设备无任何 banip 痕迹 | **需用户确认**（§11 第 2 项，建议放弃）；要就加回（+0.3 人日） |
| `luci-app-banip` | seed 意图 | 同上 | 跟随 `banip` |
| `luci-i18n-banip-zh-cn` | seed 意图 | 同上 | 跟随 `banip` |
| `luci-app-wireguard` | seed 意图 | 上游已废弃，构建时很可能被丢弃；设备实际用 `luci-proto-wireguard` | **不恢复** |
| `dnsmasq_full_ipset` | 非包 | Kconfig 符号；设备用的是 `dnsmasq-full` 的 nftset | **放弃**（由 nftset 取代） |
| `libcurl` | 非包 | 虚拟包；设备实际是 `libcurl4` | **保留**（虚拟名，随 `libcurl4` 带入） |

### 2.2 设备有、seed 无（362 项，其中 15 项需要判断）

362 项里 347 项是官方 core / packages / luci 的依赖（libs、kmods、base-files 等），属于构建自动拉入，不需要逐个判断。真正需要判断的是 15 项：

| 包 | 版本 | 来源 | 判定 | 处置 |
|---|---|---|---|---|
| `mosdns` | 5.3.4-r6 | 旧树 `luci-app-mosdns/mosdns` | 依赖（seed 的 `luci-app-mosdns` 嵌套编译它） | 保全 |
| `v2dat` | 2022.12.15 | 同上 | 依赖 | 保全（随 `luci-app-mosdns`） |
| `kmod-shortcut-fe` | 6.12.94-r8 | 旧树 `shortcut-fe` | 依赖（seed 有 `-cm`） | 见第 4 节 |
| `kmod-nft-fullcone` | 6.12.94.2023.05.17 | 旧树 `lede_pkg/fullconenat-nft` | 依赖（firewall fullcone） | 见第 4 节 |
| `bandix` | 0.12.9-r1 | 旧树 `openwrt-bandix` | 依赖（seed 有 `luci-app-bandix`） | 保全 |
| `luci-i18n-{arpbind,aurora-config,bandix,ramfree}-zh-cn` | 26.223.x | 旧树 | 构建语言包自动拉入 | 保全 |
| `3proxy` | 0.9.5-r5 | 旧树 `imm_pkg/3proxy` | **疑似残留**：无 `/etc/config/3proxy`、服务 enabled 但未运行 | **放弃** |
| `performance-manager` | 1.0.4-r1 | 旧树 `openwrt-performance-manager`，当前 OpenWrt-Add 无此目录 | **来源不明**：GitHub 未搜到对应仓库 | **放弃**（除非用户要求） |
| `luci-app-performance-manager` | 1.0.4-r1 | 同上 | 同上 | 放弃 |
| `luci-i18n-performance-manager-zh-cn` | 0 | 同上 | 同上 | 放弃 |
| `luci-app-cloudflare-ip-rill` | 2.6.0-r12 | 自有源码的 rill 变体 | 可选：依赖 `rill-runtime-preview` | 只发布基础版，不保全 rill 变体 |
| `rill-runtime-preview` | 1.5.6-r6 | 旧树 | rill 变体的运行时 | 不保全 |

### 2.3 两边都有、状态不同

| 项 | seed | 设备实测 | 结论 |
|---|---|---|---|
| `openclash` | 有 | enabled + **running**（`clash` 进程，653/890-895/9090） | 运行中，保全 |
| `mosdns` | 有 | enabled + **running**（`127.0.0.1:953`） | 运行中，保全 |
| `nginx` | 有 | enabled + **running**（80/443/5686/9083） | 运行中，保全 |
| `uwsgi` | 有 | enabled + **running** | 运行中，保全 |
| `samba4` / `ddns` / `avahi` / `wsdd2` / `fail2ban` / `dropbear` / `cron` / `sysntpd` | 有 | running | 保全 |
| `cf_ip` | 有 | enabled、**无实例**；但 cron 里 `/usr/bin/cf-ip-auto --run` 每天 3 次 | 功能以 **CLI + cron** 形式在跑，服务本身没起 |
| `upnp_nat_relay` | 有 | enabled、**未运行**；`miniupnpd` 未装、`miniupnpc` 已装 | 当前坏；需用户确认是否要修 |
| `bandix` | 有 | enabled、**无实例** | 未使用，可不保全 |
| `arpbind` | 有 | enabled、**无实例**（`/etc/config/arpbind` 存在但无条目） | 未使用，可不保全 |
| `usbmuxd` | 有 | enabled、**未运行** | 未使用，可不保全 |
| `3proxy` | 无 | enabled、**未运行**、无配置 | 残留，放弃 |
| `nat6` | 有 | enabled（script 型） | 保全（FlowCone 自带 nat6） |

### 2.4 seed 里被 zap 的项（42 行，其中包级 20 项）

`# CONFIG_PACKAGE_xxx is not set` 代表"当初明确不要"。实测：SELF seed 共 42 行 `is not set`，其中 `CONFIG_PACKAGE_*` 20 项、`CONFIG_TARGET_*` 7 项、其余为内核/镜像选项。20 个被 zap 的包与设备实测**零冲突**（没有一个安装在设备上）：

| 被 zap 项 | 含义 | 设备上是否有 | 结论 |
|---|---|---|---|
| `firewall` / `dnsmasq` | 明确用 `firewall4` / `dnsmasq-full` 替代 | 设备是 `firewall4` + `dnsmasq-full` | 一致，继续用 |
| `iptables-legacy` / `ip6tables-legacy` / `arptables-legacy` / `ebtables-legacy` / `xtables-legacy` | 不要 iptables 家族 | 设备只有兼容库（`libiptext*`），无 legacy 工具 | 一致 |
| `miniupnpd-iptables` | 明确要 nftables 版 | 设备连 `miniupnpd-nftables` 都没装 | 说明 UPnP 后来整个没落地 |
| `kmod-crypto-*`（AES-NI/AVX 等 8 项） | 不要额外加密指令集优化 | 设备无 | 一致 |
| `urandom-seed` / `urngd` | 不要 | 设备无 | 一致 |
| `dnsmasq_full_broken_rtc` | 不要"坏 RTC"变体 | 设备无 | 一致 |
| `luci-app-diskman` | 不要磁盘管理 | 设备无 | 一致 |
| `CONFIG_TARGET_IMAGES_BIOS` | 不要 BIOS 启动镜像（只要 EFI） | 设备是 UEFI 启动 | 一致 |

**没有发现"zap 掉的包却出现在设备上"的冲突项**——说明设备基本忠于 seed，唯一例外是 `3proxy` / `performance-manager` / `rill-runtime-preview` 这三个 seed 里完全没有、却被装上的包，它们是后来手动加的实验件。

### 2.5 按 125 基准重述三方对照

基准 = `SELF/X86/config_self.seed` 的 125 个 `=y`。设备 481 个已装包命中 **119/125 = 95.2%**，未命中 6 项；反向"设备有、125 无"362 项，其中只有 15 项需要判断（§2.2）。

**6 项"seed 有、设备无"逐项定性（对应 §10.3 的结论）**：

| 项 | 性质 | 125 分类 | 首版是否补回 |
|---|---|---|---|
| `banip` | 包，seed 有、设备从未安装 | **需确认**（§11 第 2 项） | **不补**（建议放弃；要就 +0.3 人日） |
| `luci-app-banip` | 同上 | **需确认** | 跟随 `banip` |
| `luci-i18n-banip-zh-cn` | 同上 | **需确认** | 跟随 `banip` |
| `luci-app-wireguard` | 包，上游 luci 已无此 app | **放弃** | **不补**；WireGuard 由 `luci-proto-wireguard` 提供 |
| `dnsmasq_full_ipset` | Kconfig 符号，非包 | **放弃** | **不补**；25.12 由 `dnsmasq_full_nftset`（默认 y）取代 |
| `libcurl` | 虚拟包名 | **保留** | 不适用；设备实际是 `libcurl4 8.19.0` |

**结论**：125 基准下，**"要补回来"的项为 0**；4 项确认放弃/不适用，3 项 banip 按 §11 默认放弃（可推翻）。之前若按 149 的 `SEED/` 对照，会误把 `dae`、`frpc/frps`、`autocore-x86` 等 71 项当成"要补"，实际它们从未进入 x86 构建，设备也从未装。

反向的 362 项不构成"需要补进 seed"：347 项是官方 core/packages/luci 的自动依赖，15 项中 3 项（`3proxy`、`performance-manager`、`rill-runtime-preview`）判定为残留、不进首版，其余是既有包的依赖或语言包，随保留包自动带入。

---

## 3. 107 个"来源未知"包的处理

清单的 `source_classification=未知` 只表示"版本在当前三个源索引里找不到"，**不等于来路不明**。按 `build_origin` 归类：

| 来源 | 数量 | 说明 |
|---|---|---|
| 官方 `feeds/packages/*` | 45 | 版本已漂移（如 `bash 5.3-r4`、`curl 8.19.0-r2`） |
| 官方 `feeds/luci/*` | 27 | 版本已漂移（如 `luci 26.218.70234`） |
| 官方 core（`feeds/base/*`） | 35 | 内核/系统包，版本随 25.12.5 快照 |
| 旧树自建 / 第三方 | **0** | — |

**结论：107 个全部可解释，没有需要单独追的未知来源。** 真正要追的是 29 个"第三方源"包，已在 2.2 逐项处理。

剩下 3 个确实追不到来源的：`3proxy`、`performance-manager`、`rill-runtime-preview`，均建议放弃（见 2.2）。

---

## 4. SFE / fullconenat 专项（内核被污染）

### 4.1 事实

| 事实 | 证据 |
|---|---|
| 内核 taint | `DISTRIB_TAINTS=no-all override` |
| 4 个非官方 build origin 的 kmod | `kmod-shortcut-fe 6.12.94-r8`、`kmod-shortcut-fe-cm 6.12.94-r8`、`kmod-fast-classifier 6.12.94-r6`、`kmod-nft-fullcone 6.12.94.2023.05.17~07d93b62-r1` |
| 构建路径 | `feeds/base/new/shortcut-fe/*`、`feeds/base/new/lede_pkg/fullconenat-nft` |
| 设备确实在用 | `firewall.defaults`：`shortcut_fe=1`、`shortcut_fe_module=shortcut-fe-cm`、`fullcone=1`、`brcmfullcone=1` |
| 源码位置 | `coolsnowwolf/lede:package/qca/shortcut-fe/{shortcut-fe,fast-classifier}`（已核实存在） |
| 内核补丁 | `target/linux/generic/hack-6.12/952-add-net-conntrack-events-support-multiple-registrant.patch`、`953-net-patch-linux-kernel-to-support-shortcut-fe.patch`（旧仓库 `PATCH/kernel/sfe/` 同名） |
| fullcone 补丁 | `firewall4` 的 `001/002/007` 三个补丁 + `libnftnl` fullcone 表达式补丁（旧仓库 `PATCH/pkgs/firewall/`） |
| 当前 OpenWrt-Add | `invasive/fullconenat-nft` 存在但**不发布**；`shortcut-fe` **整目录已不存在** |

### 4.2 能不能重编同款 / 能不能带预编译 apk

先说清 4 个非官方 kmod 与 125 基准的关系：

| 包 | 在 125 seed 里 | 角色 |
|---|---|---|
| `kmod-fast-classifier 6.12.94-r6` | **是** | SFE 的快速分类器 |
| `kmod-shortcut-fe-cm 6.12.94-r8` | **是** | SFE 连接管理 |
| `kmod-shortcut-fe 6.12.94-r8` | 否（`-cm` 的依赖） | SFE 公共符号 |
| `kmod-nft-fullcone 6.12.94.2023.05.17~07d93b62-r1` | **否**（设备后加） | nft fullcone 表达式 |

把"能不能带"拆成四条路逐条判定，不停在"架构不允许"：

| 路径 | 结论 | 依据 |
|---|---|---|
| ① IB 直接装设备上现成的 `.apk` | **不能** | arch 同为 x86_64，但 `vermagic` 是 `6.12.94`，IB 当前为 `6.12.112`；且 IB 内核没打 `953`，`CONFIG_SHORTCUT_FE` 不存在 |
| ② 自建 feed 分发设备同款预编译 kmod | **不能** | 同上；`sfe_cm.ko` 引用的 `athrs_fast_nat_recv` 是 `953` 在 `net/core/dev.c` 里 `EXPORT_SYMBOL` 的，未打补丁的内核里没有该符号，`insmod` 直接失败 |
| ③ 自建 feed + SDK 对 IB 的内核版本重编 SFE | **不能** | `sfe_ipv4.c:1478/2051`、`sfe_ipv6.c:1517/2074` 写 `skb->fast_forwarded`，该成员由 `953` 加进 `struct sk_buff`；vanilla 6.12 头文件没有它，编译即失败（不是产物不兼容，是编不出来） |
| ④ 自建 feed + SDK 重编 fullconenat | **能，但不做** | `nf_nat_fullcone.c` 只用标准 `nf_conntrack` API，可对 vanilla 内核编译；但要真正生效还得重编 `nftables`、`libnftnl`、`firewall4` 三个用户态包带 fullcone 表达式。它不在 125 里，按基准不保全 |

结论一句话：**SFE 这两个 kmod 无论"装旧包"还是"自建 feed 预编译"都带不进去；fullconenat 技术上能带，但不在你要保全的 125 包里。** 以上只解释"为什么带不进来"，不是方案选择依据。

### 4.3 定案：SFE 与 fast-classifier 不保留，由 FlowCone 替代（交付决定）

`kmod-shortcut-fe-cm` 与 `kmod-fast-classifier` **不保留**，由 OpenWrt-Add `apps/FlowCone`（eBPF，RFC 4787 全锥形 NAT）+ 官方 `kmod-nft-offload` 替代。

**这是交付决定，不是待验证假设**：不做性能对比，不做阈值判定，也不保留"源码构建 SFE"作为选项。

### 4.4 功能验收断言（可判定，不含性能指标）

```bash
# 1. 包已装、内核 BTF 在
apk info -e flowcone && test -e /sys/kernel/btf/vmlinux
# 2. 服务/进程在跑
pgrep -f flowcone
# 3. nft 规则里 offload 实际生效
nft list ruleset | grep -Eq 'flowtable|offload'
# 4. 无报错
! dmesg | grep -iE 'flowcone|bpf' | grep -iE 'error|fail'
# 5. SFE 选项不应再出现
uci -q get firewall.@defaults[0].shortcut_fe || echo "SFE option gone (expected)"
```

判定：上述 5 条全部满足即通过；缺一条不发布。

---

## 5. 功能保全清单（按实测校正）

判定列：**实测** = 清单可证；**seed** = 仅 seed 意图；**一致** = 两者都有。

### 5.1 网络与转发

| 功能 | 设备证据 | 判定 | 保全方式 | 层 | 人日 | 验收 |
|---|---|---|---|---|---|---|
| 路由 / NAT / fw4 | `firewall4`、13 条 redirect（2 条启用：KMS 1688、RDP 62145）、`fullcone=1` | 实测 | 官方 `firewall4` + `kmod-nft-nat`；fullcone 用 FlowCone | 官方 feed | 0.3 | `fw4 check`；`nft list ruleset` |
| 转发加速 | `kmod-shortcut-fe-cm`、`shortcut_fe=1` | 实测 | **FlowCone 替代（定案）**：FlowCone + `kmod-nft-offload` | 替代 | 0.5 | 见 4.4 五条功能断言 |
| 全锥形 NAT | `kmod-nft-fullcone`、`fullcone=1` | 实测 | **FlowCone 替代（定案）**（RFC 4787） | 替代 | 0 | 见 4.4 五条功能断言 |
| WAN / PPPoE | `wan.proto=pppoe`、`pppoe-wan` | 实测 | `99-x86-vm-network` 重写 | overlay | 0.5 | `uci get network.wan.proto` = `pppoe` |
| 双 WAN / 上游路由 | `xiaomi_wan`（eth2，dhcp）、`xiaomi_lan` 区域 | 实测 | overlay 保留接口定义 | overlay | 0.2 | `ip -br a` 有 eth2 地址 |
| LAN | `192.168.2.1/24` @ `eth1`，`br-lan` | 实测 | uci-defaults | overlay | 0 | `uci get network.lan.ipaddr` |
| IPv6 NAT | `nat6` enabled、`kmod-ipt-nat6` | 实测 | FlowCone 自带 `nat6` | 替代 | 0 | `/etc/init.d/nat6 status` |
| WireGuard | `wg0` 监听 **8978/udp**、`10.8.0.1/24`、`kmod-wireguard`、`luci-proto-wireguard` | 实测 | 全官方 | 官方 feed | 0 | `wg show` |
| UPnP | `upnp_nat_relay` enabled 但**未运行**；`miniupnpd` 未装 | 冲突 | 不保全；如要，需装 `miniupnpd-nftables` + 补丁 | — | 0 / 0.5 | `/etc/init.d/upnp_nat_relay status` |
| BBR | `kmod-tcp-bbr`、YAOF 默认 `kmod-tcp-bbr3` | 实测 | 官方 | 官方 feed | 0 | `sysctl net.ipv4.tcp_congestion_control` |

### 5.2 代理与 DNS

| 功能 | 设备证据 | 判定 | 保全方式 | 层 | 人日 | 验收 |
|---|---|---|---|---|---|---|
| **OpenClash** | `luci-app-openclash 0.47.156`（旧树自编）；`clash` 进程；653/890-895/9090；2 条订阅；fake-ip；chnroute | 实测 | **自有 feed 从源码编译**（第 6 节） | 自建 feed | 1.5 | `pgrep -f clash`；`/etc/init.d/openclash status` |
| OpenClash 核心 | `/etc/openclash/core/`（非包安装，`linux-amd64-v1`） | 实测 | overlay 预置 + 版本/hash 记录 | overlay | 0.5 | `sha256sum` 与记录一致 |
| mosdns | `mosdns 5.3.4-r6`、`127.0.0.1:953`、API 9091、adblock=1 | 实测 | OpenWrt-Add `apps/` | OpenWrt-Add | 0 | `netstat -lnup \| grep :953` |
| mosdns 热插拔 | 旧仓库 overlay 脚本 | seed | 迁移 | overlay | 0.3 | `sh -n`；`ifup wan` 触发一次 |
| GeoIP / GeoSite 数据 | `v2ray-geoip`、`v2ray-geosite` 20260812 | 实测 | OpenWrt-Add `v2ray-geodata` | OpenWrt-Add | 0 | `apk info -e v2ray-geoip` |
| banip | **无** | seed 意图 | 需用户确认 | 官方 feed | 0.2 | `apk info -e banip` |
| flock | 被 OpenClash/mosdns 脚本使用 | 实测 | 官方 | 官方 feed | 0 | `command -v flock` |

### 5.3 Web 管理栈（实测端口）

| 功能 | 设备证据 | 判定 | 保全方式 | 层 | 人日 | 验收 |
|---|---|---|---|---|---|---|
| nginx（SSL） | `nginx-ssl 1.26.3-r4`，80/443/5686/9083 | 实测 | 官方 | 官方 feed | 0.3 | `ss -lntp \| grep :443` |
| uwsgi | `uwsgi` + `uwsgi-cgi-plugin`，LuCI 走 `/cgi-bin/luci` | 实测 | 官方 | 官方 feed | 0 | `pgrep -f uwsgi` |
| LuCI | `luci-base`、Aurora 主题 | 实测 | 官方 + OpenWrt-Add | 两者 | 0 | 菜单可访问 |
| `luci-app-nginx-manager` | 1.8.7-r1（源码 1.8.8） | 实测 | 自有 feed 发布 | 自有 feed | 0.5 | `apk info -e` |
| nginx 站点 / 反代 | `nginx_manager` 配置（含 Obsidian 反代） | 实测 | overlay 配置 | overlay | 0.2 | `nginx -t` |
| rpcd | `rpcd-mod-file/iwinfo/rpcsys/ucode` | 实测 | 官方 | 官方 feed | 0 | `ubus list` |

### 5.4 自研 app

| 功能 | 设备证据 | 判定 | 保全方式 | 人日 | 验收 |
|---|---|---|---|---|---|
| `luci-app-cloudflare-ip` | 2.6.0-r12；cron `cf-ip-auto --run` 3 次/天 | 实测 | 发布基础版（不发布 `-rill`） | 0.5 | `apk info -e`；`nft list set` |
| `luci-app-upnp-nat-relay` | 1.3.13-r1；自有 feed 已有 r4 | 实测 | 直接用 feed | 0 | `apk info -e` |
| `luci-app-nginx-manager` | 1.8.7-r1 | 实测 | 见 5.3 | 0.5 | 同上 |
| `luci-app-cloudflare-ip-rill` | 已装 | 实测 | 不保全（实验件） | 0 | — |
| `rill-runtime-preview` | 已装 | 实测 | 不保全 | 0 | — |

### 5.5 系统与运维

| 功能 | 设备证据 | 判定 | 保全方式 | 人日 | 验收 |
|---|---|---|---|---|---|
| samba4 共享 | 运行中，共享 `/yunshu` | 实测 | 官方 + overlay 配置 | 0.1 | `smbclient -L localhost` |
| ddns | 运行中，cloudflare/aliyun/dnspod 脚本 | 实测 | 官方 | 0 | `/etc/init.d/ddns status` |
| acme | `acme-acmesh`、cron 每日续期 | 实测 | 官方 | 0 | `acme.sh --info` |
| fail2ban | 运行中 | 实测 | 官方 | 0 | `fail2ban-client status` |
| avahi + wsdd2 | 运行中 | 实测 | 官方 | 0 | `pgrep avahi-daemon` |
| zram | `zram-swap`、`kmod-zram` | 实测 | 官方 + sysctl overlay | 0.1 | `swapon --show` |
| VM/Hyper-V 调优 | 旧仓库 init + sysctl | seed | overlay | 0.4 | 端口范围、`ethtool -G` |
| 网卡 ring buffer | 旧仓库 hotplug | seed | overlay | 0.2 | 非 `hv_netvsc` 生效 |
| arpbind / bandix / usbmuxd | enabled 但未运行/无实例 | 冲突 | 不保全（上游默认会带 `arpbind`/`bandix`，装了不影响） | 0 | — |
| 3proxy / performance-manager | 未运行 / 来源不明 | 残留 | 放弃 | 0 | — |
| aurora 主题 + config | 已装、Aurora 为默认 | 实测 | OpenWrt-Add `themes/` | 0 | `uci get luci.main.mediaurlbase` |
| 中文翻译增强 | `addition-trans-zh` | 实测 | YAOF 默认已有 | 0 | — |
| 运维工具 | bash/htop/jq/tcpdump/git/iperf3/ethtool/pciutils/usbutils… | 实测 | 官方 | 0 | `apk info -e` |

---

## 6. OpenClash 三条路径（按设备实测更新）

### 6.1 事实

| 事实 | 证据 |
|---|---|
| 设备版本 0.47.156，**旧树自编** | `build_origin=feeds/base/new/OpenClash/luci-app-openclash` |
| 当前上游最新 0.48.002，同时发 `.apk` 与 `.ipk` | Release 资产：`luci-app-openclash-0.48.002.apk` 10,702,048 B |
| 包与架构无关 | Makefile `PKGARCH:=all`，`Build/Compile` 为空 |
| 依赖全部在官方 25.12.6 | `dnsmasq-full`、`bash`、`curl`、`ca-bundle`、`ip-full`、`ruby`、`ruby-yaml`、`kmod-tun`、`unzip`、`luci-compat`、`kmod-inet-diag`、`kmod-nft-tproxy`（逐个 grep 命中） |
| `dnsmasq-full` 默认带 nftset | `openwrt-25.12` 的 dnsmasq Makefile：`config PACKAGE_dnsmasq_full_nftset` → `default y` |
| 核心不在包内 | 监听进程 `clash` 的 `owner_package=未归属（非包安装）` |
| 设备实际模式 | `en_mode=fake-ip-tun`、`dns_port=653`、`operation_mode=fake-ip`、`rule` | 

### 6.2 三条路径

| | A. 自有 feed 编译（推荐） | B. 用上游 .apk / overlay | C. 替代 |
|---|---|---|---|
| 可行性 | 能，且设备已验证过同源代码可编译 | 能，但签名不受信 | 能 |
| 做法 | SDK + `po2lmo`，pin tag，签自有密钥，随 feed 发布 | 下载 apk，`--allow-untrusted` 或解包塞 rootfs | `nikki` / `homeproxy` / `localclash` |
| 功能缩水 | 无 | 无 | 有：订阅、规则、策略组要重做 |
| 维护成本 | 每 1–2 个月跟一次 tag，0.2 人日/次 | 每次发版重下 + 重算哈希 | 无 |
| 包管理一致性 | 好 | 差（无 apk 数据库记录） | 好 |
| 风险 | `luci-compat` 未来移除 | 签名/信任链、架构字段未核实 | 配置迁移成本高 |

**结论：A**。B 作为应急兜底；C 只在用户接受配置迁移时用。核心 `clash_meta` 由 overlay 预置并记录 sha256。

---

## 7. crontab 实际任务（以命令为准，不看注释）

清单里注释与命令错位，真实在跑的是：

| 计划 | 命令 | 实际用途 | 判定 |
|---|---|---|---|
| `0 0 * * *` | `acme renew` | 证书续期 | 保全 |
| `30 0 1 * *` | 日志截断 | 运维 | 保全 |
| `0 6 * * 1` | `openclash_chnroute.sh` | 中国 IP 路由表 | 保全 |
| `0 6 * * 3` | `openclash_geo.sh all` | GeoIP/GeoSite 更新 | 保全 |
| `0 7 * * *` | `/usr/share/openclash/openclash.sh` | 订阅更新 | 保全 |
| `3 3 12 12 *` | `nginx-util check_ssl` | 证书巡检 | 保全 |
| **`0 5 * * 2`** | **`/usr/share/mosdns/mosdns.uc update`** | **MosDNS Geo 数据**（注释写成了 Cloudflare） | 保全 |
| **`30 7,18,23 * * *`** | **`/usr/bin/cf-ip-auto --run`** | **Cloudflare IP 优选**（注释写成了 MosDNS） | 保全 |
| 已注释 | `/etc/cf-openwrt-auto.sh`、每周重启 | 旧方案 | 不保全 |

---

## 8. 明确不可能项 / 只能替代

| 项 | 结论 | 唯一出路 |
|---|---|---|
| SFE（`kmod-shortcut-fe-cm`） | **只能替代** | FlowCone，或改成源码构建 |
| fullconenat（`kmod-nft-fullcone`） | **只能替代** | FlowCone 的 RFC 4787 实现 |
| `luci-app-wireguard` | **不需要**（设备未装，上游已废弃） | 用 `luci-proto-wireguard` |
| `performance-manager` | **来源不明，建议放弃** | 找到源码再评估 |
| `3proxy` / `rill-runtime-preview` | 残留/实验件，**放弃** | — |
| `luci-app-cloudflare-ip` | **可以保全**，不是不可能项 | 发布进自有 feed |
| `banip` | 可恢复，但需用户确认 | 加包 + overlay 配置 |

---

## 9. 优先级与阶段表

### 9.1 分档

| 档 | 项 |
|---|---|
| **第一档：实测在跑、断网即感知** | OpenClash + 核心、mosdns（953）、dnsmasq→653、nginx/uwsgi/LuCI、WAN/PPPoE、双 WAN、WireGuard、NAT/fw4、Samba、DDNS |
| **第二档：实测在跑、影响面小** | cf-ip cron、acme、fail2ban、avahi/wsdd2、zram、aurora、addition-trans-zh、nginx-manager |
| **第三档：seed 意图或已坏** | banip（未装，默认放弃）、upnp_nat_relay（未运行，默认修）、bandix/arpbind/usbmuxd（未运行）、SFE（已定案 FlowCone 替代） |
| **放弃** | 3proxy、performance-manager、rill-runtime、`luci-app-wireguard` |

### 9.2 阶段与人日（FlowCone 定案后优化版）

| 阶段 | 人日 | 依赖 / 并行 | 主要变化 |
|---|---|---|---|
| P0 基线与决策 | 0.5 | 串行 | 决策已全部定案（§11 默认采纳），不再留空档 |
| P1 上游镜像与同步 | 1.0 | 串行 | — |
| P2 组装与构建骨架 | 1.5 | 串行 | — |
| P3 包与能力齐备 | 3.2 | P2 后；**与 P3b 并行** | 114 保留 + 3 替换物入清单、feed 接入、发布 2 个自研 app；banip 决策已关闭（−0.2）；残留清理 0.1 |
| **P3b OpenClash 自有 feed** | 1.5 | **P2 后即可启动**，不依赖 P3 | 只依赖 feed 密钥/索引与 SDK；与 P3 同时跑 |
| P4 运行期与失效检测 | 3.3 | P3 后（不等 P3b） | 11 项验收分档：8 项阻塞、3 项延后（0.5 人日移出首版）；FlowCone 只做 4.4 功能断言 |
| P5 自动升级流水线 | 3.5 | P4 后 | **吸收原 P6**：能力清单收口降级为一次评审，不独立成阶段 |
| **合计** | **14.5** | 关键路径 **13.0 天** | 0.5+1.0+1.5+3.2+3.3+3.5（P3b 被 P3 覆盖） |

原 16.7 → 14.5，减少 2.2：FlowCone 去掉性能验证 0.5、banip 决策关闭 0.2、P6 并入 P5 省 0.7、P4 非阻塞验收移出 0.5、残留清理口径收紧 0.1、延后项合并少一轮独立验证 0.2。另有 **0.5 人日**（P4 的 3 项延后验收）在首版后补做，不计入 14.5。

**P4 十一项验收分档**：

| 档 | 项 | 说明 |
|---|---|---|
| **阻塞首版（8）** | ① WAN/LAN 地址与 `192.168.2.1/24`、② LuCI 200、③ `ip_local_port_range`、④ mosdns 953、⑤ uci-defaults 清理、⑥ `net.device` + `fw4 check`、⑦ UPnP 断言、⑧ FlowCone 五条功能断言（4.4） | 不做完不发布 |
| **可延后（3）** | ⑨ `swrt-vm-performance` enabled、⑩ 非 `hv_netvsc` 网卡 ethtool 守卫、⑪ 失效检测负向用例（删目录 / 撞名 / 全量 `.meta.json` 缺失即失败） | 首版后补，0.5 人日 |

### 9.3 最小可用版（只做必做项）

| 含 | 内容 | 人日 |
|---|---|---|
| P0 + P1 + P2 | 骨架、同步、能出镜像 | 3.0 |
| P3 最小集 | 114 保留 + 3 替换物入清单、feed 接入、`check-packages`、nginx/uhttpd 冲突；**暂不发布** nginx-manager、cloudflare-ip | 2.2 |
| P3b | OpenClash（硬需求，不砍） | 1.5 |
| P4 最小集 | 只做 8 项阻塞验收 + FlowCone 5 条功能断言 | 2.4 |
| **合计** | 可启动、能上网、LuCI 可访问、代理可起、DNS 可用、FlowCone 生效 | **9.1** |

砍掉的部分：P5 自动流水线（改手动构建 + 手动发布）、两个自研 LuCI（首版后再发）、3 项延后验收。关键路径 7.6 天（P3b 与 P3 并行）。

---

## 10. 125 包逐个分类（最终基准：`SELF/X86/config_self.seed`）

依据 1.4：基准文件 sha256 `5f6e14f6…`、125 个 `CONFIG_PACKAGE_*=y`。"设备"列是 2026-11-11 清单实测版本；结论只用可核对事实（重复项、上游合并、已废弃、有等价物、设备未装/未运行），不凭感觉削。

### 10.1 统计

| 结论 | 数量 | 是哪几个 |
|---|---|---|
| **保留** | **114** | 其余全部；其中 3 个自研 app 需先发布，1 个 OpenClash 需 P3b |
| **替换** | **3** | `kmod-shortcut-fe-cm`、`kmod-fast-classifier` → FlowCone；`nat6` → OpenWrt-Add `nat6 1.0.0-r3`（nft） |
| **放弃** | **3** | `dnsmasq_full_ipset`、`luci-app-wireguard`、`kmod-ipt-nat6` |
| **需确认** | **5** | `banip`、`luci-app-banip`、`luci-i18n-banip-zh-cn`、`usbmuxd`、`luci-app-bandix`；**已按 §11 默认关闭**（3 个 banip 放弃、usbmuxd 放弃、bandix 保留包但不启用） |
| 合计 | 125 | — |

### 10.2 关键项结论与证据

| 能力 | 125 里的包 | 结论 | 证据 |
|---|---|---|---|
| OpenClash | `luci-app-openclash 0.47.156` | **保留**（自有 feed 编译） | OpenWrt-Add `legacy/OpenClash` 不发布；`vernesong/OpenClash` 的 `PKGARCH:=all`，依赖 `dnsmasq-full/bash/curl/ca-bundle/ip-full/ruby/ruby-yaml/kmod-tun/unzip/kmod-inet-diag/kmod-nft-tproxy/luci-compat` 在官方 25.12.6 全齐；走 P3b |
| nginx-manager | `luci-app-nginx-manager 1.8.7-r1` | **保留**（自有 feed 发布） | 自研；设备 `nginx-ssl 1.26.3` 占用 80/443/9083/5686，`uhttpd` 未运行 |
| cloudflare-ip | `luci-app-cloudflare-ip 2.6.0-r12` | **保留**（只发基础版） | 上游 Makefile 2.6.0-r13 `PKGARCH:=all`，依赖全为官方包；`-rill` 变体依赖 `rill-runtime-preview`（来源不明），不发布 |
| upnp-nat-relay | `luci-app-upnp-nat-relay 1.3.13-r1` | **保留包**，运行态待修 | 自有 feed 已发布；设备服务 `inactive`（不是 running），`miniupnpd` 也没装 → 列入 §11 |
| banip | `banip` + `luci-app-banip` + i18n | **需确认** | seed 有、设备从未安装（无包、无 `/etc/config/banip`、无进程） |
| SFE | `kmod-shortcut-fe-cm`、`kmod-fast-classifier` | **替换（定案）**：FlowCone + `kmod-nft-offload`，不再验证也不保留源码构建选项 | 4.2 四条路径只作"为什么带不进来"的背景；定案见 4.3、功能验收见 4.4 |
| IPv6 NAT | `nat6`、`kmod-ipt-nat6` | `nat6` 替换、`kmod-ipt-nat6` 放弃 | OpenWrt-Add `apps/FlowCone/openwrt/nat6` 提供 `nat6 1.0.0-r3`，`DEPENDS:=+kmod-nft-nat +nftables-json`；设备已有 `kmod-nft-nat` 与 `nftables-json 1.1.6` |
| DNS ipset | `dnsmasq_full_ipset` | **放弃** | 25.12 `dnsmasq/Makefile` 中 `PACKAGE_dnsmasq_full_ipset` 默认 `n`（L114），`PACKAGE_dnsmasq_full_nftset` 默认 `y`（L117）；设备未装 `ipset`，mosdns 走 nftset |

### 10.3 125 行全表

| # | 包 | 设备实测版本 | 结论 | 依据 |
|---|---|---|---|---|
| 1 | `addition-trans-zh` | 1.1-r52 | **保留** | OpenWrt-Add apps 已发布；中文翻译集合 |
| 2 | `ethtool` | 6.15-r1 | **保留** | 设备已装；官方 base 包 |
| 3 | `dnsmasq-full` | 2.93-r1 | **保留** | 设备已装；官方 base 包 |
| 4 | `dnsmasq_full_ipset` | **未装** | **放弃** | 25.12 的 dnsmasq-full 用 nftset（Makefile L117 默认 y），ipset 选项 L114 默认 n；设备也没装 ipset |
| 5 | `kmod-fs-f2fs` | 6.12.94-r1 | **保留** | 设备已装；官方 kernel kmod，f2fs 文件系统支持 |
| 6 | `luci-app-ddns` | 26.218.70234~1654281 | **保留** | 设备已装；官方 luci feed |
| 7 | `ddns-scripts` | 2.8.3-r5 | **保留** | 设备已装；官方 packages feed |
| 8 | `ddns-scripts-cloudflare` | 2.8.3-r5 | **保留** | 设备已装；官方 packages feed |
| 9 | `ddns-scripts-dnspod` | 2.8.3-r5 | **保留** | 设备已装；官方 packages feed |
| 10 | `ddns-scripts-services` | 2.8.3-r5 | **保留** | 设备已装；官方 packages feed |
| 11 | `ddns-scripts-aliyun` | 2.8.3-r5 | **保留** | 设备已装；官方 packages feed |
| 12 | `bind-host` | 9.20.26-r1 | **保留** | 设备已装；官方 packages feed |
| 13 | `firewall4` | 2025.03.17~b6e51575-r2 | **保留** | 官方包；需带 flow-offload 补丁（P4） |
| 14 | `kmod-nft-offload` | 6.12.94-r1 | **保留** | 官方 kmod；FlowCone 的软件流表依赖 |
| 15 | `arptables-nft` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 16 | `ebtables-nft` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 17 | `ip6tables-nft` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 18 | `xtables-nft` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 19 | `iptables-nft` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 20 | `iptables-mod-conntrack-extra` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 21 | `iptables-mod-iprange` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 22 | `iptables-mod-nat-extra` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 23 | `iptables-mod-socket` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 24 | `iptables-mod-tproxy` | 1.8.10-r3 | **保留** | 设备已装；官方 base 包 |
| 25 | `nat6` | 1.0.0-r2 | **替换** | 改用 OpenWrt-Add 的 nat6 1.0.0-r3（nft 版，依赖 kmod-nft-nat）；设备已有 nftables-json |
| 26 | `luci-nginx` | 26.218.70234~1654281 | **保留** | 设备已装；官方 luci feed |
| 27 | `luci-compat` | 26.218.70234~1654281 | **保留** | 设备已装；官方 luci feed |
| 28 | `kmod-wireguard` | 6.12.94-r1 | **保留** | 设备已装；官方 kernel kmod，IB 源可得 |
| 29 | `wireguard-tools` | 1.0.20250521-r1 | **保留** | 设备已装；官方 base 包 |
| 30 | `luci-proto-wireguard` | 26.218.70234~1654281 | **保留** | 设备已装；官方 luci feed |
| 31 | `luci-app-wireguard` | **未装** | **放弃** | 设备未装；上游 luci 已无此 app，WireGuard 由 luci-proto-wireguard 提供 |
| 32 | `qrencode` | 4.1.1-r2 | **保留** | 设备已装；官方 packages feed |
| 33 | `luci-app-aurora-config` | 1.2.0-r20260808 | **保留** | OpenWrt-Add themes 已发布 |
| 34 | `luci-app-arpbind` | 26.223.60283~0e88994 | **保留** | OpenWrt-Add apps 已发布 |
| 35 | `luci-app-mosdns` | 1.7.4-r1 | **保留** | OpenWrt-Add apps 已发布；设备 127.0.0.1:953 在跑 |
| 36 | `luci-i18n-mosdns-zh-cn` | 26.223.60283~0e88994 | **保留** | 同上 |
| 37 | `flock` | 2.41.5-r1 | **保留** | mosdns 定时更新用 flock 串行化（crontab 实测） |
| 38 | `luci-app-openclash` | 0.47.156 | **保留** | OpenWrt-Add legacy 不发布；由自有 feed 从 vernesong/OpenClash 编译（P3b） |
| 39 | `luci-app-upnp-nat-relay` | 1.3.13-r1 | **保留** | 自有 feed 已发布；但当前服务 inactive，运行态需在 P4 修（见 §11） |
| 40 | `luci-app-ramfree` | 26.223.60283~0e88994 | **保留** | OpenWrt-Add apps 已发布 |
| 41 | `coremark` | 2025.05.01-r1 | **保留** | 官方 packages 有同名包；设备上的是旧树构建 |
| 42 | `htop` | 3.5.1-r1 | **保留** | 设备已装；官方 packages feed |
| 43 | `kmod-shortcut-fe-cm` | 6.12.94-r8 | **替换** | 用 FlowCone + kmod-nft-offload；SFE 依赖内核 952/953，IB 无法重编，预编译包也装不上（见 4.2） |
| 44 | `kmod-fast-classifier` | 6.12.94-r6 | **替换** | 同上；依赖 skb->fast_forwarded（953 引入），vanilla 头文件编不出来 |
| 45 | `luci-app-bandix` | 0.12.8-r1 | **需确认** | 服务 enabled 但未运行（active with no instances）；保留包还是修，需你决定 |
| 46 | `luci-app-nginx-manager` | 1.8.7-r1 | **保留** | 自研，自有 feed 需发布；设备 80/443 由 nginx 提供 |
| 47 | `luci-app-cloudflare-ip` | 2.6.0-r12 | **保留** | 自研，自有 feed 需发布（只发基础版，不发 -rill） |
| 48 | `luci-theme-aurora` | 1.2.0-r20260808 | **保留** | OpenWrt-Add themes 已发布 |
| 49 | `luci-theme-bootstrap` | 26.218.70234~1654281 | **保留** | 设备已装；官方 luci feed |
| 50 | `usbmuxd` | 2025.02.01~523f7004-r1 | **需确认** | 服务 enabled 但未运行；x86 虚拟化环境无 USB 透传证据，建议放弃 |
| 51 | `kmod-tcp-bbr` | 6.12.94-r1 | **保留** | 设备已装；官方 kernel kmod，IB 源可得 |
| 52 | `libopenssl-conf` | 3.5.7-r1 | **保留** | 设备已装；官方 base 包 |
| 53 | `libopenssl-devcrypto` | 3.5.7-r1 | **保留** | 设备已装；官方 base 包 |
| 54 | `libopenssl-legacy` | 3.5.7-r1 | **保留** | 设备已装；官方 base 包 |
| 55 | `libcurl` | libcurl4 8.19.0（虚拟名） | **保留** | 虚拟名，设备实际装的是 libcurl4 8.19.0 |
| 56 | `kmod-crypto-acompress` | 6.12.94-r1 | **保留** | 设备已装；官方 kernel kmod，wireguard/openssl 依赖带出 |
| 57 | `kmod-crypto-aead` | 6.12.94-r1 | **保留** | 同上 |
| 58 | `kmod-crypto-authenc` | 6.12.94-r1 | **保留** | 同上 |
| 59 | `kmod-crypto-hash` | 6.12.94-r1 | **保留** | 同上 |
| 60 | `kmod-crypto-manager` | 6.12.94-r1 | **保留** | 同上 |
| 61 | `kmod-crypto-null` | 6.12.94-r1 | **保留** | 同上 |
| 62 | `kmod-crypto-seqiv` | 6.12.94-r1 | **保留** | 同上 |
| 63 | `kmod-crypto-user` | 6.12.94-r1 | **保留** | 同上 |
| 64 | `kmod-crypto-crc32c` | 6.12.94-r1 | **保留** | 同上 |
| 65 | `kmod-crypto-chacha20poly1305` | 6.12.94-r1 | **保留** | 同上 |
| 66 | `kmod-crypto-ghash` | 6.12.94-r1 | **保留** | 同上 |
| 67 | `kmod-crypto-hmac` | 6.12.94-r1 | **保留** | 同上 |
| 68 | `kmod-crypto-sha256` | 6.12.94-r1 | **保留** | 同上 |
| 69 | `kmod-crypto-sha512` | 6.12.94-r1 | **保留** | 同上 |
| 70 | `kmod-crypto-xts` | 6.12.94-r1 | **保留** | 同上 |
| 71 | `kmod-crypto-ctr` | 6.12.94-r1 | **保留** | 同上 |
| 72 | `kmod-crypto-gcm` | 6.12.94-r1 | **保留** | 同上 |
| 73 | `kmod-crypto-cbc` | 6.12.94-r1 | **保留** | 同上 |
| 74 | `kmod-crypto-md5` | 6.12.94-r1 | **保留** | 同上 |
| 75 | `kmod-crypto-sha1` | 6.12.94-r1 | **保留** | 同上 |
| 76 | `kmod-crypto-sha3` | 6.12.94-r1 | **保留** | 同上 |
| 77 | `kmod-crypto-rng` | 6.12.94-r1 | **保留** | 同上 |
| 78 | `block-mount` | 2026.05.23~16718b6e-r1 | **保留** | 设备已装；官方 base 包 |
| 79 | `zram-swap` | 32 | **保留** | 设备已装；官方 base 包 |
| 80 | `kmod-zram` | 6.12.94-r1 | **保留** | 设备已装；官方 kernel kmod，zram-swap 依赖带出 |
| 81 | `kmod-lib-lz4` | 6.12.94-r1 | **保留** | 同上 |
| 82 | `kmod-lib-zstd` | 6.12.94-r1 | **保留** | 同上 |
| 83 | `kmod-ikconfig` | 6.12.94-r1 | **保留** | 设备已装；官方 kernel kmod，IB 源可得 |
| 84 | `kmod-ipt-nat6` | 6.12.94-r1 | **放弃** | nft 版 nat6 不需要 iptables 的 nat6 模块；随旧 nat6 一起退场 |
| 85 | `kmod-nft-socket` | 6.12.94-r1 | **保留** | OpenClash nft tproxy 依赖 |
| 86 | `bash` | 5.3-r4 | **保留** | 设备已装；官方 packages feed |
| 87 | `bc` | 1.08.2-r1 | **保留** | 设备已装；官方 packages feed |
| 88 | `coreutils` | 9.9-r2 | **保留** | 设备已装；官方 packages feed |
| 89 | `coreutils-stat` | 9.9-r2 | **保留** | 设备已装；官方 packages feed |
| 90 | `drill` | 1.8.3-r1 | **保留** | 设备已装；官方 packages feed |
| 91 | `banip` | **未装** | **需确认** | seed 有、设备从未安装；恢复要新增包与配置，需你决定 |
| 92 | `luci-app-banip` | **未装** | **需确认** | 同上 |
| 93 | `luci-i18n-banip-zh-cn` | **未装** | **需确认** | 同上 |
| 94 | `fdisk` | 2.41.5-r1 | **保留** | 设备已装；官方 base 包 |
| 95 | `fstrim` | 2.41.5-r1 | **保留** | 设备已装；官方 base 包 |
| 96 | `jq` | 1.8.1-r2 | **保留** | 设备已装；官方 packages feed |
| 97 | `pv` | 1.9.31-r2 | **保留** | 设备已装；官方 packages feed |
| 98 | `resize2fs` | 1.47.3-r1 | **保留** | 设备已装；官方 base 包 |
| 99 | `zstd` | 1.5.7-r1 | **保留** | 设备已装；官方 packages feed |
| 100 | `git` | 2.50.1-r1 | **保留** | 设备已装；官方 packages feed |
| 101 | `git-http` | 2.50.1-r1 | **保留** | 设备已装；官方 packages feed |
| 102 | `gzip` | 1.14-r1 | **保留** | 设备已装；官方 packages feed |
| 103 | `bzip2` | 1.0.8-r1 | **保留** | 设备已装；官方 base 包 |
| 104 | `xz` | 5.8.1-r1 | **保留** | 设备已装；官方 packages feed |
| 105 | `xz-utils` | 5.8.1-r1 | **保留** | 设备已装；官方 packages feed |
| 106 | `coreutils-timeout` | 9.9-r2 | **保留** | 设备已装；官方 packages feed |
| 107 | `coreutils-nohup` | 9.9-r2 | **保留** | 设备已装；官方 packages feed |
| 108 | `gdisk` | 1.0.10-r1 | **保留** | 设备已装；官方 packages feed |
| 109 | `iperf3` | 3.20-r1 | **保留** | 设备已装；官方 packages feed |
| 110 | `pciutils` | 3.14.0-r2 | **保留** | 设备已装；官方 packages feed |
| 111 | `procps-ng` | 4.0.5-r1 | **保留** | 设备已装；官方 packages feed |
| 112 | `procps-ng-ps` | 4.0.5-r1 | **保留** | 设备已装；官方 packages feed |
| 113 | `procps-ng-top` | 4.0.5-r1 | **保留** | 设备已装；官方 packages feed |
| 114 | `procps-ng-pkill` | 4.0.5-r1 | **保留** | 设备已装；官方 packages feed |
| 115 | `procps-ng-free` | 4.0.5-r1 | **保留** | 设备已装；官方 packages feed |
| 116 | `usbutils` | 019-r1 | **保留** | 设备已装；官方 packages feed |
| 117 | `v2ray-geoip` | 20260812072327-r1 | **保留** | OpenWrt-Add apps 已发布；mosdns 数据源 |
| 118 | `v2ray-geosite` | 20260812072327-r1 | **保留** | 同上 |
| 119 | `ip-full` | 6.18.0-r2 | **保留** | 设备已装；官方 base 包 |
| 120 | `tcpdump` | 4.99.6-r1 | **保留** | 设备已装；官方 base 包 |
| 121 | `nano` | 9.2-r1 | **保留** | 设备已装；官方 packages feed |
| 122 | `tree` | 2.2.1-r2 | **保留** | 设备已装；官方 packages feed |
| 123 | `luci-app-samba4` | 26.218.70234~1654281 | **保留** | 设备已装；官方 luci feed |
| 124 | `openssh-sftp-client` | 10.3_p1-r1 | **保留** | 设备已装；官方 packages feed |
| 125 | `openssh-sftp-server` | 10.3_p1-r1 | **保留** | 设备已装；官方 packages feed |

### 10.4 首版范围冻结线与加包规则

**冻结线**：首版范围 = §10.3 的 **114 个保留包 + 3 个替换项对应的替换物**（`flowcone` + `kmod-nft-offload` 替代 SFE 两个 kmod；OpenWrt-Add `nat6` 替代旧 `nat6`）。**除此之外一律不进首版**：3 个放弃项、§11 的默认关闭项（`banip` 三件套、`usbmuxd` 不写；`luci-app-bandix` 写包但不启用）、§2.2 的 3 个残留、以及任何 125 之外的新包。

**加包判断规则（六条全满足才加）**：

1. 对应能力清单里已有的一行，或修复一个已复现的缺陷；"可能有用"不算。
2. 来自官方 25.12 feed 或自有签名 feed，x86_64 可安装；需源码编译的仅限 `PKGARCH:=all`。
3. 不引入内核补丁或非官方 kmod（否则回到 4.1 的架构问题，须重新评审）。
4. 不与已保留包的同名功能冲突（不搞双栈）。
5. 镜像增量 ≤ 5 MB、首启增量 ≤ 2 s；超出须写明理由。
6. 流程：issue 写清能力/缺陷与验收命令 → 能力清单加一行 → 评审 → 写入 `*.extra` → 下次构建自动验收。

默认拒绝；拒绝理由记在 issue，不写进包清单。

---

## 11. 默认采纳的决策（可推翻）

2026-11-11 用户未逐项答复的项，按以下默认值执行，标注「默认采纳、可推翻」；推翻只改结论，不阻塞阶段推进。

| # | 项 | 默认结论 | 推翻的代价 |
|---|---|---|---|
| 1 | SFE 是否 1:1 保留 | **FlowCone 替代（定案）**：SFE 与 fast-classifier 不保留，交付决定，不再评估 | 不适用（无兜底路径） |
| 2 | `banip` 三件套 | **放弃**；确需 IP 黑名单再单独立项 | +0.3 人日 |
| 3 | `usbmuxd` | **放弃**（从未运行，无 USB 透传证据） | 0 |
| 4 | `luci-app-bandix` | **保留包、默认不启用** | 0 |
| 5 | `luci-app-upnp-nat-relay` | **修到 running**，列入 P4 阻塞验收 ⑦ | 已在 P4 内 |

「默认采纳」意味着 P3/P4 不再等待答复；后续若推翻，只在对应阶段返工，不回滚已完成阶段。

---

## 12. 复核方法

```bash
# 设备清单再采集后，重算差额
python3 - <<'PY'
import json
d=json.load(open('openwrt-inventory-2026-11-11.json'))
dev={p['name'] for p in d['packages']}
seed={l.strip() for l in open('/tmp/self_y.txt')}
print("seed-only:", sorted(seed-dev))
print("device-only custom:", sorted(n for n in dev-seed if n in {
 '3proxy','performance-manager','rill-runtime-preview','luci-app-cloudflare-ip-rill'}))
PY

# OpenClash 新版本
curl -sL https://api.github.com/repos/vernesong/OpenClash/releases/latest | jq -r '.tag_name, .assets[].name'
```
