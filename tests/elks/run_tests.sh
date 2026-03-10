#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
KERNEL=${KERNEL:-"$REPO_ROOT/sys/kernel.bin"}
if [ -n "${ROOT_IMG:-}" ]; then
    ROOTFS_IMG=$ROOT_IMG
elif [ -f "$REPO_ROOT/root.img" ]; then
    ROOTFS_IMG=$REPO_ROOT/root.img
else
    ROOTFS_IMG=/home/segin/root.img
fi

QEMU=${QEMU:-qemu-system-i386}
TIMEOUT_BIN=${TIMEOUT_BIN:-timeout}
QEMU_TIMEOUT=${QEMU_TIMEOUT:-35}
LOG_DIR=${LOG_DIR:-"$SCRIPT_DIR/logs"}

require_file() {
    if [ ! -f "$1" ]; then
        printf 'missing required file: %s\n' "$1" >&2
        exit 1
    fi
}

stage_binary() {
    host_path=$1
    guest_path=$2
    debugfs -w -R "rm $guest_path" "$ROOTFS_IMG" >/dev/null 2>&1 || true
    debugfs -w -R "write $host_path $guest_path" "$ROOTFS_IMG" >/dev/null
}

run_case() {
    binary=$1
    shift
    log_path=$LOG_DIR/$binary.log

    "$TIMEOUT_BIN" "${QEMU_TIMEOUT}s" \
        "$QEMU" \
        -kernel "$KERNEL" \
        -accel tcg \
        -icount shift=9 \
        -smp 1 \
        -append "serial_debug console=serial0 root=/dev/storage/ide0 init=/bin/${binary} syscall_trace" \
        -serial "file:$log_path" \
        -drive "file=$ROOTFS_IMG,format=raw,if=ide" \
        -display none \
        -no-reboot >/dev/null 2>&1 || true

    for pattern in "$@"; do
        if ! rg -q --fixed-strings "$pattern" "$log_path"; then
            printf 'FAIL %s: missing "%s"\n' "$binary" "$pattern" >&2
            tail -n 80 "$log_path" >&2 || true
            exit 1
        fi
    done

    printf 'PASS %s\n' "$binary"
}

mkdir -p "$LOG_DIR"
require_file "$KERNEL"
require_file "$ROOTFS_IMG"

make -C "$SCRIPT_DIR" all >/dev/null

stage_binary "$SCRIPT_DIR/hello_elks" /bin/hello_elks
stage_binary "$SCRIPT_DIR/sleep_elks" /bin/sleep_elks
stage_binary "$SCRIPT_DIR/fileio_elks" /bin/fileio_elks
stage_binary "$SCRIPT_DIR/fork_elks" /bin/fork_elks
stage_binary "$SCRIPT_DIR/bounds_test_elks" /bin/bounds_test_elks
run_case hello_elks "Hello, ELKS!"
run_case sleep_elks "Slept, ELKS!"
run_case fileio_elks "ELKS file io"
run_case fork_elks "ELKS child" "ELKS parent"
run_case bounds_test_elks "Page Fault (in user process)" "Warning: Init process exited. System Halted (idle)."
