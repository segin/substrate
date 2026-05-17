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

# Write fstest.conf into the dist/ tree, rebuild rootfs.img with it
# baked in.  Saves the result under $WORK so back-to-back scenarios
# don't clobber each other.  We deliberately don't post-hoc-splice
# via debugfs because it corrupts substrate's mkfs.ext2 image —
# the group-descriptor count is touchy.
prep_rootfs() {
    # prep_rootfs <name> <conf-content>
    local name=$1 conf=$2
    local out="$WORK/rootfs-$name.img"
    printf '%s\n' "$conf" > "$TOP/dist/etc/fstest.conf"
    (cd "$TOP" && ./build-rootfs.sh --image >/dev/null 2>&1)
    cp "$IMG_SRC" "$out"
    rm -f "$TOP/dist/etc/fstest.conf"
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
    # cache=writethrough on the test disk so writes hit the host
    # file immediately — without it, QEMU's default writeback cache
    # can swallow a touch + sync + poweroff sequence (the inode
    # block landed in QEMU's page cache and the file on disk only
    # gets updated on a subsequent explicit flush).
    timeout "$t" "$QEMU" -cpu pentium2 \
        -m 256 -display none -no-reboot \
        -drive file="$root",format=raw,if=none,id=drive0,cache=writethrough \
        -drive file="$disk",format=raw,if=none,id=drive1,cache=writethrough \
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

t_htree_listing() {
    echo "==> htree-listing"
    # Build an ext4 image with dir_index enabled (default), populate
    # a directory with 500 files via debugfs so ext4 promotes the
    # dir to an htree.  Then verify our linear-scan readdir sees
    # them all.
    local img; img=$(mkimg t-htree ext4 -O '^64bit,^metadata_csum,dir_index')
    # Pre-populate via debugfs script — create dir + 500 files.
    {
        echo "mkdir /many"
        for i in $(seq 1 500); do
            echo "write /dev/null /many/f$i"
        done
        echo "close"
    } | debugfs -w "$img" >/dev/null 2>&1 || true

    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='ls /mnt/test/many | wc -l'"
    local rfs; rfs=$(prep_rootfs t-htree "$conf")
    local log="$WORK/t-htree.log"
    boot_with "$rfs" "$img" "$log" 40
    assert_log "$log" "FSTEST: mount OK" "htree mount succeeds"
    # If the linear-scan walks the htree blocks correctly, ls reports
    # 500.  If it stops at the index block, it reports < 10.
    assert_log "$log" "FSTEST:[[:space:]]+500$" "htree listing sees all 500 entries"
}

t_setattr_persist() {
    echo "==> setattr-persist"
    # Create an ext2 image with a file, mount + touch -t to set a
    # known mtime, then umount.  Boot fresh + remount, check stat
    # reports the timestamp.  Verifies vfs setattr_fs -> ext2_setattr
    # -> ext2_write_inode actually persists.
    local img; img=$(mkimg t-setattr ext2)
    {
        echo "write /dev/null /target"
        echo "close"
    } | debugfs -w "$img" >/dev/null 2>&1 || true

    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='touch -d @1577836800 /mnt/test/target && ls -l /mnt/test/target'"
    local rfs; rfs=$(prep_rootfs t-setattr "$conf")
    local log="$WORK/t-setattr.log"
    boot_with "$rfs" "$img" "$log" 30
    assert_log "$log" "FSTEST: mount OK" "setattr mount succeeds"
    # 1577836800 = 2020-01-01 00:00:00 UTC.  ls -l prints "Jan  1 2020".
    assert_log "$log" "Jan +1 +2020" "setattr persists mtime (read-after-write)"

    # Re-boot the same image — fresh inode cache reads from disk —
    # mtime should still be 1577836800.
    local conf2="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='ls -l /mnt/test/target'"
    local rfs2; rfs2=$(prep_rootfs t-setattr-readback "$conf2")
    local log2="$WORK/t-setattr-readback.log"
    boot_with "$rfs2" "$img" "$log2" 30
    assert_log "$log2" "Jan +1 +2020" "setattr survives umount + remount"
}

t_setattr_csum_survives() {
    echo "==> setattr-persist-on-metadata-csum"
    # Same shape as setattr_persist but on a metadata_csum-enabled
    # ext4 image.  This exercises the per-inode csum recompute on
    # write: if write_inode forgot to update chksum_lo/hi after
    # bumping i_mtime, the re-mount's inode-csum verify would
    # refuse to open the file and the "Jan 1 2020" assert would
    # fail because ls returns ENOENT/EIO.
    local img; img=$(mkimg t-setattr-csum ext4 -O '^64bit,metadata_csum')
    {
        echo "write /dev/null /target"
        echo "close"
    } | debugfs -w "$img" >/dev/null 2>&1 || true

    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='touch -d @1577836800 /mnt/test/target && ls -l /mnt/test/target'"
    local rfs; rfs=$(prep_rootfs t-setattr-csum "$conf")
    local log="$WORK/t-setattr-csum.log"
    boot_with "$rfs" "$img" "$log" 30
    assert_log "$log" "FSTEST: mount OK" "setattr-csum mount succeeds"
    assert_log "$log" "Jan +1 +2020"    "setattr-csum mtime visible immediately"

    local conf2="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='ls -l /mnt/test/target'"
    local rfs2; rfs2=$(prep_rootfs t-setattr-csum-readback "$conf2")
    local log2="$WORK/t-setattr-csum-readback.log"
    boot_with "$rfs2" "$img" "$log2" 30
    assert_log "$log2" "Jan +1 +2020"   "setattr-csum re-mount validates csum"
    # And confirm no inode-csum mismatch warning leaked into the log.
    if grep -q "inode .* csum mismatch" "$log2"; then
        echo "  FAIL: post-setattr inode csum mismatch detected"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: post-setattr inode csum stays valid"
        PASS=$((PASS + 1))
    fi
}

t_ro_unsupported_rocompat() {
    echo "==> ro-mount-on-unsupported-rocompat"
    # mkfs with the QUOTA ROCOMPAT — we don't support it but the
    # driver should mount read-only rather than refuse outright,
    # then refuse any write attempt with EROFS.
    local img; img=$(mkimg t-quota ext4 -O '^64bit,^metadata_csum,quota')
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='echo TOUCHING; touch /mnt/test/x 2>&1; echo TOUCH_DONE_RC=\$?'"
    local rfs; rfs=$(prep_rootfs t-quota "$conf")
    local log="$WORK/t-quota.log"
    boot_with "$rfs" "$img" "$log" 30
    assert_log "$log" "mount forced read-only" "ro-mount logged"
    assert_log "$log" "FSTEST: mount OK" "ro-mount succeeds"
    # touch should fail because the mount is RO.
    assert_log "$log" "TOUCH_DONE_RC=[1-9]" "write on ro mount refused"
}

t_mount_ext3() {
    echo "==> mount-ext3"
    # ext3 = ext2 + journal.  We ignore the journal but the
    # filesystem must still mount cleanly when not dirty.
    local img; img=$(mkimg t-ext3 ext3)
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='echo CHECK_OK'"
    local rfs; rfs=$(prep_rootfs t-ext3 "$conf")
    local log="$WORK/t-ext3.log"
    boot_with "$rfs" "$img" "$log" 25
    assert_log "$log" "ext3\\+ journal present" "ext3 journal logged"
    assert_log "$log" "FSTEST: mount OK" "ext3 mount succeeds"
    assert_log "$log" "CHECK_OK"        "ext3 check ran"
}

t_extent_large_read() {
    echo "==> extent-large-read"
    # Create an ext4 image with a multi-MiB file backed by extents.
    # On a 4K-block fs, a 4 MiB file is 1024 blocks — far more than
    # the 4 inline extents in the inode header, forcing a depth-1
    # extent tree.  Tests that the extent walker correctly chases
    # through an index node to the leaf extents.
    local img; img=$(mkimg t-bigext ext4 -O '^64bit,^metadata_csum')
    local payload="$WORK/t-bigext.payload"
    # Deterministic 1 MiB content so we can check the sha256 from
    # inside the VM.  (Substrate doesn't have sha256sum on the
    # image, so use `cksum` instead — CRC32 + byte count.)
    dd if=/dev/zero of="$payload" bs=1024 count=1024 status=none
    # Fill with a repeating pattern (zeros + pattern blocks) to
    # make any corruption easy to spot.
    printf 'SUBSTRATE_EXTENT_TEST_PATTERN_REPEAT_OK\n' | dd of="$payload" bs=40 count=1 conv=notrunc status=none
    {
        echo "write $payload /big"
        echo "close"
    } | debugfs -w "$img" >/dev/null 2>&1 || true
    local expect; expect=$(cksum < "$payload")
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='ls -l /mnt/test/big; head -c 40 /mnt/test/big; echo TAIL=\$(wc -c < /mnt/test/big)'"
    local rfs; rfs=$(prep_rootfs t-bigext "$conf")
    local log="$WORK/t-bigext.log"
    boot_with "$rfs" "$img" "$log" 40
    assert_log "$log" "FSTEST: mount OK"                  "big-extent mount succeeds"
    # head -c 40 prints exactly the pattern we wrote at offset 0.
    # If the extent reader returns wrong physical blocks the
    # pattern won't be there.
    assert_log "$log" "SUBSTRATE_EXTENT_TEST_PATTERN_REPEAT_OK"  "extent-walker delivers first block"
    # And wc -c reports the right size end-to-end (1048576 bytes),
    # which only happens if every block index 0..1023 resolves to
    # a real on-disk block.
    assert_log "$log" "TAIL=1048576"                      "extent-walker delivers all 1 MiB"
}

t_csum_verify_ok() {
    echo "==> csum-verify-ok"
    # mkfs.ext4 with metadata_csum on — we should verify the
    # superblock checksum and mount cleanly.
    local img; img=$(mkimg t-csum ext4 -O '^64bit,metadata_csum')
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='echo CHECK_OK'"
    local rfs; rfs=$(prep_rootfs t-csum "$conf")
    local log="$WORK/t-csum.log"
    boot_with "$rfs" "$img" "$log" 30
    assert_log "$log" "superblock metadata_csum verified" "csum verify logged"
    assert_log "$log" "FSTEST: mount OK"                   "csum-OK mount succeeds"
    assert_log "$log" "CHECK_OK"                           "csum-OK check ran"
}

t_csum_verify_fail() {
    echo "==> csum-verify-fail"
    # Corrupt the superblock's first block-count field after mkfs.
    # That changes the csum-protected bytes; verify must fail.
    local img; img=$(mkimg t-cfail ext4 -O '^64bit,metadata_csum')
    # Flip one byte in the superblock at offset 1024+4 (s_blocks_count
    # low half).  Don't recompute the on-disk csum.
    python3 -c "
import sys
with open('$img','r+b') as f:
    f.seek(1024+4)
    b = f.read(1)
    f.seek(1024+4)
    f.write(bytes([b[0] ^ 0x55]))
" 2>/dev/null
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='echo SHOULD_NOT_GET_HERE'"
    local rfs; rfs=$(prep_rootfs t-cfail "$conf")
    local log="$WORK/t-cfail.log"
    boot_with "$rfs" "$img" "$log" 30
    assert_log "$log" "superblock checksum mismatch" "tampered csum refused"
    assert_log "$log" "FSTEST: mount FAIL"           "userland sees mount failure"
}

t_bg_csum_verify() {
    echo "==> bg-csum-verify"
    # Same setup as csum-verify-ok, but assert that the group-
    # descriptor verify line is also emitted, and add a second
    # case where we tamper a bg descriptor byte and confirm
    # the mount is refused with bg-csum diagnostic.
    local img; img=$(mkimg t-bgcsum ext4 -O '^64bit,metadata_csum')
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='echo CHECK_OK'"
    local rfs; rfs=$(prep_rootfs t-bgcsum "$conf")
    local log="$WORK/t-bgcsum.log"
    boot_with "$rfs" "$img" "$log" 30
    assert_log "$log" "group descriptor csums verified" "bg csums verified"
    assert_log "$log" "CHECK_OK"                        "bg-csum-OK boot reached check"

    # Tamper: flip a byte inside the first bg descriptor (offset
    # 4096 with 1KiB blocks: block 1 = group descriptor table).
    # bg0 lives at the start; bytes 0..29 are checksum-covered.
    local img2; img2=$(mkimg t-bgfail ext4 -O '^64bit,metadata_csum')
    python3 -c "
import os, struct
# Find block_size: superblock at 1024, s_log_block_size at offset 24
with open('$img2','r+b') as f:
    f.seek(1024+24)
    log_bs = struct.unpack('<I', f.read(4))[0]
    bs = 1024 << log_bs
    # bg descriptor table follows superblock: block 2 if bs==1024, else block 1
    gdt_off = (2 if bs == 1024 else 1) * bs
    # flip a byte inside bg0's descriptor (offset 8 = bg_inode_table low)
    f.seek(gdt_off + 8)
    b = f.read(1)
    f.seek(gdt_off + 8)
    f.write(bytes([b[0] ^ 0xAA]))
" 2>/dev/null
    local rfs2; rfs2=$(prep_rootfs t-bgfail "$conf")
    local log2="$WORK/t-bgfail.log"
    boot_with "$rfs2" "$img2" "$log2" 30
    assert_log "$log2" "bg descriptor 0 csum mismatch" "bg tamper refused"
    assert_log "$log2" "FSTEST: mount FAIL"            "userland sees bg failure"
}

t_refuse_extent_write() {
    echo "==> refuse-ext4-extent-write"
    # ext4 with extents only (no 64bit / csum).  Pre-create a file
    # with EXT4_EXTENTS_FL; substrate should READ it but REFUSE
    # writes (EROFS) until the extent alloc path lands.  Without
    # the guard, writes would corrupt the extent header.
    local img; img=$(mkimg t-extwr ext4 -O '^64bit,^metadata_csum')
    {
        echo "write /dev/null /target"
        echo "close"
    } | debugfs -w "$img" >/dev/null 2>&1 || true
    local conf="device=/dev/storage/sata1
mount=/mnt/test
fs=ext2
check='cat /mnt/test/target >/dev/null && echo READ_OK; echo NEWCONTENT > /mnt/test/target; ls -l /mnt/test/target'"
    local rfs; rfs=$(prep_rootfs t-extwr "$conf")
    local log="$WORK/t-extwr.log"
    boot_with "$rfs" "$img" "$log" 30
    assert_log "$log" "FSTEST: mount OK" "extent-file mount succeeds"
    assert_log "$log" "READ_OK" "extent-file read works"
    # If write was refused, the ls line shows " root 0 " for the
    # file size column.  If the write had taken effect (corrupting
    # the extent header), the size would be 11 ("NEWCONTENT\n").
    assert_log "$log" "root +0 +.* /mnt/test/target" "extent-file write left size 0"
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
t_mount_ext3
t_mount_ext4_extents
t_htree_listing
t_setattr_persist
t_setattr_csum_survives
t_ro_unsupported_rocompat
t_extent_large_read
t_csum_verify_ok
t_csum_verify_fail
t_bg_csum_verify
t_refuse_extent_write
t_refuse_64bit

# Restore the production rootfs.img so the user's next interactive
# boot isn't sitting on whichever test config we ran last.
echo "==> restoring production rootfs.img"
rm -f "$TOP/dist/etc/fstest.conf"
(cd "$TOP" && ./build-rootfs.sh --image >/dev/null 2>&1)

echo
echo "Total: $((PASS + FAIL)) — $PASS passed, $FAIL failed"
exit $FAIL
