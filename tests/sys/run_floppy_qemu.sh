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
DEFAULT_CASES="floppy_qemu floppy_format floppy_change"

if [ "$#" -gt 0 ]; then
    CASES="$*"
else
    CASES=${CASES:-$DEFAULT_CASES}
fi

mkdir -p "$LOG_DIR"

want_case() {
    name=$1
    for enabled in $CASES; do
        if [ "$enabled" = "$name" ]; then
            return 0
        fi
    done
    return 1
}

create_pattern_image() {
    image=$1
    fill=$2

    python3 - "$image" "$fill" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
fill = int(sys.argv[2], 0)
size = 1474560
path.parent.mkdir(parents=True, exist_ok=True)
path.write_bytes(bytes([fill]) * size)
PY
}

run_case() {
    case_name=$1
    test_name=$2
    marker=$3
    floppy_img=$4

    log_file="$LOG_DIR/$case_name.log"
    out_file="$LOG_DIR/$case_name.out"
    rc=0

    rm -f "$log_file" "$out_file"

    if ! "$TIMEOUT_BIN" "${QEMU_TIMEOUT}s" \
        "$QEMU" \
        -display none \
        -no-reboot \
        -accel tcg \
        -icount shift=9 \
        -m 256 \
        -kernel "$KERNEL_ZIMAGE" \
        -initrd "$INITRD_IMG" \
        -append "serial_debug console=serial0 root=/dev/storage/ram0 test=$test_name test_halt=1" \
        -drive "file=$floppy_img,format=raw,if=floppy,index=0" \
        -serial "file:$log_file" >"$out_file" 2>&1; then
        rc=$?
    fi

    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        printf 'FAIL %s: qemu exited rc=%d\n' "$case_name" "$rc" >&2
        tail -n 80 "$out_file" >&2 || true
        exit 1
    fi

    if ! rg -q --fixed-strings "$marker" "$log_file"; then
        printf 'FAIL %s: missing pass marker\n' "$case_name" >&2
        tail -n 120 "$log_file" >&2 || true
        exit 1
    fi

    printf 'PASS %s\n' "$case_name"
}

run_monitor_case() {
    case_name=$1
    test_name=$2
    marker=$3
    floppy_img=$4
    replacement_img=$5

    log_file="$LOG_DIR/$case_name.log"
    out_file="$LOG_DIR/$case_name.out"
    mon_path="$LOG_DIR/$case_name.mon"
    qemu_pid=
    found=0
    i=0

    rm -f "$log_file" "$out_file" "$mon_path"

    "$QEMU" \
        -display none \
        -no-reboot \
        -accel tcg \
        -icount shift=9 \
        -m 256 \
        -kernel "$KERNEL_ZIMAGE" \
        -initrd "$INITRD_IMG" \
        -append "serial_debug console=serial0 root=/dev/storage/ram0 test=$test_name test_halt=1" \
        -drive "file=$floppy_img,format=raw,if=floppy,index=0" \
        -serial "file:$log_file" \
        -monitor "unix:$mon_path,server,nowait" >"$out_file" 2>&1 &
    qemu_pid=$!

    trap 'kill "$qemu_pid" >/dev/null 2>&1 || true; wait "$qemu_pid" >/dev/null 2>&1 || true' EXIT INT TERM

    while [ ! -S "$mon_path" ] && [ $i -lt 50 ]; do
        sleep 0.1
        i=$((i + 1))
    done
    if [ ! -S "$mon_path" ]; then
        printf 'FAIL %s: monitor socket not ready\n' "$case_name" >&2
        exit 1
    fi

    sleep 2
    printf 'change floppy0 %s raw\n' "$replacement_img" | socat - UNIX-CONNECT:"$mon_path" >/dev/null 2>&1 || true

    i=0
    while [ $i -lt $((QEMU_TIMEOUT * 5)) ]; do
        if [ -f "$log_file" ] && rg -q --fixed-strings "$marker" "$log_file"; then
            found=1
            break
        fi
        if ! kill -0 "$qemu_pid" >/dev/null 2>&1; then
            break
        fi
        sleep 0.2
        i=$((i + 1))
    done

    printf 'quit\n' | socat - UNIX-CONNECT:"$mon_path" >/dev/null 2>&1 || true
    wait "$qemu_pid" >/dev/null 2>&1 || true
    trap - EXIT INT TERM

    if [ "$found" -ne 1 ]; then
        printf 'FAIL %s: missing pass marker\n' "$case_name" >&2
        tail -n 120 "$log_file" >&2 || true
        tail -n 80 "$out_file" >&2 || true
        exit 1
    fi

    printf 'PASS %s\n' "$case_name"
}

if want_case floppy_qemu; then
    floppy_img="$LOG_DIR/fd0-roundtrip.img"
    truncate -s 1474560 "$floppy_img"
    run_case floppy_qemu floppy_qemu "PASS: floppy_qemu round-trip" "$floppy_img"
fi

if want_case floppy_format; then
    floppy_img="$LOG_DIR/fd0-format.img"
    truncate -s 1474560 "$floppy_img"
    run_case floppy_format floppy_format "PASS: floppy_format format/read-back" "$floppy_img"
fi

if want_case floppy_change; then
    before_img="$LOG_DIR/fd0-before.img"
    after_img="$LOG_DIR/fd0-after.img"
    create_pattern_image "$before_img" 0x11
    create_pattern_image "$after_img" 0xA5
    run_monitor_case floppy_change floppy_change "PASS: floppy_change media change detected" "$before_img" "$after_img"
fi

printf 'floppy-qemu: ok\n'
