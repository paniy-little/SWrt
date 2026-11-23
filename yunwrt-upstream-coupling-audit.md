# YunWrt 对上游 YAOF / OpenWrt ImageBuilder 的耦合审计

范围：`/Users/yunshu/Documents/GitHub/YunWrt`（main @ `fc9d141`）
方法：只读代码与文件；「实测」= 能在仓库里直接验证的事实，「推导」= 需要真跑一次构建才能确认。
本文件只做分析，未改任何构建逻辑，未提交代码。

---

## 0. 一个前提：CI 根本不使用仓库里钉住的上游

`.github/workflows/build.yml:48-49` 在每次构建前执行 `scripts/sync-upstream.sh`，而它（`scripts/sync-upstream.sh:8-9,17`）是 `--depth 1 --branch 25.12` 拉分支 HEAD。

- 后果：**仓库里 `upstream/yaof/` 的“固定版本”只对本地构建有意义**；CI 的 nightly（`build.yml:15-21` 的 schedule / push）每次都拿上游当天 HEAD。
- 实测：`upstream/yaof.commit` 在 CI 里会被 `sync-upstream.sh:28` 覆写后再被 `build.sh:7` 读进 `build-log`，所以构建日志记录的是本次真实 SHA——但没有“预期 SHA”可与它比对。
- 结论：上游一改，下一次 nightly 就会撞上；所有下面的断言，实际是在 CI 的 2 小时窗口里替我们兜底。

---

## 一、隐含假设清单

### A. 目录形态

| # | 假设 | 位置 | 不满足时 | 现有断言 |
|---|---|---|---|---|
| A1 | 上游有 `scripts/build.sh`、`profiles/packages.common`、`profiles/packages.x86_64`、`files/` | `scripts/sync-upstream.sh:20-22` | 同步阶段硬失败 | 有，同步时 `die` |
| A2 | `upstream/yaof/scripts` 存在 | `scripts/assemble.sh:10` | 硬失败 | 有 |
| A3 | 组装后必须有 `scripts/build.sh`、两份 `packages.*`、`files/etc/rc.local` | `scripts/assemble.sh:66-68` | 硬失败 | 有 |
| A4 | `files/etc/rc.local` 是上游固定文件 | 同上 | 上游删掉/改名 → 硬失败（误报） | 有（但断言的是上游文件，属于误报源） |
| A5 | `check-upstream.sh` 还假设 `files/etc/hotplug.d/net/01-maximize_nic_rx_tx_buffers`、`files/etc/sysctl.d/99-zram.conf` | `scripts/check-upstream.sh:14-18` | 硬失败 | 有（误报源） |
| A6 | 上游根目录名就是 `yaof`，且不带 `.git` | `sync-upstream.sh:27` | 带 `.git` 也不影响构建；无断言 | 无（低危） |

### B. 接口契约（`check-upstream.sh` 覆盖到什么程度）

`scripts/check-upstream.sh:25-39` 用字面量 grep 校验 11 项：

| 校验项 | 行 | 实际意义 |
|---|---|---|
| `Usage: $0 <TARGET> <SUBTARGET> <PROFILE> <PACKAGES_PROFILE> [VERSION]` | L27 | 4+1 位置参数 |
| `TARGET/SUBTARGET/PROFILE/PACKAGES_PROFILE/VERSION="${N:-}"` | L28-32 | 参数顺序语义 |
| `resolve_version()` | L33 | 版本解析函数仍存在 |
| `ARCH="x86_64"` | L34 | x86 的 feed 架构名 |
| `profiles/packages.common` / `profiles/${PACKAGES_PROFILE}` | L35-36 | 清单加载路径模板 |
| `FILES="$OVERLAY_DIR"` | L37 | overlay 注入方式 |
| `ROOTFS_PARTSIZE=` | L38 | make 变量 |
| `*combined-efi.img.gz` | L39 | 产物匹配串 |

**没覆盖到的（= 只靠运行时行为兜底）**：

- `load_packages()` 的实现与格式（`upstream/yaof/scripts/build.sh:131-135`：按行、`#` 注释、空格拼接）
- `make image` 的实参集合（上游 `:144-148`）、产物目录 `bin/targets/<target>/<subtarget>`（上游 `:191-197`）
- 产物命名模板 `YAOF-${PLATFORM_LABEL}-${VERSION}-${DATE}-${suffix}`（上游 `:199-212`）
- 日志文案：`Using OpenWrt release: `（上游 `:66`）被 `scripts/build.sh:177-178` 解析；`skipping: `（上游 `:186`）被 `scripts/build.sh:171-172` 解析
- 版本线变量写法 `local default_major="25.12"`（上游 `:29`）被 `scripts/build.sh:32-33` 用 sed 抠

其中「日志文案」两处是**字符串级契约**：上游改一句提示语，我们的解析就空转。版本那处有空值硬失败（`build.sh:34`、`:178`）；`skipping:` 那处没有——见 D3。

### C. 清单机制

| # | 假设 | 位置 | 不满足时 | 断言 |
|---|---|---|---|---|
| C1 | 包清单是「一行一个包、`#` 注释、空行忽略」的纯文本 | 上游 `build.sh:131-135`；`scripts/assemble.sh:46-52` | 上游换成 JSON/YAML/其他加载器 → `build/` 里多出的行不被读取 | 间接有：`build.sh:192-208` 会用 `build/profiles/*` 反查 manifest |
| C2 | 我们的 `.extra` 用 `cat >>` 追加到 `packages.common` / `packages.x86_64` 是有效注入 | `assemble.sh:47-51` | 上游改读别的文件名（旧文件留作装饰）→ 追加看似成功、实际未生效 | 间接有（C1 的 manifest 反查），失败形态是**构建末尾**硬失败 |
| C3 | `-pkg` 前缀表示“从默认集中移除” | 上游 `profiles/packages.common:71,73-74`；`overlay/packages/common.extra:101`（`-usbmuxd`）；`build.sh:195-198` | 上游/IB 不再识别 `-` 语法 → 该包**静默留在镜像里** | **无**：`build.sh:197-199` 只是把 `-xxx` 从 EXPECTED 里剔除，从不校验 manifest 中确实没有它 |
| C4 | `PACKAGES_PROFILE` 传 `packages.x86_64`（带 `packages.` 前缀）仍能拼出正确路径 | `build.sh:165` → 上游 `:137` | 上游改成 `${PACKAGES_PROFILE}.list` 之类 → 文件不存在，上游 `die` | 有（硬失败） |
| C5 | manifest 行格式是 `name - version` | `build.sh:203-206`；`check-openclash-watchdog.sh:43` | 格式变 → 所有包被判定缺失，硬失败（误报） | 有（硬失败） |
| C6 | `libcurl` 在 manifest 里可能叫 `libcurl4` | `build.sh:203` | 特例失效 → 误报硬失败 | 有 |

### D. 构建入口

| # | 假设 | 位置 | 不满足时 | 断言 |
|---|---|---|---|---|
| D1 | 固定四参数调用 `x86 64 generic packages.x86_64`，不传版本 | `scripts/build.sh:165`；上游 `:13-16` | 上游新增/重排位置参数 → 可能参数错位（把 profile 当包清单）；多数情况会因文件不存在硬失败 | 部分（`check-upstream.sh:27-32` 只查字面量，不查语义） |
| D2 | ImageBuilder profile 名仍是 `generic` | `build.sh:165`；上游 `:199`（`generic` → `x86-64` 命名映射） | IB 删掉该 profile → `make image` 硬失败 | 上游自身会失败 |
| D3 | 缺包只打印 `WARNING ... skipping: <pkgs>`，不直接失败 | `build.sh:170-172`（解析该文案把 WARNING 升级为硬失败） | 文案变化 → 解析为空 → **跳过检查**；若缺的是 IB profile 默认包（不在我们清单里），manifest 反查也查不到 | **无**（仅覆盖我们列出的包） |
| D4 | `ROOTFS_PARTSIZE=1024` 这个 make 变量名可用 | 上游 `:144-148` | 变量改名 → 分区大小回退默认，`build.sh` 无回查 | **无**（`check-upstream.sh:38` 只查字面量存在） |
| D5 | 版本行 `Using OpenWrt release: X` 与预置 IB 的版本一致 | `build.sh:177-180` | 文案变 → 解析空 → 硬失败（好）；版本不同 → 硬失败（好） | 有 |
| D6 | 解包目录名 `openwrt-imagebuilder-<ver>-x86-64.Linux-x86_64` | `build.sh:42-43` | 上游改 IB 包命名 → 我们预置失败/上游自己下载，feed 注入丢失 | 部分：`build.sh:53,59` 检查 `repositories`/`.config` 存在 |

### E. ImageBuilder 内部（`.config` 与文本级改写）

| # | 假设 | 位置 | 不满足时 | 断言 |
|---|---|---|---|---|
| E1 | IB 包自带 `.config`，可直接改键 | `build.sh:59-68` | 上游不再附带 `.config` → 硬失败（`die`） | 有 |
| E2 | `CONFIG_TARGET_KERNEL_PARTSIZE` / `CONFIG_TARGET_ROOTFS_PARTSIZE` 仍是这两个键名 | `build.sh:69-70` | 键名变 → `set_ib_config` 追加一个无效键、自检仍通过 → **分区回退默认，构建成功，坏镜像** | **无**（只校验自己写入成功） |
| E3 | `CONFIG_GRUB_CONSOLE/TITLE/TIMEOUT/BOOTOPTS/BAUDRATE` 仍是这五个键 | `build.sh:77-81` | 同上 → `title`/`timeout`/`mitigations=off` 静默回退 | **无**（见 E4 的关键漏洞） |
| E4 | 离线渲染门禁能代表真实产物 | `build.sh:130-139` | 门禁用**硬编码**的 `mitigations=off`、`timeout=0` 渲染模板；它验证的是模板文本，**不验证 `.config` 是否被 IB 采纳** | 门禁本身会过，坏镜像照样出 |
| E5 | `make image` 不会用 defconfig/oldconfig 重算 `.config` | 推导（未跑构建验证） | 若会重算 → E2/E3 全部静默失效 | **无** |
| E6 | 上游 Makefile 里那行仍是 `GRUB_CONSOLE_CMDLINE += console=...` | `build.sh:92-95` | 换文件/换写法（如 `:=`、`+=console=`、挪进 include）→ sed 不匹配 → grep 断言失败 → 硬失败（不会出错镜像，但会误报） | 有 |
| E7 | 上游替换串仍是 `s#@CMDLINE@#$(BOOTOPTS) $(GRUB_CONSOLE_CMDLINE)#g` | `build.sh:98-101` | 同上 → 硬失败 | 有 |
| E8 | 两个模板里仍是 `@GPT_ROOTPART@ @CMDLINE@ noinitrd` 这一顺序 | `build.sh:104-110` | 上游调整顺序 → grep 失败 → 硬失败（误报）；上游用等价格式（换行、变量名变体）→ 同样硬失败 | 有 |
| E9 | `grub-pc.cfg` 与 `grub-efi.cfg` 都需要改 | `build.sh:104-111` | 实际只构建 efi 变体（上游 `:201-204`），pc 模板的改动从未被渲染校验 | 无（当前无实际影响） |
| E10 | sed 在 CRLF / 不同缩进下仍匹配 | `build.sh:92,98,105` | 上游换行符或缩进风格变 → 硬失败 | 有（同为误报源） |

**E2/E3/E4 是同一类最危险的组合**：写入失败不会报错，门禁也抓不到，最终是“构建成功但 p1 里 grub 配置回退”的坏镜像。

### F. 产物名与变体选择

| # | 假设 | 位置 | 不满足时 | 断言 |
|---|---|---|---|---|
| F1 | 上游产物以 `YAOF-` 开头 | `build.sh:211-217`（`for f in build/artifact/YAOF-*`） | 前缀变 → 循环空转；后续 `vhdx_src` 为空 → `build.sh:222` 硬失败（信息误导） | 间接有 |
| F2 | x86 产物匹配 `*combined-efi.img.gz` | 上游 `:201-204` | 上游改挑 `combined.img.gz` → 我们拿不到 efi | 间接（F1 的连锁） |
| F3 | 一定有 `*-ext4-combined-efi.img.gz` 用来转 VHDX | `build.sh:221` | ext4 变体改名/取消 → 硬失败 | 有 |
| F4 | `PLATFORM_LABEL` 把 `generic` 映射为 `x86-64` | 上游 `:199` | 只影响文件名 | 无（无害） |

### G. OpenClash 看门狗门禁

| # | 假设 | 位置 | 不满足时 | 断言 |
|---|---|---|---|---|
| G1 | 包把脚本装到 `.../root-x86/usr/share/openclash/openclash_watchdog.sh` | `check-openclash-watchdog.sh:23` | 上游包改路径/文件名 → 硬失败（误报） | 有 |
| G2 | 坏版本特征串是 `instances\.\*\.running` | `:29-33` | 新版重构掉这个串 → 误报通过（但 G3 兜住） | 有 |
| G3 | 好版本必有 `instances.openclash.running` 或 `...respawn.retry` | `:35-39` | 0.48.x 重构 → 硬失败（误报），不会放过坏版本 | 有 |
| G4 | manifest 里有 `luci-app-openclash - <ver>` | `:43-48` | 包改名 → 硬失败 | 有 |

结论：G 组偏“误报”而非“漏报”，方向是对的。

### H. 其他

| # | 假设 | 位置 | 不满足时 | 断言 |
|---|---|---|---|---|
| H1 | patch 基线可从 `PKG_SOURCE_VERSION:=` 解析 | `check-patch-bases.sh:35-43` | Makefile 写法变 → 硬失败 | 有 |
| H2 | patch 源码可从 `<repo>/raw/<sha>/<path>` 取到 | `:45-48` | 仓库迁移/私有化 → 硬失败 | 有 |
| H3 | overlay 每条 `meta.json` 的 `upstream_path` 在上游真实存在（`kind=override`） | `verify-bases.sh:53-60` | 上游删文件 → 硬失败（正确） | 有 |
| H4 | `kind=add` 与上游撞名只是 WARN | `verify-bases.sh:46-51` | 撞名仍会继续构建（need-review） | 有 WARN |
| H5 | feed 地址取 `yun-feed.list` 第一条非注释行 | `build.sh:143-145` | 文件格式变 → 硬失败 | 有 |
| H6 | 自有 feed 必须出现在 IB `repositories` | `build.sh:153-157` | 注入失败 → 硬失败 | 有 |

---

## 二、防护覆盖度

### 已硬失败（相当于“拦住了”）

- 上游目录/关键文件消失：A1-A3、H1-H3
- 参数、版本线、模板文本漂移：`check-upstream.sh:25-39`、`build.sh:34/94/100/109/138/178`
- 我们清单里的包没进镜像：`build.sh:192-208`
- feed 未注入 / 版本不一致：`build.sh:157/179-180`
- 看门狗回退：`check-openclash-watchdog.sh:50-53`
- 产物缺失/命名断裂：`build.sh:222`（信息不准）

代价：**多数是误报型**（上游改格式但语义不变就中断），会表现为“某天 nightly 突然红”，排查成本不低，但不会流出坏镜像。

### 会静默出错（最需要关注）

1. **`-pkg` 减法失效**（C3）：`build.sh:195-198` 只把 `-xxx` 从期望里删掉，从不验证它真的不在 manifest。上游/IB 一旦不认这个语法，被移除的包（`-dnsmasq`、`-luci-app-attendedsysupgrade`、`-usbmuxd`）会**静默留在镜像**，构建全绿。
2. **`.config` 键名漂移**（E2/E3）：`set_ib_config` 的断言是“我写进去了”，不是“IB 认这个键”。键改名 → 追加无效键 → 分区/grub 参数回退默认，`build.sh:130-139` 的渲染门禁用的是硬编码值，**照样通过**。
3. **`.config` 被 oldconfig 重算覆盖**（E5，推导）：同上的静默后果，且当前没有任何回查。
4. **`skipping:` 文案变化**（D3）：缺包检查失效；manifest 反查只覆盖我们清单内的包，IB profile 默认包缺失会漏。
5. **`KERNEL` 解析失败**（`build.sh:189-190`）：只影响 `build-log` 元数据，静默降级为 `unknown`（低危）。

### 完全无防护

1. **`upstream/yaof/` 内容与 `yaof.commit` 无任何哈希校验**（实测：`sync-upstream.sh` 只写 SHA，`check-upstream.sh:41-45` 只打印 SHA；`verify-bases.sh` 校验的是 overlay/patch 的 base，不是上游树）。手动改上游不会在任何门禁上暴露，只有 `git diff` 能看出来。
2. **`.config` 是否在构建后仍生效**（实测无回查）。
3. **产物里的 `grub.cfg` 内容**（实测无回查）：只验证了“模板渲染结果”，没验证“最终 p1 里的文件”。本地一次离线渲染（478 B / `7f0e9799…`）是人工验证，不在流水线里。
4. **CI 的“钉版本”语义**（实测）：`build.yml:48-49` 每次重新同步，`yaof.commit` 不参与任何比对。

---

## 三、加固建议（按性价比排序）

### 1. 构建后回查 `-pkg` 减法（成本：极低，收益：高）
在 `build.sh:192-208` 之后加：对 `REMOVED` 中每个包，断言 manifest 里**不存在** `^pkg - ` 行。
- 拦住：C3 的整类静默错误（例如 dnsmasq 双装、attendedsysupgrade 复活）。
- 误报：几乎为零（只有当上游语义从“移除”变成“忽略”时才触发，而那正是要报警的情况）。

### 2. 构建后从产物回查 grub.cfg / 分区几何（成本：低-中，收益：高）
`build/artifact/YunWrt-*-ext4-combined-efi.img.gz` gunzip 后按字节 grep：
- 必须出现 `set timeout="0"`、`menuentry "yunshu"`、`mitigations=off`、`noinitrd mitigations=off`
- 必须不出现 `console=ttyS`、`console=tty1`

分区几何用 `sfdisk -d`（或解析 GPT）断言 p1=64 MiB、p2=1024 MiB。
- 拦住：E2/E3/E5 全部静默路径——这是当前唯一能证明 `.config` 真被采纳的办法。
- 误报：极低；实现上注意镜像里字符串可能跨 512 B 边界，用 `grep -a` 全文件搜即可。

### 3. 写入 `.config` 前先验证键名在上游存在（成本：低，收益：高）
`set_ib_config` 增加：若键在 `.config` 中不存在，则在 IB 的 `target/linux/x86/image/Config.in`（或 `config/Config-images.in`）里 grep 键名，找不到就 `die`。
- 拦住：E2/E3 的“键改名 → 静默追加无效键”。
- 误报：只在 IB 把选项移出 Config.in 时触发（真变化）。

### 4. 给 `upstream/yaof/` 加内容哈希（成本：低，收益：中）
`sync-upstream.sh` 同步后对目录做 `find … -type f | sort | xargs sha256sum | sha256sum`，与 SHA 一起写入 `upstream/yaof.commit`（两行）；`build.sh` 开头比对，不一致 `die`（或至少 WARN）。
- 拦住：A6/手改上游/半途中断的同步。
- 注意：CI 每次都会重新同步（`build.yml:48`），所以这条只保护本地构建；CI 侧应改为“记录本次 SHA 到 job summary，并对与上次不同的 SHA 发告警”。
- 误报：同步脚本自身若改过滤规则会触发一次，可接受。

### 5. 把文本级改写换成“基线哈希 + 整段替换”（成本：中，收益：中）
现状是 sed 精确串 + grep 断言：安全但误报多。更抗变的两条路：
- **优先**：改用 make 变量（如上游将来提供 `GRUB_CMDLINE_OVERRIDE`），彻底不碰文件；
- **次选**：对 `target/linux/x86/image/Makefile` 与两个模板记录 sha256 基线，变了就停，人工确认后再更新基线——把“静默/误报”变成“显式升级动作”。
- 不要改成 `sed -E` 宽匹配：那会把“上游语义变了”放过成坏镜像。

### 6. 让 `skipping:` 解析有兜底（成本：低，收益：中）
`build.sh:171-172` 在解析为空时，再用 manifest 反查一次“我们清单里的包是否齐全”（已做），并额外比对 IB 的默认包数量/`PACKAGES` 回显；或在日志里搜索 `Cannot install package|no such package` 作为第二判据（上游 `build.sh:153-158` 已有两种格式的正则，可复用）。
- 拦住：D3 的漏检。
- 误报：低。

### 7. 收敛“误报源”（成本：低，收益：中）
- `assemble.sh:66` 断言上游 `files/etc/rc.local` 存在，属于上游文件，建议改为“存在则用、不存在不拦”（我们真正需要的是 `scripts/build.sh` 和两份 profiles）。
- `check-upstream.sh:14-18` 同理：`files/etc/hotplug.d/...`、`files/etc/sysctl.d/...` 是上游内容，删了不该中断构建。

### 8. 记录并比对 nightly 的上游 SHA（成本：低，收益：中）
在 `build-log/*.json` 里已有 `yaof_commit`（`build.sh:238`）。加一步：workflow 里把它与上一次成功的 run 比较，不同则在 job summary 里显著标注，并附上本次 diff 的文件清单（`git diff` 需要保留浅克隆，成本略增）。

---

## 四、一句话结论

- 当前防护的形态是：**“上游结构/文本一变就硬失败”**，所以“上游改了东西导致构建流程坏掉”这件事，大概率表现为 **nightly 红**，而不是静静出坏镜像——除了三处例外：**`-pkg` 减法、`.config` 键名/生效、`skipping:` 文案**。
- 最该补的三件事，按顺序：**产物侧回查 grub/分区 → 回查 `-pkg` 是否真的被移除 → `set_ib_config` 前验证键名**。这三条都能在 CI 里跑，成本都低于一次排障。
- `upstream/yaof` 与 `yaof.commit` 的哈希校验属于“卫生”而非“保命”，但在 CI 每次重新同步的现状下，它的价值主要在本地可复现性，不在防漂移。
