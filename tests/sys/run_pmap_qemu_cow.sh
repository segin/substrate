#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SYS_DIR="$ROOT_DIR/sys"
KERNEL_BIN="$SYS_DIR/kernel.bin"
ROOT_IMG="$ROOT_DIR/root.img"
if [ ! -f "$ROOT_IMG" ] && [ -f "$HOME/root.img" ]; then
    ROOT_IMG="$HOME/root.img"
fi
LOG_DIR="${TMPDIR:-/tmp}/substrate-pmap-qemu"
LOG_FILE="$LOG_DIR/pmap-cow.log"
OUT_FILE="$LOG_DIR/pmap-cow.out"
TEST_MARKER="Test: pmap_fork COW fault isolation"
PASS_MARKER="=== TESTS COMPLETE ==="
COW_PASS_MARKER="  PASS"

mkdir -p "$LOG_DIR"
rm -f "$LOG_FILE" "$OUT_FILE"

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "pmap-qemu: qemu-system-i386 not found" >&2
    exit 1
fi

if [ ! -f "$KERNEL_BIN" ]; then
    echo "pmap-qemu: missing kernel image: $KERNEL_BIN" >&2
    exit 1
fi

if [ ! -f "$ROOT_IMG" ]; then
    echo "pmap-qemu: missing root image: $ROOT_IMG" >&2
    exit 1
fi

rc=0
if ! (
    cd "$SYS_DIR"
    timeout 60s qemu-system-i386 \
        -display none \
        -snapshot \
        -no-reboot \
        -kernel kernel.bin \
        -accel tcg \
        -icount shift=9 \
        -smp 1 \
        -append "serial_debug test=pmap test_halt=1" \
        -serial "file:$LOG_FILE" \
        -hda "$ROOT_IMG"
) >"$OUT_FILE" 2>&1; then
    rc=$?
fi

if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "pmap-qemu: qemu failed" >&2
    tail -n 60 "$OUT_FILE" >&2 || true
    exit 1
fi

if ! grep -q "$TEST_MARKER" "$LOG_FILE"; then
    echo "pmap-qemu: missing COW integration marker" >&2
    tail -n 120 "$LOG_FILE" >&2 || true
    exit 1
fi

if ! awk -v marker="$TEST_MARKER" -v pass="$COW_PASS_MARKER" '
    $0 ~ marker { seen=1; next }
    seen && index($0, pass) { ok=1; exit }
    END { exit ok ? 0 : 1 }
' "$LOG_FILE"; then
    echo "pmap-qemu: COW test did not pass" >&2
    tail -n 120 "$LOG_FILE" >&2 || true
    exit 1
fi

if ! grep -q "$PASS_MARKER" "$LOG_FILE"; then
    echo "pmap-qemu: test suite did not complete" >&2
    tail -n 120 "$LOG_FILE" >&2 || true
    exit 1
fi

echo "pmap-qemu: ok"
