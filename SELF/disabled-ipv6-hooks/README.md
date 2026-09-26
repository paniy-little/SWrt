# 已停用的 IPv6 脚本

此目录仅存档历史源码，不属于 `files` overlay，不编入固件：

- `99-zzz-odhcpd.bak`：曾在 WAN6 事件后反复重启 odhcpd、删除 DHCP 主机记录并修改 RA。
- `99-yunshu-disable-ipv6-pd`：曾在首次启动/升级时强制关闭 PD 请求并删除 LAN 前缀分配配置。

OpenWrt 会 source `hotplug.d` 中的普通文件；改 `.bak` 后缀或去掉可执行位不能禁用它。
这两份源码仅用于回溯，不应复制回自动执行目录。

同步工作流保留相应排除规则，并在 rsync 后清理目标分支历史副本；`SELF/` 和 `.github/`
均不被上游同步覆盖。SELF/X86 在合并 overlay 后再次移除旧入口，升级迁移会备份并移走
恢复到 `/etc` 的旧入口。停用只改变脚本执行策略，不追溯修改已有 UCI 网络配置。
