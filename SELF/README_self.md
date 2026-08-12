<p align="center">
<img width="768" src="https://raw.githubusercontent.com/QiuSimons/Others/master/YAOF.png" >
</p>
<p align="center">
<img src="https://forthebadge.com/images/badges/built-with-love.svg">
<p>
<p align="center">
<img alt="GitHub All Releases" src="https://img.shields.io/github/downloads/paniy-little/R2S-R4S-X86-OpenWrt/total?style=for-the-badge">
<img alt="GitHub" src="https://img.shields.io/github/license/paniy-little/R2S-R4S-X86-OpenWrt?style=for-the-badge">
<p>
<p align="center">
<img src="https://github.com/paniy-little/R2S-R4S-X86-OpenWrt/workflows/R2S-OpenWrt-Current/badge.svg">
<p>


<h1 align="center">请勿用于商业用途!!!</h1>



### 特性

- 基于原生 OpenWrt 21.02 编译，默认管理地址192.168.2.1
默认开启了 Software Offload
- 内置升级功能可用，物理 Reset 按键可用
- 预配置了部分插件（包括但不限于 DNS 套娃，~~<b>(注意，6月29日开始取消了dns套娃，使用dnsfilter作为广告过滤手段，使用dnsproxy作为dns分流措施，海外端口5335，国内端口6050。)</b>~~
- 可无脑 opkg kmod
- R2S核心频率1.6（~~交换了LAN WAN~~），R4S核心频率2.2/1.8（建议使用5v4a电源，死机大多数情况下，都是因为<b>你用的电源过于垃圾</b>，另外，你也可以选择使用<b>自带的app限制最大频率</b>，茄子🍆）
- O3 编译，CFLAG优化，CacULE Scheduler，BBRv2
- 插件包含：SSRP，PassWall，OpenClash，AdguardHome，微信推送，网易云解锁，SQM，SmartDNS，ChinaDNS，网络唤醒，DDNS，迅雷快鸟，UPNP，FullCone(防火墙中开启，默认开启)，流量分载，irq优化，京东签到，Zerotier，FRPC，FRPS，无线打印，流量监控，过滤军刀，R2S-OLED
- ss协议在armv8上实现了aes硬件加速（请<b>仅使用aead加密</b>的连接方式）
- 如有任何问题，请先尝试ssh进入后台，输入fuck后回车，等待机器重启后确认问题是否已经解决

### 下载

- 选择自己<b>设备对应的固件</b>，并[下载](https://github.com/paniy-little/R2S-R4S-X86-OpenWrt/releases)

### 鸣谢

|          [CTCGFW](https://github.com/immortalwrt)           |           [coolsnowwolf](https://github.com/coolsnowwolf)            |              [Lienol](https://github.com/Lienol)               |
| :----------------------------------------------------------: | :----------------------------------------------------------: | :----------------------------------------------------------: |
| <img width="60" src="https://avatars.githubusercontent.com/u/53193414"/> | <img width="60" src="https://avatars.githubusercontent.com/u/31687149" /> | <img width="60" src="https://avatars.githubusercontent.com/u/23146169" /> |
|              [NoTengoBattery](https://github.com/NoTengoBattery)               |              [tty228](https://github.com/tty228)               |              [destan19](https://github.com/destan19)               |
| <img width="60" src="https://avatars.githubusercontent.com/u/11285513" /> | <img width="60" src="https://avatars.githubusercontent.com/u/33397881" /> | <img width="60" src="https://avatars.githubusercontent.com/u/3950091" /> |
|              [jerrykuku](https://github.com/jerrykuku)               |              [lisaac](https://github.com/lisaac)               |              [rufengsuixing](https://github.com/rufengsuixing)               |
| <img width="60" src="https://avatars.githubusercontent.com/u/9485680" /> | <img width="60" src="https://avatars.githubusercontent.com/u/3320969" /> | <img width="60" src="https://avatars.githubusercontent.com/u/22387141" /> |
|              [ElonH](https://github.com/ElonH)               |              [NateLol](https://github.com/NateLol)               |              [garypang13](https://github.com/garypang13)               |
| <img width="60" src="https://avatars.githubusercontent.com/u/32666230" /> | <img width="60" src="https://avatars.githubusercontent.com/u/5166306" /> | <img width="60" src="https://avatars.githubusercontent.com/u/48883331" /> |
|              [AmadeusGhost](https://github.com/AmadeusGhost)               |              [1715173329](https://github.com/1715173329)               |              [vernesong](https://github.com/vernesong)               |
| <img width="60" src="https://avatars.githubusercontent.com/u/42570690" /> | <img width="60" src="https://avatars.githubusercontent.com/u/22235437" /> | <img width="60" src="https://avatars.githubusercontent.com/u/42875168" /> |

---

## SELF 工程维护约定

`SELF/` 是自用覆盖层，与上游 `SCRIPTS/` 对应关系如下：

| 上游（SCRIPTS/） | 自用（SELF/） |
|---|---|
| `01_get_ready.sh` | `01_get_ready_self.sh` |
| `02_prepare_package.sh` | `02_prepare_package_self.sh` |
| `R2S/02_target_only.sh` | `R2S/02_target_only_self.sh` |
| `X86/02_target_only.sh` | `X86/02_target_only_self.sh` |
| `SEED/R2S/config.seed` | `R2S/config_self.seed` |
| `SEED/X86/config.seed` | `X86/config_self.seed` |

维护铁律：

1. **永不修改 `SCRIPTS/`**：上游每 3 小时自动同步覆盖，改动会丢失。
2. **`SELF/` 中的注释是刻意的**：不要顺手取消注释。
3. **`config_self.seed` 用注释而非删除**：删除会导致 `defconfig` 补回上游默认值。

上游同步后，先看 `git log --oneline -5` 与 `git diff HEAD~1 --name-only`，核对 `SCRIPTS/`
与 `SEED/` 的变更，再决定是否同步到 `SELF/` 对应文件。上游 bug 修复 / 功能改进应同步并保留
自用定制；上游新增功能或与自用定制冲突时，先确认再处理。
