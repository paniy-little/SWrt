#!/bin/bash
# 快速验证 smoke harness 的 OVMF 参数契约，无需真实固件或 QEMU。
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SMOKE="$SCRIPT_DIR/vm-smoke-test.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

IMAGE="$WORK/test.img"
OVMF_CODE_FIXTURE="$WORK/OVMF_CODE_4M.fd"
OVMF_VARS_FIXTURE="$WORK/OVMF_VARS_4M.fd"
QEMU_MOCK="$WORK/qemu-system-x86_64"
TIMEOUT_MOCK="$WORK/timeout"
ARGS_LOG="$WORK/qemu.args"
OUTPUT_LOG="$WORK/smoke.output"

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
printf '%s\n' 'procd: - init complete -' 'eth0: link up' 'eth1: link up'
EOF
chmod +x "$QEMU_MOCK"

cat > "$TIMEOUT_MOCK" <<'EOF'
#!/bin/bash
set -euo pipefail
shift
exec "$@"
EOF
chmod +x "$TIMEOUT_MOCK"

QEMU_ARGS_LOG="$ARGS_LOG" \
OVMF_CODE="$OVMF_CODE_FIXTURE" \
OVMF_VARS="$OVMF_VARS_FIXTURE" \
QEMU_BIN="$QEMU_MOCK" \
TIMEOUT_BIN="$TIMEOUT_MOCK" \
BOOT_TIMEOUT=5 \
  "$SMOKE" "$IMAGE" > "$OUTPUT_LOG" || {
    cat "$OUTPUT_LOG"
    exit 1
  }

grep -Fxq "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE_FIXTURE" "$ARGS_LOG"
grep -Eq '^if=pflash,format=raw,unit=1,file=.*/OVMF_VARS\.fd$' "$ARGS_LOG"
if grep -Fxq -- '-bios' "$ARGS_LOG"; then
  echo "ERROR: legacy -bios argument returned"
  exit 1
fi
grep -Fxq 'vars template' "$OVMF_VARS_FIXTURE"
grep -Fq "[smoke] PASS: OpenWrt 'init complete' reached" "$OUTPUT_LOG"
grep -Fq '[smoke] unique NIC interface names seen: 2 (eth0 eth1)' "$OUTPUT_LOG"

echo "[selftest] PASS: OVMF pflash pair, isolated VARS, and boot gates verified"
