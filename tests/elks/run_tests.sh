#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
KERNEL=${KERNEL:-"$REPO_ROOT/sys/kernel.bin"}
if [ -n "${ROOT_IMG:-}" ]; then
    ROOTFS_SOURCE_IMG=$ROOT_IMG
elif [ -f "$REPO_ROOT/root.img" ]; then
    ROOTFS_SOURCE_IMG=$REPO_ROOT/root.img
else
    ROOTFS_SOURCE_IMG=/home/segin/root.img
fi

QEMU=${QEMU:-qemu-system-i386}
TIMEOUT_BIN=${TIMEOUT_BIN:-timeout}
QEMU_TIMEOUT=${QEMU_TIMEOUT:-35}
LOG_DIR=${LOG_DIR:-"$SCRIPT_DIR/logs"}
WORK_ROOTFS_IMG=${WORK_ROOTFS_IMG:-"$LOG_DIR/rootfs.img"}
DEFAULT_CASES="hello_elks sleep_elks fileio_elks fork_elks bounds_test_elks cat_elks"

if [ "$#" -gt 0 ]; then
    CASES="$*"
else
    CASES=${CASES:-$DEFAULT_CASES}
fi

require_file() {
    if [ ! -f "$1" ]; then
        printf 'missing required file: %s\n' "$1" >&2
        exit 1
    fi
}

want_case() {
    name=$1
    for enabled in $CASES; do
        if [ "$enabled" = "$name" ]; then
            return 0
        fi
    done
    return 1
}

stage_binary() {
    host_path=$1
    guest_path=$2
    debugfs -w -R "rm $guest_path" "$WORK_ROOTFS_IMG" >/dev/null 2>&1 || true
    debugfs -w -R "write $host_path $guest_path" "$WORK_ROOTFS_IMG" >/dev/null
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
        -drive "file=$WORK_ROOTFS_IMG,format=raw,if=ide" \
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
require_file "$ROOTFS_SOURCE_IMG"
cp "$ROOTFS_SOURCE_IMG" "$WORK_ROOTFS_IMG"

make -C "$SCRIPT_DIR" all >/dev/null

stage_binary "$SCRIPT_DIR/hello_elks" /bin/hello_elks
stage_binary "$SCRIPT_DIR/sleep_elks" /bin/sleep_elks
stage_binary "$SCRIPT_DIR/fileio_elks" /bin/fileio_elks
stage_binary "$SCRIPT_DIR/fork_elks" /bin/fork_elks
stage_binary "$SCRIPT_DIR/bounds_test_elks" /bin/bounds_test_elks
stage_binary "$SCRIPT_DIR/cat_elks" /bin/cat_elks

if want_case cat_elks; then
    cat_input=$LOG_DIR/cat_input.txt
    printf 'ELKS cat sample\n' > "$cat_input"
    debugfs -w -R "rm /elks-cat.txt" "$WORK_ROOTFS_IMG" >/dev/null 2>&1 || true
    debugfs -w -R "write $cat_input /elks-cat.txt" "$WORK_ROOTFS_IMG" >/dev/null
fi

if want_case hello_elks; then
    run_case hello_elks "Hello, ELKS!"
fi
if want_case sleep_elks; then
    run_case sleep_elks "Slept, ELKS!"
fi
if want_case fileio_elks; then
    run_case fileio_elks "ELKS file io"
fi
if want_case fork_elks; then
    run_case fork_elks "ELKS child" "ELKS parent"
fi
if want_case bounds_test_elks; then
    run_case bounds_test_elks "Page Fault (in user process)" "Warning: Init process exited. System Halted (idle)."
fi
if want_case cat_elks; then
    run_case cat_elks "ELKS cat sample"
fi
