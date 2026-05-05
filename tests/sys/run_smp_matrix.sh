#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SYS_DIR="$ROOT_DIR/sys"
KERNEL_BIN="$SYS_DIR/kernel.bin"
ROOT_IMG="$ROOT_DIR/root.img"
DISK_IMG="${HOME}/20Mb Hard Disk.img"
LOG_DIR="${TMPDIR:-/tmp}/substrate-smp-matrix"
CPU_SET="2 4 8"
PASS_MARKER="execve: Final check"

mkdir -p "$LOG_DIR"

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "smp-matrix: qemu-system-i386 not found" >&2
    exit 1
fi

if [ ! -f "$KERNEL_BIN" ]; then
    echo "smp-matrix: missing kernel image: $KERNEL_BIN" >&2
    exit 1
fi

if [ ! -f "$ROOT_IMG" ]; then
    echo "smp-matrix: missing root image: $ROOT_IMG" >&2
    exit 1
fi

if [ ! -f "$DISK_IMG" ]; then
    echo "smp-matrix: missing secondary disk image: $DISK_IMG" >&2
    exit 1
fi

for cpus in $CPU_SET; do
    log="$LOG_DIR/smp-${cpus}.log"
    out="$LOG_DIR/qemu-smp-${cpus}.out"
    rm -f "$log" "$out"

    echo "smp-matrix: testing ${cpus} CPU(s)"
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
            -smp "$cpus" \
            -append "serial_debug root=/dev/storage/ide0 init=/bin/native_test" \
            -serial "file:$log" \
            -hda ../root.img \
            -hdb "$DISK_IMG"
    ) >"$out" 2>&1; then
        rc=$?
    fi

    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        echo "smp-matrix: qemu failed for ${cpus} CPU(s)" >&2
        tail -n 40 "$out" >&2 || true
        exit 1
    fi

    if ! grep -q "$PASS_MARKER" "$log"; then
        echo "smp-matrix: kernel did not reach userspace for ${cpus} CPU(s)" >&2
        tail -n 80 "$log" >&2 || true
        exit 1
    fi

    aps=$((cpus - 1))
    if ! grep -q "${aps} AP(s) online." "$log" || ! grep -q "${cpus} CPU(s)!" "$log"; then
        echo "smp-matrix: missing final SMP summary for ${cpus} CPU(s)" >&2
        tail -n 120 "$log" >&2 || true
        exit 1
    fi

    if [ "$(grep -c '^KMAIN START$' "$log")" -ne 1 ]; then
        echo "smp-matrix: AP bootstrap re-entered kernel start for ${cpus} CPU(s)" >&2
        tail -n 120 "$log" >&2 || true
        exit 1
    fi
done

echo "smp-matrix: ok"
