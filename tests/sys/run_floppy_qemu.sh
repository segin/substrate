#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SYS_DIR="$ROOT_DIR/sys"
KERNEL_ZIMAGE="${KERNEL_ZIMAGE:-$SYS_DIR/kernel.zimage}"
if [ -n "${ROOT_IMG:-}" ]; then
    INITRD_IMG=$ROOT_IMG
elif [ -f "$ROOT_DIR/root.img" ]; then
    INITRD_IMG="$ROOT_DIR/root.img"
else
    INITRD_IMG="/home/segin/root.img"
fi
QEMU=${QEMU:-qemu-system-i386}
TIMEOUT_BIN=${TIMEOUT_BIN:-timeout}
QEMU_TIMEOUT=${QEMU_TIMEOUT:-45}
LOG_DIR=${LOG_DIR:-"${TMPDIR:-/tmp}/substrate-floppy-qemu"}
FLOPPY_IMG="$LOG_DIR/fd0.img"
LOG_FILE="$LOG_DIR/floppy.log"
OUT_FILE="$LOG_DIR/floppy.out"

mkdir -p "$LOG_DIR"
truncate -s 1474560 "$FLOPPY_IMG"
rm -f "$LOG_FILE" "$OUT_FILE"

"$TIMEOUT_BIN" "${QEMU_TIMEOUT}s" \
    "$QEMU" \
    -display none \
    -no-reboot \
    -accel tcg \
    -icount shift=9 \
    -m 256 \
    -kernel "$KERNEL_ZIMAGE" \
    -initrd "$INITRD_IMG" \
    -append "serial_debug console=serial0 root=/dev/storage/ram0 test=floppy_qemu test_halt=1" \
    -drive "file=$FLOPPY_IMG,format=raw,if=floppy,index=0" \
    -serial "file:$LOG_FILE" >"$OUT_FILE" 2>&1 || rc=$?
rc=${rc:-0}
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    printf 'FAIL floppy_qemu: qemu exited rc=%d\n' "$rc" >&2
    tail -n 80 "$OUT_FILE" >&2 || true
    exit 1
fi

if ! rg -q --fixed-strings "PASS: floppy_qemu round-trip" "$LOG_FILE"; then
    printf 'FAIL floppy_qemu: missing pass marker\n' >&2
    tail -n 120 "$LOG_FILE" >&2 || true
    exit 1
fi

printf 'PASS floppy_qemu\n'
