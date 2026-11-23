# YunWrt 迁移分期方案

- 日期：2026-11-11（v6，FlowCone 定案 + 工期压缩）
- 状态：待评审（未执行）
- 目标仓库：`hello-yunshu/YunWrt`（尚未创建）
- 上游：`QiuSimons/YAOF` 分支 `25.12` + `QiuSimons/OpenWrt-Add` 的二进制 feed
- 范围：仅 x86_64；不 fork 上游、不给上游开 PR
- 功能保全：逐项审计见 [2026-11-11-yunwrt-capability-preservation.md](./2026-11-11-yunwrt-capability-preservation.md)（含 FlowCone 定案 §4.3、功能验收 §4.4、范围冻结 §10.4）
- 总人日：**14.5**（关键路径 13.0 天；最小可用版 9.1）
- 审计基准：
  - 旧结构最后一版：`paniy-little/SWrt@ea88a4d`（2026-10-27，`d74f4e2` 的父提交）
  - 新架构落地版：`paniy-little/SWrt@7374697`（2026-11-03）；架构切换点是 `d74f4e2`（2026-10-29 首次同步，删掉 `SCRIPTS/`、`SEED/`、`PATCH/`）
  - 现 `origin/25.12` tip：`ddc7422`（2026-11-11 自动同步），`SCRIPTS/`、`PATCH/`、`SEED/` 已被 `sync-files.yml` 删除，但活跃 workflow 仍引用前两者 → **当前不可构建**；功能对照以 `SELF/X86/config_self.seed`（sha256 `5f6e14f6…`）为准，证据见保全审计 1.4
  - 补丁：78 个 `.patch`（另有 10 个 `sfe-v2/package` 源文件、4 个 overlay、1 个 README）
  - overlay：18 个文件（`PATCH/files` 4 + `SELF/X86/common/files` 14）
  - 设备实际启用包：`SELF/X86/config_self.seed` 中 125 个 `=y`

> 约束：本文件只是计划。不创建仓库、不推送、不在现有 SWrt 仓库提交迁移代码。

---

## 0. 取向修正

### 0.1 从"钉死复现"改为"跟随上游 + 保留能力 + 失效可见"

前三版的方向是"把设备那一刻的状态钉死"：锁版本、锁 feed 哈希、要求包清单逐次一致、能用快照复现。这个方向被否掉了，理由是它和 YAOF 的演进方式正面冲突——上游本来就是滚动更新，钉死等于把一个滚动项目手动改成静态项目，维护成本持续累积，收益只是"看起来稳定"。

新取向三条：

| 原则 | 含义 | 反面（不允许） |
|---|---|---|
| **跟随** | 版本完全由 YAOF 分支和上游 `resolve_version()` 决定，升到 25.13 也照走 | 仓库里出现版本号常量、版本策略枚举、点版本白名单 |
| **保留** | 只保住我实际在用的能力，其余跟着上游变 | 要求包集合等同、要求配置逐项对齐、要求两份镜像包清单一致 |
| **可见** | 本地覆盖与补丁一旦失效必须显式失败或告警，每次自动构建都要跑能力验收 | "构建成功就当没事"、静默跳过、只留日志不报警 |

### 0.2 风险判断的转移

钉死方案防的是"版本漂移"，但真正的故障源不在这里。`overlay/` 和 `patches/` 是**按上游具体文件内容写的**：上游删了一个路径、`fw4.uc` 改了结构、`miniupnpd` 的 init 换了写法，覆盖就会静默失效——镜像照样构建成功，问题在设备上才暴露。

所以本版把投入从"锁定"移到"失效检测"：

- 每个 overlay 文件、每个补丁都带 base 判据；对不上就失败。
- 每次上游变更触发的构建都必须跑能力验收；验收不过，流水线红，不发布。

### 0.3 反向约束（防止以后有人把锁定加回来）

以下内容**不得**加入本仓库，除非重新走一次决策评审：

- `versions.env` 或任何形式的 OpenWrt 版本常量 / 策略枚举；
- 包清单基线文件（`baseline-*.txt`）与"包集合必须覆盖基线"的判据；
- feed 索引 / 公钥 / 工具的哈希校验门禁；
- `feed-snapshot` 之类的产物归档与"用快照复现"验收；
- 三段式 `upstream.lock` 与回退语义。

构建记录仍然保留上游版本、内核、包清单、feed 索引哈希——但它们是**排障线索**，不参与任何校验或回退。

---

## 1. 目标与不变量

### 1.1 目标

1. 能持续接收 YAOF 的更新，同步过程不覆盖 YunWrt 的自研内容。
2. 保住设备上**实际在用**的能力（见第 7 节能力清单），其余跟随上游。
3. 本地覆盖与补丁失效时，构建当场失败或明确告警，不静默降级。
4. 上游一变就能自动构建并跑能力验收，结果对人有可见的出口。

### 1.2 不变量

- `upstream/yaof/` 必须逐字节等于上游，任何自研改动不得写进该目录。
- 构建树 `build/` 每次由 `upstream/yaof/` 重新生成，不手工修改、不提交。
- 每个覆盖上游已有文件的 overlay 项，必须能证明"我覆盖的是我以为的那个文件"。
- 构建成功 ≠ 交付成功；能力验收不过的产物不发布。
- 缺包必须失败，不允许"WARNING 后继续出镜像"。

---

## 2. 总体路线

### 2.1 上游构成（已核实）

| 仓库 | 分支 | 角色 | 引用证据 |
|---|---|---|---|
| `QiuSimons/YAOF` | `25.12` | **唯一源码树**：`scripts/build.sh` + `profiles/` + `files/` | 根目录仅 `files/ profiles/ scripts/ README.md LICENSE .github/` |
| `QiuSimons/OpenWrt-Add` | `master` | **二进制 feed 生产端**，不参与构建树 | `scripts/build.sh` L97 公钥、L101 索引、L115 `clean-overlay.sh`、L124 写 `repositories.d` |
| FlowCone / openwrt-natflow / vmlinux-btf / luci-app-honk 等 | 各自 | OpenWrt-Add 的源码输入，二级依赖 | `OpenWrt-Add/core/scripts/packages.conf` |
| Chnroute / geoip-moedove / openwrt-mos | `master` | 与本项目无关，全链路无引用 | `grep` 无命中 |

两条会影响判断的事实：

- YAOF **从不 clone** OpenWrt-Add，只在构建时按 URL 取公钥、索引和一个脚本。
- `build-feed.yml` 只把 `apps/*` 与 `themes/*` 打进 feed；`legacy/*`（OpenClash 在这里）与 `invasive/*` 不发布。

### 2.2 仓库形态

```text
YunWrt/
├── upstream/
│   └── yaof/                     # YAOF 纯净镜像（同步机器人唯一可写）
├── overlay/
│   ├── files/                    # rootfs 覆盖层
│   │   └── <每个 override 项旁放 .meta.json>
│   ├── packages/
│   │   ├── common.extra
│   │   ├── x86_64.extra
│   │   └── disabled/
│   └── keys/yun-feed.rsa.pub
├── patches/
│   ├── fw4-flow-offload/         # *.patch + meta.json + README.md
│   └── miniupnpd-force-forwarding/
├── scripts/
│   ├── sync-upstream.sh          # YAOF → upstream/yaof/
│   ├── check-upstream.sh         # 接口漂移（硬失败）
│   ├── assemble.sh               # 组装 + base 校验
│   ├── verify-bases.sh           # overlay/patch 失效检测
│   ├── check-packages.sh         # 缺包 fail-fast
│   ├── build.sh                  # 唯一构建入口
│   └── qemu-capability.sh        # 能力验收
├── .github/workflows/
│   ├── sync-and-build.yml        # 上游变更 → 构建 → 冒烟
│   └── release.yml
├── build-log/                    # 每次构建的只读快照（排障用）
└── docs/
    ├── plans/2026-11-11-yunwrt-migration.md
    ├── decisions/                # D1–D9
    ├── capabilities.md           # 能力清单
    └── runbooks/build-and-release.md
```

**没有 `versions.env`**。构建入口不传版本，全部交给上游 `build.sh` 的 `resolve_version()`。

### 2.3 分层职责边界

| 层 | 位置 | 职责 | 禁止事项 | 同步是否覆盖 |
|---|---|---|---|---|
| 上游源码层 | `upstream/yaof/` | 原样保存 YAOF | 任何手工修改 | 每次 `rsync --delete` 覆盖 |
| overlay 文件层 | `overlay/files/` | rootfs 文件、配置、init.d、uci-defaults、sysctl | 改二进制、改包内已编译逻辑 | 不覆盖 |
| 包清单层 | `overlay/packages/*.extra` | 追加/删除包名 | Kconfig 选项、编译参数 | 不覆盖 |
| patch 层 | `patches/` | 生成少数必须覆盖的运行时文件 | 改 `upstream/yaof/`、改内核、批量打补丁 | 不覆盖 |
| 自有 feed 层 | `yun-openwrt-packages` | 自研 LuCI、必要的 kmod | 覆盖上游同名包 | 外部仓库 |
| 构建参数层 | `scripts/build.sh` | partsize、产物命名、调用上游 | 改 ImageBuilder 内部逻辑 | 自有 |

一句话规则：能用文件覆盖解决的 → overlay；只能改已编译死的逻辑 → patch 生成 overlay 文件；需要新功能 → 自有 feed；需要改构建行为 → 构建参数层；都不行 → 放弃。

### 2.4 构建流水线

```text
scripts/build.sh
  1. check-upstream.sh     YAOF 接口漂移 → 硬失败
  2. verify-bases.sh       overlay/patch 的 base 判据 → 硬失败或显式告警
  3. check-packages.sh     按本次解析出的 release 校验包 → 硬失败
  4. assemble.sh
       rm -rf build && cp -a upstream/yaof/. build/
       cp -a overlay/files/. build/files/
       cp -a overlay/keys/.  build/keys/
       cat overlay/packages/*.extra >> build/profiles/packages.*
       apply patches → build/files/...（.rej 检查）
  5. bash build/scripts/build.sh x86 64 generic packages.x86_64
       ↑ 不传版本；上游自行解析最新 25.12.x，自行拉 IB、公钥、索引、clean-overlay.sh
  6. 写 build-log/<timestamp>.json + 重命名产物
```

### 2.5 构建快照日志

`build-log/<YYYYMMDDTHHMMSSZ>-<yaof-sha7>.json`，每次构建写一份，只增不改：

```yaml
built_at: "2026-11-11T13:20:00Z"
yaof_commit: "<sha>"
openwrt_version: "25.12.6"        # 实际解析值
kernel_version: "6.12.112"
kmod_vermagic: "6.12.112-1-<hash>"
feed_index_sha256: "<sha>"        # 只作线索
feed_index_checked_at: "…"
packages: [ … ]                   # 实际安装的全量包与版本
capability_result: "pass|fail"
failed_capabilities: [ … ]
```

定位是**排障线索**：

- 不参与任何门禁；
- 不用于回退（回退就是 `git revert` 或重新触发构建）；
- 不承诺"同输入同输出"——上游 feed 每天重建，本来就不成立。

### 2.6 失效检测（本版重点）

三种失效形态，各自有判据：

| 形态 | 例子 | 判据 | 结果 |
|---|---|---|---|
| 覆盖目标消失 | 上游删了某个 `/etc/uci-defaults/*` 或改了目录 | `upstream_path` 不存在 | **硬失败** |
| 覆盖目标变形 | `fw4.uc` 的 `parse_zone()` 重写、`miniupnpd.init` 换成 procd 新写法 | base 文件 `sha256` 不符 / 结构断言不匹配 | **硬失败** |
| 新增文件撞名 | 我们在 `overlay/files` 新增的文件，上游后来也加了一个同名文件 | `kind: add` 且上游已存在该路径 | **告警 + 标记 need-review** |

实现方式：

- 每个覆盖项旁边放 `<文件>.meta.json`：

```json
{
  "kind": "override",
  "upstream_path": "usr/share/ucode/fw4.uc",
  "base_sha256": "<上游原文件哈希>",
  "asserts": ["function parse_zone", "related_physdevs"],
  "reason": "flow offload 需要 net.device 而不是 net.physdev",
  "owner": "yunwrt"
}
```

- `verify-bases.sh` 从**本次解析出的 release** 取基准文件（官方 feed 里对应包的 apk 解包，或 `openwrt/packages` 对应 tag 的源码），逐项比对。
- 默认硬失败；只有手动构建允许 `--allow-base-drift`，此时产物**不得发布**，并自动生成一条 need-review 记录。
- 补丁层同理：`patches/<name>/meta.json` 记录被改文件的 base 哈希与断言，`assemble.sh` 应用前先校验。

---

## 3. 分期计划

总工作量：**14.5 人日**（v5 为 16.7；按 FlowCone 定案与并行化压缩 2.2，另 0.5 人日非阻塞验收放到首版后）。明细见保全审计 9.2–9.3。

**三条硬约束**：

1. **SFE 与 fast-classifier 不保留，由 FlowCone 替代（交付决定，审计 4.3）**：不做性能验证，不保留源码构建选项。
2. **P3b 与 P3 并行**：P3b 只依赖 feed 密钥/索引与 SDK，在 P2 完成后即可启动。
3. **P5 吸收原 P6**：能力清单收口是同一次发布评审，不再独立成阶段。

**范围来源单一化**：P3 / P3b / P4 / P5 一律引用保全审计 `§10` 的 125 包分类结果——`§10.1` 是统计口径（保留 114 / 替换 3 / 放弃 3 / 默认关闭 5），`§10.2` 是关键项结论与证据，`§10.3` 是 125 行逐包结论表，`§10.4` 是首版范围冻结线与加包规则。

### P0 基线与决策冻结（0.5 人日）

| 项 | 内容 |
|---|---|
| 目标 | 冻结决策，建立空骨架与能力清单骨架 |
| 动作 | 1. 确认 D1–D9 并写成 ADR；2. 建目录骨架；3. 建 `docs/capabilities.md` 骨架，先填「我在用的能力」列；4. 本计划与审计结论入库 |
| 产出 | 目录骨架、9 份 ADR、能力清单骨架 |
| 验收 | ① 仓库内 `grep -rE "25\.12\.[0-9]" .` 无输出（除文档引用）；② 9 份 ADR 均有结论与日期；③ 能力清单覆盖第 7 节 7 项能力域；④ 目录树与 2.2 一致 |
| 依赖 | 无 |
| 回退 | 删除分支，无外部副作用 |
| 闸门 | G0：决策签字 + 能力清单骨架就位 |

### P1 上游镜像与同步（1.0 人日）

| 项 | 内容 |
|---|---|
| 目标 | `upstream/yaof/` 忠实镜像；同步幂等；接口变化可感知 |
| 动作 | 1. `sync-upstream.sh`（`rsync -a --delete --exclude=.git`）；2. `check-upstream.sh` 只查真会让我们失效的东西：`build.sh` Usage/参数个数、`profiles/packages.*` 存在性、`files/` 关键路径、workflow 调用方式；3. 定时 workflow 只提 PR |
| 产出 | 同步与检查脚本、同步 PR 模板 |
| 验收 | ① `diff -rq upstream/yaof/ /tmp/yaof` 无输出；② 连续两次同步，第二次 `git status --porcelain upstream/yaof/` 为空；③ 改 `build.sh` Usage 行 → `check-upstream.sh` 非 0；④ 删除 `profiles/packages.common` → 非 0；⑤ 上游发新 commit 时同步 PR 只改 `upstream/yaof/` |
| 依赖 | P0 |
| 回退 | `git revert` 同步提交 |
| 闸门 | G1：五项验收全过 |

### P2 组装与构建骨架（1.5 人日）

| 项 | 内容 |
|---|---|
| 目标 | 不加自研内容也能出镜像，构建记录可用 |
| 动作 | 1. `assemble.sh`；2. `scripts/build.sh` 包装（不传版本、固定 partsize、产物命名）；3. 写 `build-log`；4. `.rej` 与路径断言 |
| 产出 | 构建入口、首个 x86 镜像、build-log |
| 验收 | ① `./scripts/build.sh` 退出 0；② 产出镜像且 `sha256sum` 生成；③ `jq -e '.yaof_commit and .openwrt_version and .kernel_version and (.packages\|length>0)' build-log/*.json` 通过；④ 记录的 `openwrt_version` 与下载站最新 25.12.x 一致（不硬编码）；⑤ 全树无 `*.rej` |
| 依赖 | P1 |
| 回退 | 直接调 `build/scripts/build.sh` |
| 闸门 | G2：五项验收全过 |

### P3 包与能力齐备（3.2 人日，可与 P3b 并行）

| 项 | 内容 |
|---|---|
| 目标 | 保全审计 §10.3 标「保留」与「替换」的包都能装上，标「放弃」「默认关闭」的不进首版，缺包当场失败 |
| 范围 | 直接引用保全审计 §10.3：写入 §10.1 的 **114 个保留包** + **3 个替换项的替换物**（`flowcone` + `kmod-nft-offload` 替代 `kmod-shortcut-fe-cm`/`kmod-fast-classifier`；OpenWrt-Add `nat6` 替代旧 `nat6`）；**不写入 3 个放弃项**（`dnsmasq_full_ipset`、`luci-app-wireguard`、`kmod-ipt-nat6`）与 §11 的默认关闭项（`banip` 三件套、`usbmuxd` 不写；`luci-app-bandix` 写包但默认不启用） |
| 动作 | 1. 按 §10.3「保留」列写 `common.extra` / `x86_64.extra`；2. 接入 `yun-openwrt-packages`（公钥 + repositories，与 OpenWrt-Add 并存），并把密钥/索引准备好供 P3b 并行使用；3. `check-packages.sh` 查官方索引与两个外部 feed，缺失即失败；4. 处理 `luci` 元包与 nginx 栈冲突；5. 补 `luci-app-nginx-manager`、`luci-app-cloudflare-ip`（只发基础版，不发 `-rill`）进自研 feed 构建矩阵（当前 feed 只发布了 `upnp-nat-relay`），依据 §10.2 对应两行；6. 引入 OpenClash 源码入口（§10.2 OpenClash 行），交接给并行的 P3b；7. `banip` 三件套按 §11 默认**不写入**，不再等待答复；8. 清理残留项 `3proxy`、`performance-manager`、`rill-runtime-preview`（不在 125 内，见 §2.2），不写入包清单 |
| 产出 | 包清单、feed 接入、缺包检查 |
| 验收 | ① `check-packages.sh` 退出 0；② 镜像内 `apk info -e` 对 §10.3 标「保留」的 114 个包与 3 个替换物全部成功；③ `pgrep uhttpd` 为空且 80 端口仅 nginx；④ 故意写错一个包名 → `check-packages.sh` 非 0；⑤ 上游移除依赖包时，构建失败并指出包名；⑥ `grep -E '^(3proxy|performance-manager|rill-runtime-preview|dnsmasq_full_ipset|luci-app-wireguard|kmod-ipt-nat6)$' overlay/packages/*` 无输出 |
| 依赖 | P2（与 P3b 并行，不等 P3b） |
| 回退 | 收缩 `*.extra`，保留 fail-fast |
| 闸门 | G3：六项验收全过（只证明"包在"） |

### P3b OpenClash 自有 feed（1.5 人日，P2 后即可启动）

| 项 | 内容 |
|---|---|
| 目标 | OpenClash 作为自有 feed 的签名包随镜像发布，不依赖 OpenWrt-Add `legacy/` |
| 范围 | 保全审计 §10.3 第 38 行 `luci-app-openclash 0.47.156`（结论「保留」，实现方式「自有 feed 编译」）+ §10.2 OpenClash 行（依赖在官方 25.12.6 全齐）+ §6 三条路径（`legacy/` 不发布 → 只能自建） |
| 动作 | 1. `vernesong/OpenClash` 固定 tag 拉取 `luci-app-openclash`；2. SDK + `po2lmo` 编译，产出 `PKGARCH:=all` 的 `.apk`；3. 用 yun feed 私钥签名，更新 `packages.adb` 索引；4. `clash_meta` 核心由 overlay 预置并记录 sha256（核心不在包内）；5. 订阅与 chnroute/geo 脚本沿用 OpenClash 自带，不二次开发；6. 记录 tag → 包版本的对应关系，供 P5 跟随 |
| 产出 | 签名后的 `luci-app-openclash` `.apk`、索引条目、核心预置目录 |
| 验收 | ① `apk info -e luci-app-openclash` 成功且版本等于所 pin 的 tag；② 冷启动后 `pgrep -f clash` 有进程；③ `uci get openclash.config.enable` = `1`；④ `/etc/init.d/openclash status` 为 running；⑤ `sha256sum /etc/openclash/core/clash_meta` 与记录一致 |
| 依赖 | **P2**（只依赖 feed 密钥/索引与 SDK，不依赖 P3 的包清单完成） |
| 回退 | 从 `*.extra` 摘掉 OpenClash，退回 `nikki`（OpenWrt-Add 已发布） |
| 闸门 | G3b：五项验收全过 |

### P4 运行期能力与失效检测（3.3 人日，重心；不等 P3b）

| 项 | 内容 |
|---|---|
| 目标 | 设备在用的运行期能力还原；overlay/patch 的失效能当场发现；能力清单有可执行验收 |
| 范围 | 保全审计 §10.2 四个关键行：SFE 两个 kmod → FlowCone（已定案，4.3）；`nat6` → OpenWrt-Add nft 版（替换）；`upnp-nat-relay` → 修到运行态（§11 第 5 项，默认采纳）；`dnsmasq_full_ipset` → 不处理（已放弃）。另含 §5 的 overlay 类能力（网络初始化、Hyper-V 调优、mosdns hotplug），§7.2 的「转发加速」行 |
| 动作 | 1. 迁移 `swrt-vm-performance`（init+config）、`90-swrt-x86-network.conf`、`swrt-vm-perf`、Hyper-V ring 脚本；2. **重写** `99-x86-vm-network`（以 `network.wan` 缺失为触发，处理 `br-lan` 与 `ports`，保留 MAC 映射）；3. 恢复 mosdns hotplug + `flock`；4. 默认 IP `192.168.2.1` 用 uci-defaults；5. 沿用上游 `rc.local`（D1）；6. 覆盖项补 `.meta.json`（首版只要求两个 patch + 4 个关键 overlay）；7. `patches/fw4-flow-offload`、`patches/miniupnpd-force-forwarding` 带 base 哈希与断言；8. `verify-bases.sh` 三种失效形态；9. `qemu-capability.sh` 跑阻塞验收；10. **FlowCone 功能落地**：装 `flowcone`，跑审计 4.4 的 5 条功能断言（包 + BTF、进程、nft flowtable/offload、`dmesg` 无报错、SFE 选项消失）；11. **nat6 替换落地**：装 OpenWrt-Add `nat6 1.0.0-r3`，断言 `kmod-nft-nat` 在、旧 `kmod-ipt-nat6` 不在；12. **修 `upnp-nat-relay` 运行态**：随镜像带 `miniupnpd-nftables`，服务须 running |
| 产出 | `overlay/files/` 全量、两个 patch 目录、`verify-bases.sh`、`qemu-capability.sh`、FlowCone 功能断言报告（审计 4.4 五条） |
| 验收（阻塞首版 8 项） | ① QEMU 启动后 `uci -q get network.wan` 非空，LAN 有 `192.168.2.1/24`；② `http://192.168.2.1/` 返回 200；③ `sysctl net.ipv4.ip_local_port_range` = `10240 65535`；④ mosdns 进程 running 且 `127.0.0.1:953` 监听；⑤ 首启后 `/etc/uci-defaults/99-x86-vm-network` 被删除；⑥ `grep -q net.device` 命中且 `fw4 check` 退出 0；⑦ `uci get upnpd.@upnpd[0].ext_allow_private_ipv4` = `1` 且 `upnp_nat_relay` running；⑧ FlowCone 五条功能断言全过（审计 4.4） |
| 验收（可延后 3 项，0.5 人日） | ⑨ `swrt-vm-performance` enabled；⑩ 非 `hv_netvsc` 网卡无 `ethtool -G`；⑪ 失效检测负向用例（删上游目录 → "目标消失"；新增同名文件 → 告警 need-review；全量 `.meta.json` 缺失即失败） |
| 依赖 | P3（不等 P3b；OpenClash 的运行断言在 P5 复跑） |
| 回退 | 按文件粒度回退；`--allow-base-drift` 只用于手动排障 |
| 闸门 | G4：8 项阻塞验收全过，FlowCone 5 条断言缺一条即不发布 |

### P5 自动升级流水线 + 能力清单收口（3.5 人日，吸收原 P6）

| 项 | 内容 |
|---|---|
| 目标 | 上游一变就自动构建并跑能力验收；失败对人可见；能力清单在同一次发布评审里定稿 |
| 范围 | 流水线本身不新增包；验收对象是 P3 写入的 §10.1「114 保留 + 3 替换物」与 P3b 的 OpenClash；跟踪项只有 §10.2 OpenClash 行（新 tag 才重编）与 §10.2 SFE 行（FlowCone 断言随上游变） |
| 动作 | 1. `sync-and-build.yml`：定时拉起上游 → 有变更才继续；2. 构建 → QEMU 能力验收（P4 的 8 项阻塞 + P3b 的 G3b）→ 汇总表；3. 失败出口：Job Summary、自动开/更新 Issue、Release 不发布、产物保留；4. `release.yml` 只在能力验收通过时发布；5. OpenClash tag 跟随（有新 tag 才重编）与新核心 sha256 校验；6. **能力清单收口**：复核 7 个能力域每行「首版要有 / 用替代 / 不要」，清理全部「待定」，三方对照表入库，写 runbook |
| 产出 | 两条 workflow、验收报告产物、失败通知机制、定稿 `docs/capabilities.md`、`docs/runbooks/build-and-release.md`、三方对照表 |
| 验收 | ① 上游无变更时流水线在同步步骤快速结束，不触发构建；② 人为制造上游内容变更 → 自动构建并产出能力报告；③ 人为让一个能力断言失败 → 流水线红、自动开 Issue、Release 步骤被跳过；④ Job Summary 能直接看到逐项能力通过/失败；⑤ 失败产物可下载；⑥ OpenClash 无新 tag 时不重编，有新 tag 时产出新包并通过 G3b 断言；⑦ 能力清单无「待定」且每行与最近一次验收报告一致 |
| 依赖 | P4 与 P3b（两者都完成后才能定稿） |
| 回退 | 关闭 schedule，保留 `workflow_dispatch` |
| 闸门 | G5：七项验收全过（原 G5 + G6 合并） |

---

## 4. 阶段闸门与并行关系

| 闸门 | 位置 | 通过条件 | 不通过怎么办 |
|---|---|---|---|
| G0 | P0 后 | 决策签字 + 能力清单骨架 | 停在 P0 |
| G1 | P1 后 | 镜像一致 + 同步幂等 + 接口检查可用 | 修脚本 |
| G2 | P2 后 | 能出镜像 + build-log 完整 | 修组装 |
| G3 | P3 后 | 保全审计 §10.1 的 114 保留包 + 3 替换物全部可装 + 缺包失败 + nginx 栈无冲突 + 3 个放弃项与 5 个默认关闭项未入清单 | 收缩或补 feed |
| G3b | P3b 后 | OpenClash 可装、可起核、核心哈希一致 | 退回 `nikki` |
| G4 | P4 后 | 8 项阻塞验收全过（FlowCone 按审计 4.4 五条功能断言判定） | 缺一条即停下补，不发布 |
| G5 | P5 后 | 流水线 6 项 + 能力清单零待定（原 G5 + G6） | 关闭定时 / 退回补做 |

```text
P0 → P1 → P2 ─┬→ P3  ─┐
              └→ P3b ─┴→ P4 → P5
```

**并行与关键路径**：

- P3b 只依赖 P2 的 feed 密钥/索引与 SDK，与 P3 同时跑；其 1.5 人日完全落在 P3 的 3.2 人日窗口内。
- P4 只等 P3，不等 P3b；OpenClash 的运行断言在 P5 首轮构建复跑。
- P5 等 P4 与 P3b 都完成，因为能力清单要覆盖 OpenClash。
- 关键路径 = P0 0.5 + P1 1.0 + P2 1.5 + P3 3.2 + P4 3.3 + P5 3.5 = **13.0 天**；总人日 **14.5**（P3b 的 1.5 已含）。

其余阶段保持串行的理由：P4 的断言就是 P5 流水线要跑的那套，串起来失败定位更直接；拆并行只会增加"哪一层坏了"的排查成本。

**最小可用版（9.1 人日，关键路径 7.6 天）**：只做 P0+P1+P2、P3 最小集（暂不发布 nginx-manager/cloudflare-ip）、P3b、P4 的 8 项阻塞验收；先不做 P5 自动流水线（改手动构建+手动发布）、两个自研 LuCI、3 项延后验收。明细见保全审计 9.3。

---

## 5. 任务清单（可直接拆 issue）

### P0
- [ ] D1–D9 ADR
- [ ] 目录骨架
- [ ] `docs/capabilities.md` 骨架 + 「我在用的能力」列
- [ ] 入库计划与审计摘要

### P1
- [ ] `scripts/sync-upstream.sh`
- [ ] `scripts/check-upstream.sh`（Usage / 参数 / profiles / files 关键路径 / workflow 调用）
- [ ] `.github/workflows/sync-and-build.yml` 的同步部分
- [ ] 幂等与接口漂移负向测试

### P2
- [ ] `scripts/assemble.sh`
- [ ] `scripts/build.sh` 包装（不传版本）
- [ ] `build-log` 写入
- [ ] `.rej` 与路径断言
- [ ] 首次 x86 构建

### P3（与 P3b 并行）
- [ ] 按保全审计 §10.3「保留」列写 `common.extra` / `x86_64.extra`（114 个 + 3 替换物）
- [ ] `yun-openwrt-packages` 接入，密钥/索引交接给 P3b
- [ ] 补 `luci-app-nginx-manager`、`luci-app-cloudflare-ip`（基础版）进 feed
- [ ] `scripts/check-packages.sh`
- [ ] nginx 与 uhttpd 冲突处理
- [ ] OpenClash 源码入口（pin tag）交接 P3b
- [ ] `banip` 三件套按默认不写入（§11）
- [ ] 残留项（3proxy / performance-manager / rill-runtime-preview）不入清单

### P3b（P2 后即可启动）
- [ ] 编译 `luci-app-openclash`（SDK + `po2lmo`）
- [ ] 签名并更新 `packages.adb`
- [ ] `clash_meta` 核心预置 + sha256 记录
- [ ] tag → 版本对应表

### P4
- [ ] 迁移 `swrt-vm-performance`（init + config）〔可延后〕
- [ ] 迁移 `90-swrt-x86-network.conf`、`swrt-vm-perf`
- [ ] 迁入 Hyper-V ring 脚本
- [ ] 重写 `99-x86-vm-network`
- [ ] 恢复 mosdns hotplug + `flock`
- [ ] 默认 IP `192.168.2.1`
- [ ] 关键覆盖项补 `.meta.json`（阻塞）、全量补齐〔可延后〕
- [ ] `patches/fw4-flow-offload/`（patch + meta + README）
- [ ] `patches/miniupnpd-force-forwarding/`
- [ ] `scripts/verify-bases.sh`（目标消失 / 变形 / 撞名）
- [ ] `scripts/qemu-capability.sh`（8 项阻塞）
- [ ] FlowCone 安装 + 按审计 4.4 跑 5 条功能断言
- [ ] `nat6` 换 OpenWrt-Add nft 版
- [ ] `upnp-nat-relay` 修到 running（默认采纳）
- [ ] 负向测试：base 哈希改错（阻塞）；删目录 / 撞名 / 全量 `.meta.json`〔可延后〕

### P5（含能力清单收口）
- [ ] `sync-and-build.yml`（构建 + 冒烟 + 汇总）
- [ ] 失败出口：Job Summary / Issue / 不发布 / 保留产物
- [ ] `release.yml`（验收通过才发布）
- [ ] OpenClash tag 跟随与核心校验
- [ ] IB 缓存与清理
- [ ] 能力清单逐行定稿 + `docs/runbooks/build-and-release.md` + 三方对照表

---

## 6. 决策点

| # | 决策 | 结论 | 理由 |
|---|---|---|---|
| D1 | `rc.local` governor 冲突 | **沿用上游，不覆盖** | 旧 SELF 是注释 + `exit 0`；上游有 `intel_pstate` 存在性守卫，Hyper-V 自动跳过。该文件是 YAOF 分支静态 overlay，不随点版本变化（当前 sha256 `288c55f4…d34a` 与 10-29 快照一致） |
| D2 | 旧仓 `OpenWrt-Matrix.yml`（引用已删 `SCRIPTS/`） | **弃用旧仓 x86 构建，YunWrt 接管** | 修旧管线要恢复三棵旧树并持续对抗上游 |
| D3 | 版本策略 | **完全不锁**，由 YAOF 分支 + `resolve_version()` 决定，升到 25.13 照走 | 整包出镜像，包与内核同源；vermagic 只在"向运行中设备单独装 kmod"时才构成约束，本项目不做 |
| D4 | 两个必留补丁放哪层 | **patch 层，产物是 overlay 文件**，带 base 哈希与断言 | ImageBuilder 无源码树；base 校验能在上游变形时立即失败 |
| D5 | Web 栈 | **保留 nginx + uwsgi** | 与设备一致；调优改为 overlay 配置 |
| D6 | 代理栈 | **首版 OpenClash（自有 feed 编译）；`nikki` 仅作回退** | 设备实测 OpenClash 在跑（`luci-app-openclash 0.47.156`、`clash` 进程、`/etc/config/openclash`）。OpenWrt-Add 把它放在 `legacy/` 不发布，但 `vernesong/OpenClash` 的 `PKGARCH:=all`，依赖在官方 25.12.6 全齐，可自建 feed 编译 |
| D7 | WLAN seed 120 包 | **x86 不要**，留 `disabled/wlan.extra` | x86 用不到 |
| D8 | feed 与版本固定 | **不做镜像、不做哈希门禁**；只在 build-log 留实际版本、内核、包清单、feed 索引哈希作为排障线索 | 固定解决不了"覆盖静默失效"这个真问题，反而增加日常维护；线索够用即可 |
| D9 | base 校验失败时的策略 | **默认硬失败**；`--allow-base-drift` 仅限手动排障，产物不得发布 | 静默失效比构建失败代价高得多 |

> D1–D9 在 P0 落成 9 份短 ADR；D3/D8 的 ADR 必须写明"为什么不做锁定"，与 0.3 的反向约束互相对应。

---

## 7. 能力清单（代替功能对照矩阵）

### 7.1 字段

| 字段 | 说明 |
|---|---|
| 能力域 | Web / 代理 / DNS / 转发与加速 / UPnP / VPN / 网络初始化 / 性能 / 存储 / 运维工具 |
| 我在用的能力 | 描述功能，可附当前实现作为参考，但**不作为判定依据** |
| 首版 | `要有` / `用替代` / `不要` |
| 实现方式 | 包 / overlay / patch / 自研 feed / 替代品名 |
| 可执行验收 | QEMU 里能跑的命令与期望输出 |
| 结果 | 最近一次能力验收的 pass/fail |

判定只看一件事：**这个能力在首版镜像里能不能用**。不要求包名相同、不要求配置项逐项对齐、不要求与旧镜像包集合一致。

### 7.2 七项能力域的首版判定

| 能力域 | 首版 | 实现方式 | 验收 |
|---|---|---|---|
| Web 管理界面（含反代与上传大文件） | 要有 | nginx + uwsgi + LuCI | `/cgi-bin/luci` 200；80 端口仅 nginx |
| 自研 LuCI（nginx-manager / upnp-nat-relay / cloudflare-ip） | 三个都要有；**三个都必须先发布进自有 feed**，当前只有 `upnp-nat-relay` 已发布；`cloudflare-ip` 没有等价替代 | 自研 feed | 菜单可见；`apk info -e` 成功 |
| 分流去重与域名防护（mosdns + flock；banip 默认不要） | 要有（mosdns + flock）；`banip` **设备未装，按 §11 默认放弃** | 包 + overlay 配置 | 进程 running；规则加载成功 |
| 透明代理（OpenClash / v2ray 数据） | **要有**（用户明确要求保全）；OpenClash 由自有 feed 从 `vernesong/OpenClash` 源码编译，`nikki` / `homeproxy` 仅作备选 | 自有 feed + OpenWrt-Add `apps/` | 能起隧道、订阅可更新、`pgrep -f clash` 有进程 |
| 转发加速（SFE / fast-classifier / FullCone） | 用替代（FlowCone + `kmod-nft-offload` + BBR，**定案**） | 上游 feed | 审计 4.4 五条功能断言全过 |
| WireGuard | 要有 | 包 | 隧道握手成功 |
| 默认 IP / WAN 初始化 / Hyper-V 调优 | 要有 | overlay 重写 | P4 验收 ①–⑧ |

### 7.3 规模

7 个能力域 + 125 个包归纳出的附加域（DDNS、存储、基础工具等），约 12–16 行。每行一条可执行验收。

### 7.4 默认采纳的决策（可推翻，不再阻塞）

完整论证见保全审计 §11。**本轮起按默认值执行，不等待答复**；推翻只改结论、在对应阶段返工。

| # | 项 | 默认结论 | 推翻的代价 |
|---|---|---|---|
| 1 | SFE 是否 1:1 保留 | **FlowCone 替代（定案）**：SFE 与 fast-classifier 不保留，不再评估 | 不适用（无兜底路径） |
| 2 | `banip` 三件套 | **放弃**；确需 IP 黑名单再单独立项 | +0.3 人日 |
| 3 | `usbmuxd` | **放弃**（从未运行，无 USB 透传证据） | 0 |
| 4 | `luci-app-bandix` | **保留包、默认不启用** | 0 |
| 5 | `upnp-nat-relay` 修到运行态 | **修**，列入 P4 阻塞验收 ⑦ | 已在 P4 内 |

### 7.5 首版范围冻结线

**首版范围 = 保全审计 §10.3 的 114 个保留包 + 3 个替换项对应的替换物**（`flowcone` + `kmod-nft-offload`、OpenWrt-Add `nat6`）。除此之外一律不进首版：3 个放弃项、5 个默认关闭项、§2.2 的 3 个残留、以及任何 125 之外的新包。

以后新增包按审计 §10.4 的六条规则判断（有对应能力或缺陷、官方/自有 feed 可装、不引内核补丁或非官方 kmod、不与既有包双栈、体积与首启增量在预算内、走 issue→能力清单→评审→`*.extra` 流程）。默认拒绝，理由留在 issue。

---

## 8. 能力来源与验证路径（2026-11-11 实测）

> 核实方法：官方 25.12.6 的 `downloads.openwrt.org` 目录清单、`openwrt/firewall4` 在发布 commit `b6e51575` 的源码、`openwrt/packages` 的 25.12 分支、`QiuSimons/OpenWrt-Add` 的仓库树（`apps/` 42 个、`themes/` 8 个、`legacy/` 14 个、`invasive/` 1 个）、`hello-yunshu` 三个自研 app 仓库、`yun-openwrt-packages` 已发布产物。

### 8.1 逐项来源表

| 能力 | 设备上的实际形态 | 来源层 | 上游升级时的失效可能性 | 检测方式 | 核实 |
|---|---|---|---|---|---|
| 防火墙 flow offload | `firewall4`；`/usr/share/ucode/fw4.uc`；`/etc/config/firewall` | 官方 feed 包 + **patch 改行为** | **中**：`parse_zone()` 的 `related_physdevs` 段结构变化 | base sha256 `148ded17…`（commit `b6e51575`）+ 断言 `related_physdevs`；`fw4 check` | 已核实 |
| UPnP / NAT-PMP | `miniupnpd-nftables`；`/etc/config/upnpd`；进程 `miniupnpd`；端口 5000 | 官方 feed 包 + **patch 改行为** | **中**：init 脚本结构变化，或改为新的 procd 写法 | base sha256 `cc432b42…` + `grep ext_allow_private_ipv4`；`uci get upnpd.@upnpd[0]` | 已核实 |
| banip | **设备未安装**（seed 里有 `banip` + `luci-app-banip` + `luci-i18n-banip-zh-cn`，设备无包、无 `/etc/config/banip`、无进程） | 官方 feed 包 | 低（只是版本号变） | `apk info -e banip luci-app-banip`；`banip status` | **实测：不在用**；是否恢复需用户确认 |
| mosdns | `mosdns` + `luci-app-mosdns` + `v2ray-geoip/geosite`；`/etc/config/mosdns`；`127.0.0.1:953`；API 9091 | **OpenWrt-Add 预编译**（`apps/luci-app-mosdns/mosdns`） | **中**：官方 feed 无此包，仅靠 OpenWrt-Add；若移入 `legacy/` 立即消失 | `apk info -e mosdns luci-app-mosdns`；`ss -lnup \| grep :953` | 已核实（官方仅 `smartdns`，无 mosdns） |
| flock | `flock` 二进制，被 mosdns hotplug 调用 | 官方 base feed | 极低 | `command -v flock`；hotplug 脚本可执行 | 已核实（flock-2.41.5-r1） |
| WireGuard | `kmod-wireguard` + `wireguard-tools` + `luci-proto-wireguard`；`wg0` 监听 8978/udp、`10.8.0.1/24`；**设备未装 `luci-app-wireguard`** | 官方 feed（kmod 在 target `kmods/`，用户态在 base/luci） | 低；`luci-app-wireguard` 上游已废弃、设备也没装，不需要 | `apk info -e kmod-wireguard wireguard-tools luci-proto-wireguard`；`wg show` | 已核实 |
| nginx + uwsgi 栈 | `luci-nginx` + `luci-app-nginx-manager`；80/443；`/etc/nginx/`、`/etc/uwsgi` | 官方 feed（`luci-nginx`、`nginx-full`、`uwsgi`、`uwsgi-cgi-plugin` 均在）+ 自有 feed | **中**：LuCI 若从 uwsgi 迁走，`uwsgi_modifier1 9` 那套配置会失效 | `pgrep uhttpd` 为空；80 仅 nginx；`/cgi-bin/luci` 200；`apk info -e luci-nginx uwsgi` | 已核实 |
| luci-app-nginx-manager | 自研 LuCI，管理 nginx 站点/反代 | **自有 feed（未发布）** | **高**：依赖 `luci-nginx`/uwsgi 路径；当前 feed 里没有它 | `apk info -e luci-app-nginx-manager`；菜单节点存在 | 源码已核实；**发布未核实** |
| luci-app-upnp-nat-relay | 自研 LuCI + `/etc/config/upnp-nat-relay` | **自有 feed（已发布 1.3.13-r4）** | 中：依赖 miniupnpd 的 init/ubus 接口 | `apk info -e`；`/etc/init.d/upnp-nat-relay status` | 已核实 |
| luci-app-cloudflare-ip | 自研 LuCI，把 Cloudflare 段写入 nft/dnsmasq | **自有 feed（未发布）** | **高**：依赖 Cloudflare API 与 nft 语法；仓库体积 110 MB 需先清理 | `apk info -e`；`nft list set` 有数据；菜单存在 | 源码已核实；**发布未核实** |
| homeproxy | 设备未用，作为 OpenClash 替代 | OpenWrt-Add `apps/homeproxy` | 低 | `apk info -e luci-app-homeproxy`；隧道可起 | 目录已核实 |
| OpenClash | `luci-app-openclash 0.47.156`（旧树自编）+ `/etc/config/openclash` + `clash` 进程 + 653/890-895/9090 + 2 条订阅 | **自有 feed 编译**（`vernesong/OpenClash`，`PKGARCH:=all`；OpenWrt-Add `legacy/` 不发布） | 中：跟随上游 tag；`luci-compat` 未来可能移除 | `apk info -e luci-app-openclash`；`pgrep -f clash`；`/etc/init.d/openclash status` | 已核实可编译 |
| v2ray-geoip / v2ray-geosite | GeoIP/GeoSite 数据，被 mosdns 使用 | OpenWrt-Add `apps/v2ray-geodata`（Makefile 定义这两个包） | 低 | `apk info -e v2ray-geoip v2ray-geosite` | 已核实 |
| FullCone / 转发加速 | 设备用 `kmod-shortcut-fe-cm` + `kmod-nft-fullcone`（内核被 `952/953` 补丁改过，`DISTRIB_TAINTS=no-all override`） | **定案替代**：OpenWrt-Add `apps/FlowCone`（eBPF）+ 官方 `kmod-nft-offload` | **中**：FlowCone 依赖内核 BTF（`vmlinux-btf`）；只随 OpenWrt-Add `apps/` 发布，移入 `legacy/` 即消失 | `apk info -e flowcone`；`/sys/kernel/btf/vmlinux` 存在；`nft list ruleset \| grep flowtable`；`dmesg` 无报错 | 目录已核实；功能断言见审计 4.4 |
| TCP 拥塞控制 | `kmod-tcp-bbr` | 官方 target `kmods/` | 高（kmod 与内核强绑定，但同 release 自动匹配） | `modinfo` vermagic；`sysctl net.ipv4.tcp_congestion_control` | 已核实（`kmod-tcp-bbr-6.12.112-r1`） |
| 主题与监控 | `luci-theme-aurora`、`luci-app-aurora-config`、`luci-app-bandix`、`luci-app-ramfree` | OpenWrt-Add `themes/`、`apps/` | 低 | `apk info -e` + 菜单可见 | 目录已核实 |
| 默认 IP / WAN 初始化 / Hyper-V 调优 | `99-x86-vm-network`、`90-swrt-x86-network.conf`、`swrt-vm-performance` | **overlay 文件（重写）** | 中：依赖 `network` 默认结构、`uci-defaults` 时机、`hv_netvsc` 驱动名 | P4 验收 ①–⑧ | 旧文件已核实；新写法未核实 |

### 8.2 最可能真的丢掉或坏掉的五项

| # | 项 | 为什么 | 计划里哪一步先发现 |
|---|---|---|---|
| 1 | **SFE + fullconenat（`kmod-shortcut-fe-cm` / `kmod-nft-fullcone`）** | 设备内核被 `952/953` 补丁改过，带 4 个非官方 build origin 的 kmod、`DISTRIB_TAINTS=no-all override`；ImageBuilder 无内核源码树，无法重编同款。**SFE 与 fast-classifier 不保留，由 FlowCone + `kmod-nft-offload` 替代（交付决定，审计 4.3）** | P4 阻塞验收 ⑧：审计 4.4 五条功能断言 |
| 2 | **OpenClash** | OpenWrt-Add 把它放在 `legacy/` 不发布，**feed 里确定没有**；但源码可编译（`PKGARCH:=all`、依赖在官方 25.12.6 全齐） | P3b G3b：`apk info -e` 或起核失败 |
| 3 | **`luci-app-nginx-manager` / `luci-app-cloudflare-ip`** | 源码在 `hello-yunshu` 下，但 `yun-openwrt-packages` **只发布了 upnp-nat-relay**；没发布等于首版没有这两个 app | P3 `apk info -e` 失败（保全清单已列） |
| 4 | **`fw4.uc` 的 flow-offload 补丁** | 实测 25.12.6 所用 commit `b6e51575` 的 `parse_zone()` 仍只推 `net.physdev`，**补丁仍然必需**；上游 master 已加 `net.device` 分支，说明这块在演进 | P4 `verify-bases.sh`：base 哈希对不上即失败 |
| 5 | **`miniupnpd` 的 force_forwarding 补丁** | 实测 25.12 分支的 `miniupnpd.init` 里 `ext_allow_private_ipv4` 出现 0 次，**补丁仍然必需**；设备实测 `miniupnpd` 根本没装，`upnp_nat_relay` 只在 enabled 状态 | P4 `verify-bases.sh` + 阻塞验收 ⑦（默认修到 running） |

补充三个"看起来能用、实际要注意"的项：

- **`banip` 在 seed 里、设备上没装**，不是"在跑的能力"；按 §11 默认**放弃**，不写进首版包清单。
- **`upnp_nat_relay` enabled 但没运行、`miniupnpd` 未装**：这是**当前就坏的服务**；按 §11 默认**修到 running**，列入 P4 阻塞验收 ⑨。
- `mosdns`、`homeproxy`、`FlowCone` 都只存在于 OpenWrt-Add 的 `apps/`；这几个能力完全依赖上游继续把它们留在 `apps/`，一旦移入 `legacy/` 就同时消失。

### 8.3 一句话结论

**能保留，但前提是五条同时成立：**

1. 三个自研 app 必须先变成自有 feed 里**可安装的 APK**——现在只有 `upnp-nat-relay` 发布了；
2. **OpenClash 由自有 feed 从 `vernesong/OpenClash` 编译并签名**（P3b），不能指望 OpenWrt-Add 的 `legacy/`；
3. 两个必留补丁必须随每次 release 重新校验 base（已确认 25.12.6 仍然需要它们）；
4. 只在 OpenWrt-Add `apps/` 里的能力（mosdns、homeproxy、FlowCone、v2ray-geodata、aurora、bandix 等）依赖上游不把它们移走；
5. 每次自动构建都要跑能力验收——否则"镜像构建成功"没有任何意义。

**无法保留、只能替代的项**：SFE/`kmod-shortcut-fe-cm`、`kmod-fast-classifier` 与 fullconenat/`kmod-nft-fullcone` → **FlowCone（eBPF）+ `kmod-nft-offload`，定案**（审计 4.3），不再评估其他路径。`luci-app-wireguard` 不需要（设备未装、上游已废弃）。`luci-app-cloudflare-ip` 没有等价替代（`ddns-scripts-cloudflare` 只更新 DNS 记录，不负责把 Cloudflare 段写进 nft），必须发布自有 feed 才能保住。

## 9. 自动升级流水线

### 9.1 触发

| 触发 | 时机 | 行为 |
|---|---|---|
| `schedule` | 每日一次（可调 6 小时） | 拉上游 → 无变更则结束；有变更则继续构建 |
| `push`（`upstream/yaof/**`、`overlay/**`、`patches/**`、`scripts/**`） | 同步 PR 合并或自研改动 | 直接构建 |
| `workflow_dispatch` | 手动 | 可选择"强制全量"或"仅验收" |
| `repository_dispatch` | 外部触发（未来接 upstream webhook） | 同上 |

**跳过逻辑是硬要求**：上游无变更时不允许跑构建，否则每天烧一小时 CI 只为证明"什么都没变"。

### 9.2 步骤

```text
sync（有变更？）──否──> 结束
      │是
      ├─ check-upstream
      ├─ verify-bases        ← 失败即红，不进入构建
      ├─ check-packages
      ├─ assemble + build
      ├─ qemu-capability     ← 上行构建成功但这里失败，同样算失败
      ├─ 写 build-log + 上传报告
      └─ 通过？──是──> release
                └──否──> 开/更新 Issue + 保留产物 + 不发布
```

### 9.3 失败可见（四条出口，缺一不可）

1. **Job Summary**：一张表，逐项列能力断言、实际值、期望值、pass/fail；失败项置顶。
2. **自动 Issue**：标题固定 `[auto] YunWrt 构建/能力回归`，正文带上游 commit、版本、失败项、产物链接；用固定 label 去重，后续失败追加评论而不是重复开单。
3. **不发布**：`release.yml` 的前置条件是该 commit 的能力验收 pass；失败时 Release 步骤被跳过，产物只作为 workflow artifact 保留 7 天。
4. **README 徽章**：最近一次 `sync-and-build` 的状态，谁看仓库都能一眼看到红绿。

### 9.4 与"构建成功"的区分

流水线的成功定义是**能力验收通过**，不是 `make image` 退出 0。构建成功但能力失败，整个 job 必须红——这条要写进 workflow 的实现里，不能靠人看日志。

---

## 10. 主要风险

| 风险 | 触发点 | 缓解 |
|---|---|---|
| 上游改 `build.sh` 参数/Usage | 同步 PR | `check-upstream.sh` 硬失败 |
| 上游删除被覆盖的路径 | `verify-bases.sh` | 硬失败，提示"目标消失" |
| `fw4.uc` / `miniupnpd.init` 结构变化 | base 哈希 + 断言 | 硬失败，重做覆盖文件 |
| 上游新增文件与我们的新增撞名 | `kind: add` 检查 | 告警 + need-review |
| 上游发新点版本导致行为变化 | 每日构建 | 能力验收兜底；失败即红 |
| OpenWrt-Add 停止发布某包 | `check-packages.sh` | 硬失败，改判定或找替代 |
| OpenWrt-Add 公钥轮换 | 构建失败（签名校验不过） | 人工确认后更新，不做自动信任 |
| 能力验收覆盖不全 | 人工判断 | 每行必须有可执行断言；G4 抽查现场复跑 |
| `files/` 共享目录污染未来 rockchip | 回加 rockchip | `uname -m`/DMI 门禁 |

---

## 11. 附：本方案不做什么

- 不 fork / 不 clone `QiuSimons/OpenWrt-Add` 源码树，只消费其签名 feed。
- 不恢复 `SCRIPTS/`、`SEED/`、`PATCH/` 旧结构。
- 不锁 OpenWrt 版本、不锁 feed 哈希、不做包清单基线、不做快照复现（见 0.3）。
- 不在 ImageBuilder 里做 Kconfig 级裁剪。
- 不把 78 个旧补丁整体移植，只保留 D4 的两个。
- 不评估 SFE / fast-classifier 的 1:1 保留，不做性能验证，不做源码构建 —— FlowCone 是定案（审计 4.3）。
- 不在本轮创建仓库、推送或修改现有 SWrt。
