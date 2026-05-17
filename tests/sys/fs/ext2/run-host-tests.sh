#!/bin/bash
# tests/sys/fs/ext2/run-host-tests.sh
#
# Automated ext2/3/4 driver tests.
#
# For each scenario:
#   1. Build a tiny ext{2,3,4} image with mkfs.
#   2. Generate /etc/fstest.conf into the substrate rootfs.img telling
#      its /etc/rc.d/99-fstest hook what to mount and what to check.
#   3. Boot QEMU with the test image as a second drive, capture serial.
#   4. Grep the serial transcript for expected FSTEST: lines.
#
# Tests reuse the production rootfs.img — we splice fstest.conf in via
# debugfs since populating with mount/loop needs root.

set -eu

TOP="$(cd "$(dirname "$0")"/../../../.. && pwd)"
WORK="${WORK:-/tmp/substrate-ext2-tests}"
QEMU="${QEMU:-qemu-system-i386}"
IMG_SRC="$TOP/rootfs.img"

mkdir -p "$WORK"

PASS=0
FAIL=0

# --- helpers -------------------------------------------------------

mkimg() {
    # mkimg <name> <fs> <mkfs-opts...>  → echoes path
    local name=$1 fs=$2; shift 2
    local out="$WORK/$name.img"
    truncate -s 16M "$out"
    case "$fs" in
        ext2) mkfs.ext2 -q -F -O ^has_journal -t ext2 "$@" "$out" ;;
        ext3) mkfs.ext3 -q -F -t ext3 "$@" "$out" ;;
        ext4) mkfs.ext4 -q -F -t ext4 "$@" "$out" ;;
        *) echo "unknown fs $fs" >&2; exit 1 ;;
    esac
    echo "$out"
}

# Splice a one-shot /etc/fstest.conf into a copy of rootfs.img using
# debugfs.  Returns the path to the modified image so we don't
# clobber the production one.
prep_rootfs() {
    # prep_rootfs <name> <conf-content>
    local name=$1 conf=$2
    local out="$WORK/rootfs-$name.img"
    cp "$IMG_SRC" "$out"
    local cfile="$WORK/$name.fstest.conf"
    printf '%s\n' "$conf" > "$cfile"
    debugfs -w "$out" -R "rm /etc/fstest.conf" >/dev/null 2>&1 || true
    debugfs -w "$out" -R "write $cfile /etc/fstest.conf" >/dev/null
    echo "$out"
}

# Boot substrate with extra disk + serial→file.  Mirrors the user's
# run-networking.sh approach: load the multiboot kernel directly via
# -kernel and attach the rootfs as SATA AHCI (drive0) and the test
# disk as second SATA (drive1).  This bypasses the ext2-boot
# bootloader (which can't always find /vmunix when invoked via
# qemu-system-i386 -drive if=ide).
boot_with() {
    # boot_with <rootfs> <test-disk> <log> <timeout>
    local root=$1 disk=$2 log=$3 t=${4:-40}
    timeout "$t" "$QEMU" -cpu pentium2 \
        -m 256 -display none -no-reboot \
        -drive file="$root",format=raw,if=none,id=drive0 \
        -drive file="$disk",format=raw,if=none,id=drive1 \
        -device ich9-ahci,id=sata0 \
        -device ide-hd,bus=sata0.0,unit=0,drive=drive0 \
        -device ide-hd,bus=sata0.1,unit=0,drive=drive1 \
        -kernel "$TOP/sys/kernel.bin" \
        -append "root=/dev/storage/sata0 serial_debug" \
        -serial file:"$log" >/dev/null 2>&1 || true
}

assert_log() {
    # assert_log <log> <pattern> <name>
    local log=$1 pat=$2 name=$3
    if grep -qE "$pat" "$log"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
        return 0
    else
        echo "  FAIL: $name (no match for /$pat/)"
        echo "    --- tail ---"
        tail -25 "$log" | sed 's/^/    /'
        echo "    --- end ---"
        FAIL=$((FAIL + 1))
        return 1
    fi
}

# --- scenarios -----------------------------------------------------

t_mount_ext2() {
    echo "==> mount-ext2"
    local img; img=$(mkimg t-ext2 ext2)
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='echo CHECK_OK'"
    local rfs; rfs=$(prep_rootfs t-ext2 "$conf")
    local log="$WORK/t-ext2.log"
    boot_with "$rfs" "$img" "$log" 25
    assert_log "$log" "FSTEST: mount OK"   "ext2 mount succeeds"
    assert_log "$log" "FSTEST:   CHECK_OK" "ext2 check ran"
}

t_mount_ext4_extents() {
    echo "==> mount-ext4-extents"
    # mkfs.ext4's defaults flip on 64bit + metadata_csum_seed which we
    # don't support yet — disable so the only ext4-distinguishing
    # feature left is `extent`.  The test verifies the extent reader
    # by demanding a successful mount + ls.
    local img; img=$(mkimg t-ext4 ext4 -O '^64bit,^metadata_csum')
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='echo CHECK_OK'"
    local rfs; rfs=$(prep_rootfs t-ext4 "$conf")
    local log="$WORK/t-ext4.log"
    boot_with "$rfs" "$img" "$log" 25
    assert_log "$log" "ext4 extents enabled" "ext4 extents flag logged"
    assert_log "$log" "FSTEST: mount OK"     "ext4 mount succeeds"
}

t_refuse_64bit() {
    echo "==> refuse-ext4-64bit"
    local img; img=$(mkimg t-64bit ext4 -O 64bit)
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check=echo SHOULD_NOT_GET_HERE"
    local rfs; rfs=$(prep_rootfs t-64bit "$conf")
    local log="$WORK/t-64bit.log"
    boot_with "$rfs" "$img" "$log" 25
    assert_log "$log" "unsupported INCOMPAT"   "INCOMPAT refusal logged"
    assert_log "$log" "64-bit block addresses" "64-bit explained"
    assert_log "$log" "FSTEST: mount FAIL"     "userland sees mount failure"
}

# --- main ----------------------------------------------------------

if ! command -v mkfs.ext4 >/dev/null; then
    echo "skip: mkfs.ext4 not in PATH"
    exit 0
fi
if ! command -v debugfs >/dev/null; then
    echo "skip: debugfs not in PATH (install e2fsprogs)"
    exit 0
fi
if [ ! -f "$IMG_SRC" ]; then
    echo "skip: $IMG_SRC not built — run ./build-rootfs.sh --image first"
    exit 0
fi

t_mount_ext2
t_mount_ext4_extents
t_refuse_64bit

echo
echo "Total: $((PASS + FAIL)) — $PASS passed, $FAIL failed"
exit $FAIL
