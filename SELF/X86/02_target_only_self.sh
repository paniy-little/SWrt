#!/bin/bash

# =============================================================================
# SELF/X86 目标专用构建步骤
#
# 原则：
#   - 所有影响最终 firmware 行为的差异都集中在本目录（SELF/X86）。
#   - 上游 OpenWrt 的 kernel vermagic 由当前实际 kernel/config 自动生成，
#     绝不伪造官方 ABI，也不强制覆盖 .vermagic。
#   - 默认安全：mitigations 保持内核默认开启（见 02_prepare_package_self.sh）。
#   - 网络初始化只在“首次启动 / 无已有用户配置”时执行，绝不每次开机覆盖。
# =============================================================================

set -u

echo "[SELF][X86] Applying common VM config"

# libsodium：保持与上游 feeds 一致的编译选项
sed -i 's,no-mips16 no-lto,no-mips16,g' feeds/packages/libs/libsodium/Makefile

# ---------------------------------------------------------------------------
# rc.local：保持克制，不强制切换 CPU governor。
# 虚拟机 CPU 的 P-state 主要由宿主机控制，guest 内强制写入 sysfs 意义有限，
# 且可能因路径不存在而报错。这里只保留一个安全、可忽略的占位实现。
# ---------------------------------------------------------------------------
echo "[SELF][X86] Writing conservative rc.local"
cat > ./package/base-files/files/etc/rc.local <<'EOF'
#!/bin/sh
# Put your custom commands here that should be executed once
# the system init finished. By default this file does nothing.
#
# 说明：本 VM 不在此处强制切换 CPU governor / intel_pstate。
#   - VM 的 P-state 由宿主机调度器控制；
#   - 若确需在特定宿主机上调整，请通过 /etc/rc.local 或 LuCI 手动配置，
#     SELF 不再默认写入任何强制值，避免 sysfs 不存在时产生无意义错误。
exit 0
EOF

# ---------------------------------------------------------------------------
# 网络初始化：由 SELF/X86/common/files/etc/uci-defaults/99-x86-vm-network
# 提供的 uci-defaults 脚本负责。uci-defaults 只会在该系统首次配置时执行并
# 自删除，因此：
#   - 升级固件时不会覆盖用户已有的 /etc/config/network；
#   - 支持通过 /etc/network.env 的 WAN_MAC / LAN_MAC 做 MAC 映射；
#   - 未指定 MAC 时回退到“第一块网卡 WAN、第二块网卡 LAN”，仅对全新安装生效。
# ---------------------------------------------------------------------------
echo "[SELF][X86] Installing VM network first-boot init"
mkdir -p ./files/etc/uci-defaults
cp -f ../SELF/X86/common/files/etc/uci-defaults/99-x86-vm-network \
      ./files/etc/uci-defaults/99-x86-vm-network
chmod +x ./files/etc/uci-defaults/99-x86-vm-network

# 预配置一些插件文件（仅首次安装生效的默认文件）
echo "[SELF][X86] Copying plugin default files"
cp -rf ../PATCH/files ./files

find ./ -name *.orig | xargs rm -f
find ./ -name *.rej | xargs rm -f

echo "[SELF][X86] Done"
exit 0