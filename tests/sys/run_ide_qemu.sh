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
LOG_DIR=${LOG_DIR:-"${TMPDIR:-/tmp}/substrate-ide-qemu"}
ISO_DIR="$LOG_DIR/iso-root"
ISO_IMG="$LOG_DIR/ide-atapi.iso"
DEFAULT_CASES="ide_qemu_pio ide_qemu_dma ide_qemu_atapi ide_qemu_extra"

if [ "$#" -gt 0 ]; then
    CASES="$*"
else
    CASES=${CASES:-$DEFAULT_CASES}
fi

mkdir -p "$LOG_DIR" "$ISO_DIR"

require_file() {
    if [ ! -f "$1" ]; then
        printf 'missing required file: %s\n' "$1" >&2
        exit 1
    fi
}

build_iso() {
    printf 'SUBSTRATE IDE ATAPI TEST\n' > "$ISO_DIR/README.TXT"
    rm -f "$ISO_IMG"
    xorriso -as mkisofs -quiet -J -R -V IDETEST -o "$ISO_IMG" "$ISO_DIR"
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

run_case() {
    case_name=$1
    test_name=$2
    marker=$3
    shift 3

    log="$LOG_DIR/$case_name.log"
    out="$LOG_DIR/$case_name.out"
    rc=0

    rm -f "$log" "$out"

    if ! "$TIMEOUT_BIN" "${QEMU_TIMEOUT}s" \
        "$QEMU" \
        -display none \
        -nodefaults \
        -no-reboot \
        -m 256 \
        -kernel "$KERNEL_ZIMAGE" \
        -initrd "$INITRD_IMG" \
        -accel tcg \
        -icount shift=9 \
        -append "serial_debug console=serial0 root=/dev/storage/ram0 test=$test_name test_halt=1" \
        -serial "file:$log" \
        "$@" >"$out" 2>&1; then
        rc=$?
    fi

    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        printf 'FAIL %s: qemu exited rc=%d\n' "$case_name" "$rc" >&2
        tail -n 80 "$out" >&2 || true
        exit 1
    fi

    found=0
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if [ -f "$log" ] && rg -q --fixed-strings "$marker" "$log"; then
            found=1
            break
        fi
        sleep 0.2
    done

    if [ "$found" -ne 1 ]; then
        printf 'FAIL %s: missing marker "%s"\n' "$case_name" "$marker" >&2
        tail -n 120 "$log" >&2 || true
        exit 1
    fi

    printf 'PASS %s\n' "$case_name"
}

require_file "$KERNEL_ZIMAGE"
require_file "$INITRD_IMG"

build_iso

if want_case ide_qemu_pio; then
    ATA_IMG="$LOG_DIR/ide_qemu_pio.d/ide-primary.img"
    mkdir -p "$(dirname "$ATA_IMG")"
    truncate -s 16M "$ATA_IMG"
    run_case ide_qemu_pio ide_qemu_pio "PASS: ide_qemu_pio round-trip" \
        -machine isapc \
        -drive "file=$ATA_IMG,format=raw,if=ide,index=0"
fi

if want_case ide_qemu_dma; then
    ATA_IMG="$LOG_DIR/ide_qemu_dma.d/ide-primary.img"
    mkdir -p "$(dirname "$ATA_IMG")"
    truncate -s 16M "$ATA_IMG"
    run_case ide_qemu_dma ide_qemu_dma "PASS: ide_qemu_dma round-trip" \
        -device piix4-ide,id=idepci \
        -drive "if=none,file=$ATA_IMG,format=raw,id=primary" \
        -device "ide-hd,bus=idepci.0,unit=0,drive=primary"
fi

if want_case ide_qemu_atapi; then
    ATA_IMG="$LOG_DIR/ide_qemu_atapi.d/ide-primary.img"
    mkdir -p "$(dirname "$ATA_IMG")"
    truncate -s 16M "$ATA_IMG"
    run_case ide_qemu_atapi ide_qemu_atapi "PASS: ide_qemu_atapi capacity/toc/read" \
        -device piix4-ide,id=idepci \
        -drive "if=none,file=$ATA_IMG,format=raw,id=primary" \
        -device "ide-hd,bus=idepci.0,unit=0,drive=primary" \
        -drive "if=none,file=$ISO_IMG,format=raw,media=cdrom,id=cd0" \
        -device "ide-cd,bus=idepci.1,unit=0,drive=cd0"
fi

if want_case ide_qemu_extra; then
    ATA_IMG="$LOG_DIR/ide_qemu_extra.d/ide-primary.img"
    ATA_EXTRA0_IMG="$LOG_DIR/ide_qemu_extra.d/ide-tertiary.img"
    ATA_EXTRA1_IMG="$LOG_DIR/ide_qemu_extra.d/ide-quaternary.img"
    mkdir -p "$(dirname "$ATA_IMG")"
    truncate -s 16M "$ATA_IMG"
    truncate -s 16M "$ATA_EXTRA0_IMG"
    truncate -s 16M "$ATA_EXTRA1_IMG"
    run_case ide_qemu_extra ide_qemu_extra "PASS: ide_qemu_extra tertiary/quaternary detected" \
        -device piix4-ide,id=idepci0 \
        -drive "if=none,file=$ATA_IMG,format=raw,id=primary" \
        -device "ide-hd,bus=idepci0.0,unit=0,drive=primary" \
        -device "isa-ide,iobase=0x1e8,iobase2=0x3ee,irq=11,id=extraide0" \
        -drive "if=none,file=$ATA_EXTRA0_IMG,format=raw,id=extra0" \
        -device "ide-hd,bus=extraide0.0,unit=0,drive=extra0" \
        -device "isa-ide,iobase=0x168,iobase2=0x36e,irq=10,id=extraide1" \
        -drive "if=none,file=$ATA_EXTRA1_IMG,format=raw,id=extra1" \
        -device "ide-hd,bus=extraide1.0,unit=0,drive=extra1"
fi

printf 'ide-qemu: ok\n'
