# SWrt x86 在线升级支持计划

## 当前状态

- 目标设备运行 SWrt 定制的 OpenWrt `25.12.5` x86/64 固件。
- 设备上的 `owut` 默认请求 `https://sysupgrade.openwrt.org`。该 ASU 使用官方 OpenWrt 构建输入，无法自动重现本项目的完整源码构建、内核补丁和定制包选择。
- 当前检查报告 25 个包在目标构建中缺失、3 个包将降级、2 个默认包缺失。缺失项包括本项目/第三方 LuCI 应用、定制内核模块以及自定义数据包。此状态下不应通过官方 ASU 强制升级。
- `SELF/02_prepare_package_self.sh` 当前从 LuCI nginx 集合中移除 `luci-app-attendedsysupgrade`。
- `.github/workflows/X86-OpenWrt_self.yml` 调用完整源码构建流程；x86 产物包括 EFI ext4 镜像、VHDX 和 rootfs。Nightly Actions artifact 保留 5 天，release 模式发布固件 ZIP。

## 目标

先为 x86 固件建立可审计、可恢复的版本化升级流程；完成构建和兼容性验证后，再决定是否开放 LuCI/`owut` 一键升级。

## 推荐实施路线

### 阶段 1：升级输入与产物清单

- 每个可升级构建生成机器可读 manifest，记录 SWrt 版本、OpenWrt commit、feeds/自定义包 revision、目标设备、文件系统、内核版本及 ABI、配置摘要、包名和包版本。
- 将 manifest 与固件、SHA-256 校验文件一同发布；区分 nightly 与稳定 release channel，给稳定版使用不可变 tag 和来源锁定。
- 产物清单要能区分完整安装镜像、供当前设备升级的镜像以及仅供救援/安装使用的镜像，避免把 VHDX 或初始安装镜像误当成通用 sysupgrade 文件。
- 将稳定版本产物保存到长期发布位置；不能依赖仅保留 5 天的 Actions artifact 作为升级源。

### 阶段 2：升级前检查与恢复说明

- 编写与版本 manifest 对照的只读检查，确认设备型号/target/profile、当前 SWrt channel、来源版本、目标版本、文件系统和必要的升级路径。
- 检查目标包集合完整性；对本固件选择的定制包、替代默认包和禁用默认包维护明确预期，避免把已知定制误报成未知差异。
- 升级说明要求先生成并下载配置备份，记录当前固件版本与 manifest，说明断电/升级失败时使用本地控制台或虚拟机挂载恢复镜像的办法。
- 发现 target/profile 不符、关键包缺失、内核 ABI 不一致、签名/摘要失败或不支持的跨版本路径时必须停止，不提供忽略检查的强制选项。

### 阶段 3：SWrt 构建与升级服务

- 先评估并原型验证独立 SWrt ASU 服务：worker 必须使用对应的 SWrt 源码、`SELF/X86/config_self.seed`、kernel patches、feeds 和 source lock 生成镜像，而不是只在官方 ImageBuilder 上追加 package feed。
- 维护并发布与固件完全同构的签名 package feeds；内核模块必须来自同一次匹配构建，并通过 kernel ABI/vermagic 校验。禁止让设备从官方仓库混装不匹配的 kmod。
- 评估服务端构建负载、缓存、存储、镜像保留、签名密钥管理、访问控制及维护责任。先做离线/内网原型，暂不替换设备上的官方 ASU URL。
- 如果 ASU 无法可靠表达本项目的完整源码构建，保留完整镜像发布流程，另行设计 SWrt updater/manifest 服务；不要为了兼容 `owut` 而削减定制固件内容。

### 阶段 4：客户端接入与分阶段开放

- 保持 LuCI attended-sysupgrade 默认关闭，直到设备能够选择并验证 SWrt channel，并且更新请求确实落到 SWrt 构建服务。
- 接入时显示当前/目标版本、变更包、升级风险和备份状态；下载后验证来源签名与 SHA-256，再交给升级程序。
- 首先只支持 x86/64 本项目的 `generic`/EFI ext4 目标。其他 SWrt target 需各自完成 profile、产物类型、升级路径和恢复测试后再开启。
- 维持手动下载固件作为可用后备路径；逐步对测试虚拟机、备用设备和主路由灰度验证。

## 发布门槛

- 从 manifest 可重现目标构建输入，且构建结果包含预期定制包和正确的内核模块 ABI。
- 全新安装及从至少一个受支持旧版升级均能正常启动；需要保留的配置经过迁移验证。
- 完成配置备份与恢复演练、下载损坏/签名错误拦截、构建失败和服务不可用测试。
- 目标镜像不可用、设备型号不符、包缺失或 ABI 不一致时客户端 fail closed，不能继续刷写。
- 设备侧 `owut check`/升级预览针对 SWrt 自有 endpoint 运行；记录报告结果和本地构建 SHA，官方 ASU 输出不作为 SWrt 升级通过证据。

## 暂不纳入

- 不使用 `owut --force` 绕过当前的官方 ASU 检查。
- 不把 `apk upgrade` 或直接从官方 feeds 更新当前整套包作为固件升级方案。
- 不在 SWrt 升级服务和恢复流程通过前默认启用 LuCI 一键升级。
- 第一阶段不扩展到 R2S/R2C 等其他 target。

## 完成定义

用户能从受支持的 SWrt 稳定版本检查并取得匹配的 SWrt x86 更新；升级前可验证目标和包集合，升级镜像经过签名/摘要校验，配置可备份和恢复，设备不依赖官方 ASU 生成定制镜像。
