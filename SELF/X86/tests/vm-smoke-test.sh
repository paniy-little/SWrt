#!/bin/bash
# =============================================================================
# SWrt SELF x86-64 VM 启动 smoke test（QEMU / TCG）
#
# 目的：在 CI 中用 QEMU 对 ext4-EFI 镜像做自动启动验证，确认：
#   1. BIOS/UEFI 能启动         2. kernel 能启动
#   3. rootfs 能挂载            4. OpenWrt init 完成
#   5. 至少识别 2 块虚拟网卡     6. 无 kernel panic / Oops / BUG / invalid module
#
# 用法：
#   vm-smoke-test.sh [<image>]   # 缺省时在 openwrt/bin/targets/x86/64 下自动查找
#
# 说明：这是 smoke test，不是完整集成测试。只以「boot log + init complete」
# 作为主要门禁，不搭建完整虚拟网络实验室。
# =============================================================================
set -euo pipefail

IMG="${1:-}"
if [ -z "$IMG" ]; then
  IMG="$(ls -1 openwrt/bin/targets/x86/64/*ext4-combined-efi.img 2>/dev/null | head -n1 || true)"
fi
if [ -z "$IMG" ] || [ ! -f "$IMG" ]; then
  echo "ERROR: no ext4-EFI image found (pass a path or run from repo root)"
  exit 1
fi

# OVMF (EFI) 固件：x86 SELF 镜像为 EFI，必须提供
OVMF=""
for f in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd; do
  if [ -f "$f" ]; then OVMF="$f"; break; fi
done
if [ -z "$OVMF" ]; then
  echo "ERROR: OVMF EFI firmware not found. Install package 'ovmf'."
  exit 1
fi

WORK="$(mktemp -d)"
RUN="$WORK/boot.raw"
LOG="$WORK/boot.log"
trap 'rm -rf "$WORK"' EXIT

# 解压（若为 .gz）
case "$IMG" in
  *.gz) zcat "$IMG" > "$RUN" ;;
  *)    cp "$IMG" "$RUN" ;;
esac

echo "[smoke] Image: $IMG"
echo "[smoke] OVMF : $OVMF"
echo "[smoke] Booting with QEMU (TCG), timeout ${BOOT_TIMEOUT:-150}s ..."

set +e
timeout "${BOOT_TIMEOUT:-150}s" qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 1024 \
  -smp 2 \
  -bios "$OVMF" \
  -drive file="$RUN",format=raw,if=virtio \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -netdev user,id=n1 -device virtio-net-pci,netdev=n1 \
  -nographic -no-reboot \
  > "$LOG" 2>&1
QEMU_EXIT=$?
set -e

# 可审计性：始终打印 QEMU 真实 exit code、init 状态、NIC、命中的 fatal marker。
# 仅用于定位，不因此放宽任何门禁判定。
mapfile -t nics < <(grep -oE '\beth[0-9]+\b' "$LOG" | sort -u)
FATAL_MARKER="$(grep -oE "Kernel panic|Oops|BUG:|invalid module format|Unknown symbol|VFS: Cannot open root|Failed to mount|segfault|Request for unknown module" "$LOG" | head -n1 || true)"
# init complete 诊断：干净输出，不重复打印 0
if grep -q 'init complete' "$LOG"; then
    INIT_COMPLETE="yes"
else
    INIT_COMPLETE="no"
fi
echo "[smoke] qemu exit=${QEMU_EXIT}"
echo "[smoke] init complete=${INIT_COMPLETE}"
echo "[smoke] NICs=${nics[*]:-none}"
echo "[smoke] fatal marker=${FATAL_MARKER:-none}"
echo "[smoke] ---- boot log tail ----"
tail -n 40 "$LOG"

# 失败标记：任一命中即判失败
if [ -n "$FATAL_MARKER" ]; then
  echo "::error::VM boot hit a fatal error marker: $FATAL_MARKER"
  exit 1
fi

# 成功标记：OpenWrt 标准 init 完成
if grep -q "init complete" "$LOG"; then
  echo "[smoke] PASS: OpenWrt 'init complete' reached"
  # 双 NIC 门禁：从 boot log 提取唯一接口名，至少识别 2 块。
  # 仅匹配完整 ethN 接口名，避免把 enabled/entropy 等普通单词误判为接口。
  nic_count="${#nics[@]}"
  echo "[smoke] unique NIC interface names seen: $nic_count (${nics[*]:-none})"
  if [ "$nic_count" -lt 2 ]; then
    echo "::error::Expected at least 2 NICs, only saw $nic_count"
    exit 1
  fi
  exit 0
fi

echo "::error::VM boot did not reach 'init complete' within timeout (qemu exit=${QEMU_EXIT})"
exit 1
