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

# ---------------------------------------------------------------------------
# x86 内核版本校验：直接读取 x86 自身的 KERNEL_PATCHVER，不依赖 rockchip。
# 与公共 SELF 中 rockchip 的检查解耦，X86 不再因 rockchip Makefile 而失败。
# ---------------------------------------------------------------------------
SUPPORTED_KERNEL="6.12"
current_version="$(
    sed -n 's/^KERNEL_PATCHVER:=//p' ./target/linux/x86/Makefile
)"
if [ -z "$current_version" ]; then
    echo "ERROR: cannot determine x86 KERNEL_PATCHVER" >&2
    exit 1
fi
if [ "$current_version" != "$SUPPORTED_KERNEL" ]; then
    echo "ERROR: x86 kernel mismatch: expected $SUPPORTED_KERNEL, got $current_version" >&2
    exit 1
fi
echo "[SELF][X86] x86 kernel OK: $current_version"

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
# files overlay 合并：
#   顺序为 PATCH/files 先，SELF/X86/common/files 后（后者优先级最高）。
#   使用 cp -a <src>/. <dst>/ 合并内容，避免目标目录已存在时产生
#   ./files/files/... 的嵌套层级。
# ---------------------------------------------------------------------------
echo "[SELF][X86] Merging files overlay (PATCH then SELF)"
mkdir -p ./files
cp -a ../PATCH/files/. ./files/
cp -a ../SELF/X86/common/files/. ./files/

# 网络初始化：由 SELF/X86/common/files/etc/uci-defaults/99-x86-vm-network
# 提供的 uci-defaults 脚本负责。uci-defaults 只会在该系统首次配置时执行并
# 自删除，因此：
#   - 升级固件时不会覆盖用户已有的 /etc/config/network；
#   - 支持通过 /etc/network.env 的 WAN_MAC / LAN_MAC 做 MAC 映射；
#   - 未指定 MAC 时回退到“第一块网卡 WAN、第二块网卡 LAN”，仅对全新安装生效。
# ---------------------------------------------------------------------------
echo "[SELF][X86] Installing VM network first-boot init"
chmod +x ./files/etc/uci-defaults/99-x86-vm-network 2>/dev/null || true

# swrt-vm-performance 首次启动自动启用（uci-defaults 脚本必须可执行）
# 该脚本负责 enable + start 服务，生成 /etc/rc.d/S99swrt-vm-performance。
echo "[SELF][X86] Installing swrt-vm-performance first-boot enable"
chmod +x ./files/etc/uci-defaults/98-swrt-vm-performance 2>/dev/null || true

# 预配置一些插件文件（仅首次安装生效的默认文件）
echo "[SELF][X86] Copying plugin default files"

find ./ -name *.orig | xargs rm -f
find ./ -name *.rej | xargs rm -f

# ---------------------------------------------------------------------------
# overlay 结果校验：确保合并层级正确，且关键文件存在。
# ---------------------------------------------------------------------------
if [ -d ./files/files ]; then
    echo "ERROR: nested ./files/files overlay detected" >&2
    exit 1
fi

CHECK_FILES="./files/etc/uci-defaults/99-x86-vm-network"
# swrt-vm-performance 首次启动自动启用脚本（uci-defaults）
CHECK_FILES="$CHECK_FILES ./files/etc/uci-defaults/98-swrt-vm-performance"
# SELF/X86/common/files 中预期进入 firmware 的关键文件
CHECK_FILES="$CHECK_FILES ./files/usr/bin/swrt-vm-perf"
CHECK_FILES="$CHECK_FILES ./files/etc/config/swrt-vm-performance"
CHECK_FILES="$CHECK_FILES ./files/etc/init.d/swrt-vm-performance"
CHECK_FILES="$CHECK_FILES ./files/etc/sysctl.d/90-swrt-x86-network.conf"
# PATCH/files 中预期进入 firmware 的关键文件
if [ -f ../PATCH/files/etc/uci-defaults/99-yunshu-disable-ipv6-pd ]; then
    CHECK_FILES="$CHECK_FILES ./files/etc/uci-defaults/99-yunshu-disable-ipv6-pd"
fi
# X86 覆盖版 hotplug（SELF 覆盖 PATCH，Hyper-V hv_netvsc ring 默认调优）
if [ -f ../SELF/X86/common/files/etc/hotplug.d/net/01-maximize_nic_rx_tx_buffers ]; then
    CHECK_FILES="$CHECK_FILES ./files/etc/hotplug.d/net/01-maximize_nic_rx_tx_buffers"
fi

for f in $CHECK_FILES; do
    if [ ! -f "$f" ]; then
        echo "ERROR: overlay missing expected file: $f" >&2
        exit 1
    fi
done
echo "[SELF][X86] overlay OK: $(echo $CHECK_FILES | wc -w | tr -d ' ') files present"

echo "[SELF][X86] Done"
exit 0