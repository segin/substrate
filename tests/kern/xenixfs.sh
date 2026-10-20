#!/bin/sh
# xenixfs.sh -- the sysv driver mounts a real Xenix filesystem.
#
#   xenixfs.sh <device>
#
# NEEDS AN EXTERNAL ASSET: the "N1" image of the Xenix/286 2.3 install
# media (a raw 1.2 MB Xenix filesystem, ~/Downloads/286/rts/n1.img on the
# development host; see tools/xenix/README.md), attached as the device.
#
# The driver looked for a Xenix magic no disk carries (0x012FF7B5, at the
# System V superblock's place); a Xenix superblock is at byte 1024 with
# magic 0x2B5544 at +0x3F8.  So no real Xenix volume mounted.
dev=$1
fails=0
check() { if [ "$1" = "$2" ]; then echo "ok    $3"; else echo "FAIL  $3 (got '$1')"; fails=$((fails+1)); fi; }

mkdir -p /mnt/xenix
mount "$dev" /mnt/xenix sysv; check $? 0 "mount"
check "$(ls /mnt/xenix/bin | grep -c '^cpio$')" 1 "/bin/cpio is there"
check "$(head -1 /mnt/xenix/profile.hd | cut -c1-14)" "#	@(#) prof.hd" "a text file reads back"
umount /mnt/xenix; check $? 0 "unmount"
if [ $fails -eq 0 ]; then echo "xenixfs: PASS"; else echo "xenixfs: FAIL"; fi
