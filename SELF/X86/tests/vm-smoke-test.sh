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
# 说明：这是 smoke test，不是完整集成测试。以 procd 进入 init、控制台就绪、
# 双网卡识别及无 fatal marker 作为门禁，不搭建完整虚拟网络实验室。
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

# OVMF (EFI) 固件：4M 固件由 CODE 与 VARS 两部分组成，必须作为 pflash 成对加载。
# 不能把 OVMF_CODE_4M.fd 直接交给 -bios；它不是独立的传统 PC BIOS 镜像。
OVMF_CODE="${OVMF_CODE:-}"
OVMF_VARS="${OVMF_VARS:-}"
if [ -n "$OVMF_CODE" ] || [ -n "$OVMF_VARS" ]; then
  if [ ! -r "$OVMF_CODE" ] || [ ! -s "$OVMF_CODE" ] ||
     [ ! -r "$OVMF_VARS" ] || [ ! -s "$OVMF_VARS" ]; then
    echo "ERROR: OVMF_CODE and OVMF_VARS overrides must both name readable, non-empty files."
    exit 1
  fi
else
  for pair in \
    "/usr/share/OVMF/OVMF_CODE_4M.fd:/usr/share/OVMF/OVMF_VARS_4M.fd" \
    "/usr/share/OVMF/OVMF_CODE.fd:/usr/share/OVMF/OVMF_VARS.fd"; do
    IFS=: read -r code vars <<< "$pair"
    if [ -r "$code" ] && [ -s "$code" ] && [ -r "$vars" ] && [ -s "$vars" ]; then
      OVMF_CODE="$code"
      OVMF_VARS="$vars"
      break
    fi
  done
fi
if [ -z "$OVMF_CODE" ]; then
  echo "ERROR: complete OVMF CODE/VARS firmware pair not found. Install package 'ovmf'."
  exit 1
fi

QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  echo "ERROR: QEMU executable not found: $QEMU_BIN"
  exit 1
fi
BOOT_TIMEOUT="${BOOT_TIMEOUT:-150}"
case "$BOOT_TIMEOUT" in
  ''|*[!0-9]*) echo "ERROR: BOOT_TIMEOUT must be a positive integer"; exit 1 ;;
  0) echo "ERROR: BOOT_TIMEOUT must be greater than zero"; exit 1 ;;
esac
BOOT_STABILITY_SECONDS="${BOOT_STABILITY_SECONDS:-5}"
case "$BOOT_STABILITY_SECONDS" in
  ''|*[!0-9]*) echo "ERROR: BOOT_STABILITY_SECONDS must be a non-negative integer"; exit 1 ;;
esac

WORK="$(mktemp -d)"
RUN="$WORK/boot.raw"
LOG="$WORK/boot.log"
OVMF_VARS_RUN="$WORK/OVMF_VARS.fd"
QEMU_PID=""

cleanup() {
  if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# 每次测试使用独立的可写变量盘，避免修改系统安装的 OVMF 模板。
cp "$OVMF_VARS" "$OVMF_VARS_RUN"

# 解压（若为 .gz）
case "$IMG" in
  *.gz) zcat "$IMG" > "$RUN" ;;
  *)    cp "$IMG" "$RUN" ;;
esac

echo "[smoke] Image: $IMG"
echo "[smoke] OVMF CODE: $OVMF_CODE"
echo "[smoke] OVMF VARS: $OVMF_VARS_RUN (copy of $OVMF_VARS)"
echo "[smoke] Booting with QEMU (TCG), timeout ${BOOT_TIMEOUT}s ..."

fatal_marker_from_log() {
  grep -oE "Kernel panic|Oops|BUG:|invalid module format|Unknown symbol|VFS: Cannot open root|Failed to mount|segfault|Request for unknown module" "$LOG" | head -n1 || true
}

readiness_markers_present() {
  grep -Fq 'procd: - init -' "$LOG" &&
    grep -Fq 'Please press Enter to activate this console.' "$LOG"
}

seen_nic_count() {
  { grep -oE '\beth[0-9]+\b' "$LOG" || true; } | sort -u | wc -l | tr -d ' '
}

"$QEMU_BIN" \
  -machine q35,accel=tcg \
  -cpu max \
  -m 1024 \
  -smp 2 \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE" \
  -drive "if=pflash,format=raw,unit=1,file=$OVMF_VARS_RUN" \
  -drive file="$RUN",format=raw,if=virtio \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -netdev user,id=n1 -device virtio-net-pci,netdev=n1 \
  -nographic -no-reboot \
  > "$LOG" 2>&1 &
QEMU_PID=$!

BOOT_STARTED=$SECONDS
READY_STARTED=""
STOP_REASON="qemu-exited"
while kill -0 "$QEMU_PID" 2>/dev/null; do
  if [ -n "$(fatal_marker_from_log)" ]; then
    STOP_REASON="fatal-marker"
    break
  fi
  if readiness_markers_present && [ "$(seen_nic_count)" -ge 2 ]; then
    if [ -z "$READY_STARTED" ]; then
      READY_STARTED=$SECONDS
    fi
    if [ $((SECONDS - READY_STARTED)) -ge "$BOOT_STABILITY_SECONDS" ]; then
      STOP_REASON="boot-ready"
      break
    fi
  else
    READY_STARTED=""
  fi
  if [ $((SECONDS - BOOT_STARTED)) -ge "$BOOT_TIMEOUT" ]; then
    STOP_REASON="timeout"
    break
  fi
  sleep 1
done

if kill -0 "$QEMU_PID" 2>/dev/null; then
  kill "$QEMU_PID" 2>/dev/null || true
fi
set +e
wait "$QEMU_PID"
QEMU_EXIT=$?
set -e
QEMU_PID=""

# 可审计性：始终打印 QEMU 真实 exit code、停止原因、启动标记、NIC 和 fatal marker。
# 仅用于定位，不因此放宽任何门禁判定。
nics=()
while IFS= read -r nic; do
  nics+=("$nic")
done < <(grep -oE '\beth[0-9]+\b' "$LOG" | sort -u)
FATAL_MARKER="$(fatal_marker_from_log)"
if readiness_markers_present; then
  BOOT_READY="yes"
else
  BOOT_READY="no"
fi
echo "[smoke] qemu exit=${QEMU_EXIT}"
echo "[smoke] stop reason=${STOP_REASON}"
echo "[smoke] boot ready=${BOOT_READY}"
echo "[smoke] NICs=${nics[*]:-none}"
echo "[smoke] fatal marker=${FATAL_MARKER:-none}"
echo "[smoke] ---- boot log tail ----"
tail -n 40 "$LOG"

# 失败标记：任一命中即判失败
if [ -n "$FATAL_MARKER" ]; then
  echo "::error::VM boot hit a fatal error marker: $FATAL_MARKER"
  exit 1
fi

# 双 NIC 门禁：从 boot log 提取唯一接口名，至少识别 2 块。
# 仅匹配完整 ethN 接口名，避免把 enabled/entropy 等普通单词误判为接口。
nic_count="${#nics[@]}"
echo "[smoke] unique NIC interface names seen: $nic_count (${nics[*]:-none})"
if [ "$nic_count" -lt 2 ]; then
  echo "::error::Expected at least 2 NICs, only saw $nic_count"
  exit 1
fi

if [ "$BOOT_READY" != "yes" ]; then
  echo "::error::VM boot did not reach procd init and console readiness within ${BOOT_TIMEOUT}s (qemu exit=${QEMU_EXIT})"
  exit 1
fi

echo "[smoke] PASS: procd init, console readiness, and dual NIC gates reached"
