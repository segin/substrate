#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SYS_DIR="$ROOT_DIR/sys"
KERNEL_BIN="$SYS_DIR/kernel.bin"
ROOT_IMG="$ROOT_DIR/root.img"
LOG_DIR="${TMPDIR:-/tmp}/substrate-boot-matrix"
MEMORY_SET="16 32 128 1024 4096"
PASS_MARKER="=== Test Complete ===" # Userspace completion marker from native_test

mkdir -p "$LOG_DIR"

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "boot-matrix: qemu-system-i386 not found" >&2
    exit 1
fi

if [ ! -f "$KERNEL_BIN" ]; then
    echo "boot-matrix: missing kernel image: $KERNEL_BIN" >&2
    exit 1
fi

if [ ! -f "$ROOT_IMG" ]; then
    echo "boot-matrix: missing root image: $ROOT_IMG" >&2
    exit 1
fi

# Check if init=/bin/native_test exists in root image
if ! command -v debugfs >/dev/null 2>&1; then
    echo "boot-matrix: debugfs not found (ext2-tools package required)" >&2
    exit 1
fi

if ! debugfs -R "stat /bin/native_test" "$ROOT_IMG" >/dev/null 2>&1; then
    echo "boot-matrix: /bin/native_test not found in root image" >&2
    echo "boot-matrix: image is stale or missing userland binaries" >&2
    echo "boot-matrix: rebuild with: make -C bin native_test && ./build-rootfs.sh --dist --image" >&2
    exit 1
fi

for mem in $MEMORY_SET; do
    log="$LOG_DIR/boot-${mem}m.log"
    out="$LOG_DIR/qemu-${mem}m.out"
    rm -f "$log" "$out"

    echo "boot-matrix: testing ${mem}MB"
    rc=0
    if ! (
        cd "$SYS_DIR"
        timeout 45s qemu-system-i386 \
            -display none \
            -snapshot \
            -no-reboot \
            -m "$mem" \
            -kernel kernel.bin \
            -accel tcg \
            -icount shift=9 \
            -smp 1 \
            -append "serial_debug root=/dev/storage/ide0 init=/bin/native_test" \
            -serial "file:$log" \
            -hda ../root.img
    ) >"$out" 2>&1; then
        rc=$?
    fi

    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        echo "boot-matrix: qemu failed for ${mem}MB" >&2
        tail -n 40 "$out" >&2 || true
        exit 1
    fi

    if ! tail -n 1000 "$log" | grep -q "$PASS_MARKER"; then
        echo "boot-matrix: kernel did not reach userspace for ${mem}MB" >&2
        echo "boot-matrix: looking for: '$PASS_MARKER'" >&2
        echo "boot-matrix: last 40 lines:" >&2
        tail -n 40 "$log" >&2 || true
        exit 1
    fi
done

echo "boot-matrix: ok"
