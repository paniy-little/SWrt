#!/bin/bash
# 快速验证 smoke harness 的固件参数与启动门禁，无需真实固件或 QEMU。
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SMOKE="$SCRIPT_DIR/vm-smoke-test.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

IMAGE="$WORK/test.img"
OVMF_CODE_FIXTURE="$WORK/OVMF_CODE_4M.fd"
OVMF_VARS_FIXTURE="$WORK/OVMF_VARS_4M.fd"
QEMU_MOCK="$WORK/qemu-system-x86_64"
ARGS_LOG="$WORK/qemu.args"
SUCCESS_LOG="$WORK/success.output"
MISSING_CONSOLE_LOG="$WORK/missing-console.output"
FATAL_LOG="$WORK/fatal.output"

printf 'image\n' > "$IMAGE"
printf 'code fixture\n' > "$OVMF_CODE_FIXTURE"
printf 'vars template\n' > "$OVMF_VARS_FIXTURE"

cat > "$QEMU_MOCK" <<'EOF'
#!/bin/bash
set -euo pipefail
: "${QEMU_ARGS_LOG:?}"
printf '%s\n' "$@" > "$QEMU_ARGS_LOG"
for arg in "$@"; do
  case "$arg" in
    if=pflash,format=raw,unit=1,file=*)
      vars_run="${arg##*file=}"
      printf 'guest-modified vars\n' > "$vars_run"
      ;;
  esac
done
case "${QEMU_MOCK_MODE:-success}" in
  success)
    printf '%s\n' \
      '[   12.314668] procd: - init -' \
      'Please press Enter to activate this console.' \
      '[   38.151969] virtio_net virtio0 eth0: entered allmulticast mode' \
      '[   38.277420] 8021q: adding VLAN 0 to HW filter on device eth1'
    ;;
  missing-console)
    printf '%s\n' \
      '[   12.314668] procd: - init -' \
      'eth0: link up' \
      'eth1: link up'
    ;;
  fatal)
    printf '%s\n' \
      '[   12.314668] procd: - init -' \
      'Please press Enter to activate this console.' \
      'eth0: link up' \
      'eth1: link up' \
      'Kernel panic - not syncing: selftest'
    ;;
esac
exec sleep 30
EOF
chmod +x "$QEMU_MOCK"

QEMU_ARGS_LOG="$ARGS_LOG" \
OVMF_CODE="$OVMF_CODE_FIXTURE" \
OVMF_VARS="$OVMF_VARS_FIXTURE" \
QEMU_BIN="$QEMU_MOCK" \
BOOT_TIMEOUT=5 \
BOOT_STABILITY_SECONDS=1 \
QEMU_MOCK_MODE=success \
  "$SMOKE" "$IMAGE" > "$SUCCESS_LOG" || {
    cat "$SUCCESS_LOG"
    exit 1
  }

grep -Fxq "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE_FIXTURE" "$ARGS_LOG"
grep -Eq '^if=pflash,format=raw,unit=1,file=.*/OVMF_VARS\.fd$' "$ARGS_LOG"
if grep -Fxq -- '-bios' "$ARGS_LOG"; then
  echo "ERROR: legacy -bios argument returned"
  exit 1
fi
grep -Fxq 'vars template' "$OVMF_VARS_FIXTURE"
grep -Fq '[smoke] stop reason=boot-ready' "$SUCCESS_LOG"
grep -Fq '[smoke] boot ready=yes' "$SUCCESS_LOG"
grep -Fq '[smoke] unique NIC interface names seen: 2 (eth0 eth1)' "$SUCCESS_LOG"
grep -Fq '[smoke] PASS: procd init, console readiness, and dual NIC gates reached' "$SUCCESS_LOG"

if QEMU_ARGS_LOG="$ARGS_LOG" \
  OVMF_CODE="$OVMF_CODE_FIXTURE" \
  OVMF_VARS="$OVMF_VARS_FIXTURE" \
  QEMU_BIN="$QEMU_MOCK" \
  BOOT_TIMEOUT=2 \
  BOOT_STABILITY_SECONDS=1 \
  QEMU_MOCK_MODE=missing-console \
    "$SMOKE" "$IMAGE" > "$MISSING_CONSOLE_LOG"; then
  echo "ERROR: missing console readiness marker was accepted"
  exit 1
fi
grep -Fq '[smoke] stop reason=timeout' "$MISSING_CONSOLE_LOG"
grep -Fq '::error::VM boot did not reach procd init and console readiness' "$MISSING_CONSOLE_LOG"

if QEMU_ARGS_LOG="$ARGS_LOG" \
  OVMF_CODE="$OVMF_CODE_FIXTURE" \
  OVMF_VARS="$OVMF_VARS_FIXTURE" \
  QEMU_BIN="$QEMU_MOCK" \
  BOOT_TIMEOUT=5 \
  BOOT_STABILITY_SECONDS=1 \
  QEMU_MOCK_MODE=fatal \
    "$SMOKE" "$IMAGE" > "$FATAL_LOG"; then
  echo "ERROR: fatal marker was accepted"
  exit 1
fi
grep -Fq '[smoke] stop reason=fatal-marker' "$FATAL_LOG"
grep -Fq '::error::VM boot hit a fatal error marker: Kernel panic' "$FATAL_LOG"

echo "[selftest] PASS: pflash, isolated VARS, real readiness, timeout, and fatal gates verified"
