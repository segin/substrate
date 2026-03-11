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
DEFAULT_CASES="hello_elks sleep_elks fileio_elks fork_elks bounds_test_elks cat_elks fuzz_syscalls_elks upstream_ls_elks upstream_uname_elks upstream_df_elks upstream_ps_elks upstream_sh_prompt_elks upstream_sh_ls_elks native_sh_elks_sh"

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
        -append "serial_debug console=serial0 debug=syscall,trap,perso:elks,perso:elks:aout root=/dev/storage/ide0 init=/bin/${binary}" \
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

run_init_case() {
    case_name=$1
    init_path=$2
    shift 2
    log_path=$LOG_DIR/$case_name.log

    "$TIMEOUT_BIN" "${QEMU_TIMEOUT}s" \
        "$QEMU" \
        -kernel "$KERNEL" \
        -accel tcg \
        -icount shift=9 \
        -smp 1 \
        -append "serial_debug console=serial0 debug=trap,perso:elks,perso:elks:aout root=/dev/storage/ide0 init=$init_path" \
        -serial "file:$log_path" \
        -drive "file=$WORK_ROOTFS_IMG,format=raw,if=ide" \
        -display none \
        -no-reboot >/dev/null 2>&1 || true

    for pattern in "$@"; do
        if ! rg -q --fixed-strings "$pattern" "$log_path"; then
            printf 'FAIL %s: missing "%s"\n' "$case_name" "$pattern" >&2
            tail -n 80 "$log_path" >&2 || true
            exit 1
        fi
    done

    printf 'PASS %s\n' "$case_name"
}

run_monitor_case() {
    case_name=$1
    init_path=$2
    send_keys=$3
    shift 3
    log_path=$LOG_DIR/$case_name.log
    mon_path=$LOG_DIR/$case_name.mon

    rm -f "$mon_path"
    "$QEMU" \
        -kernel "$KERNEL" \
        -accel tcg \
        -icount shift=9 \
        -smp 1 \
        -append "serial_debug console=serial0 debug=trap,perso:elks,perso:elks:aout root=/dev/storage/ide0 init=$init_path" \
        -serial "file:$log_path" \
        -monitor "unix:$mon_path,server,nowait" \
        -drive "file=$WORK_ROOTFS_IMG,format=raw,if=ide" \
        -display none \
        -no-reboot >/dev/null 2>&1 &
    qemu_pid=$!

    trap 'kill $qemu_pid >/dev/null 2>&1 || true; wait $qemu_pid >/dev/null 2>&1 || true' EXIT INT TERM

    i=0
    while [ ! -S "$mon_path" ] && [ $i -lt 50 ]; do
        sleep 0.1
        i=$((i + 1))
    done

    if [ ! -S "$mon_path" ]; then
        printf 'FAIL %s: monitor socket not ready\n' "$case_name" >&2
        exit 1
    fi

    sleep 2
    {
        printf '%s\n' "$send_keys"
        printf 'quit\n'
    } | socat - UNIX-CONNECT:"$mon_path" >/dev/null 2>&1 || true

    wait $qemu_pid >/dev/null 2>&1 || true
    trap - EXIT INT TERM

    for pattern in "$@"; do
        if ! rg -q --fixed-strings "$pattern" "$log_path"; then
            printf 'FAIL %s: missing "%s"\n' "$case_name" "$pattern" >&2
            tail -n 120 "$log_path" >&2 || true
            exit 1
        fi
    done

    printf 'PASS %s\n' "$case_name"
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
stage_binary "$SCRIPT_DIR/fuzz_syscalls_elks" /bin/fuzz_syscalls_elks

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
    run_case bounds_test_elks "CORE: captured crash state" "Warning: Init process exited. System Halted (idle)."
fi
if want_case cat_elks; then
    run_case cat_elks "ELKS cat sample"
fi
if want_case fuzz_syscalls_elks; then
    run_case fuzz_syscalls_elks "ELKS fuzz done"
fi
if want_case upstream_ls_elks; then
    run_init_case upstream_ls_elks /perso/elks/bin/ls "bin         dev         lost+found  perso       proc        sys"
fi
if want_case upstream_uname_elks; then
    run_init_case upstream_uname_elks /perso/elks/bin/uname "Substra"
fi
if want_case upstream_df_elks; then
    run_init_case upstream_df_elks /perso/elks/bin/df "Filesystem    1K-blocks" "[Not a MINIX filesystem]             /" "[Not a MINIX filesystem]             /proc"
fi
if want_case upstream_ps_elks; then
    run_init_case upstream_ps_elks /perso/elks/bin/ps "  PID   GRP  TTY USER STAT CPU  HEAP  FREE   SIZE COMMAND" "ps " "(kinit)"
fi
if want_case upstream_sh_prompt_elks; then
    run_init_case upstream_sh_prompt_elks /perso/elks/bin/sh "# "
fi
if want_case upstream_sh_ls_elks; then
    run_monitor_case upstream_sh_ls_elks /perso/elks/bin/sh \
        "sendkey l\nsendkey s\nsendkey ret" \
        "# " "bin" "dev" "perso"
fi
if want_case native_sh_elks_sh; then
    case_name=native_sh_elks_sh
    log_path=$LOG_DIR/$case_name.log
    mon_path=$LOG_DIR/$case_name.mon
    rm -f "$mon_path"
    "$QEMU" \
        -kernel "$KERNEL" \
        -accel tcg \
        -icount shift=9 \
        -smp 1 \
        -append "serial_debug console=serial0 debug=trap,perso:elks,perso:elks:aout root=/dev/storage/ide0 init=/bin/sh" \
        -serial "file:$log_path" \
        -monitor "unix:$mon_path,server,nowait" \
        -drive "file=$WORK_ROOTFS_IMG,format=raw,if=ide" \
        -display none \
        -no-reboot >/dev/null 2>&1 &
    qemu_pid=$!

    trap 'kill $qemu_pid >/dev/null 2>&1 || true; wait $qemu_pid >/dev/null 2>&1 || true' EXIT INT TERM

    i=0
    while [ ! -S "$mon_path" ] && [ $i -lt 50 ]; do
        sleep 0.1
        i=$((i + 1))
    done

    if [ ! -S "$mon_path" ]; then
        printf 'FAIL %s: monitor socket not ready\n' "$case_name" >&2
        exit 1
    fi

    sleep 2
    {
        printf 'sendkey slash\n'
        printf 'sendkey p\n'
        printf 'sendkey e\n'
        printf 'sendkey r\n'
        printf 'sendkey s\n'
        printf 'sendkey o\n'
        printf 'sendkey slash\n'
        printf 'sendkey e\n'
        printf 'sendkey l\n'
        printf 'sendkey k\n'
        printf 'sendkey s\n'
        printf 'sendkey slash\n'
        printf 'sendkey b\n'
        printf 'sendkey i\n'
        printf 'sendkey n\n'
        printf 'sendkey slash\n'
        printf 'sendkey s\n'
        printf 'sendkey h\n'
        printf 'sendkey ret\n'
    } | socat - UNIX-CONNECT:"$mon_path" >/dev/null 2>&1 || true

    sleep 2

    {
        printf 'sendkey l\n'
        printf 'sendkey s\n'
        printf 'sendkey ret\n'
    } | socat - UNIX-CONNECT:"$mon_path" >/dev/null 2>&1 || true

    sleep 2
    printf 'quit\n' | socat - UNIX-CONNECT:"$mon_path" >/dev/null 2>&1 || true

    wait $qemu_pid >/dev/null 2>&1 || true
    trap - EXIT INT TERM

    for pattern in "# " "/perso/elks/bin/sh" "ELKS: loading /perso/elks/bin/sh" "bin" "dev" "perso"; do
        if ! rg -q --fixed-strings "$pattern" "$log_path"; then
            printf 'FAIL %s: missing "%s"\n' "$case_name" "$pattern" >&2
            tail -n 160 "$log_path" >&2 || true
            exit 1
        fi
    done

    printf 'PASS %s\n' "$case_name"
fi
