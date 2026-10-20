#!/bin/sh
# ext2immutable.sh -- an immutable ext2 file's attributes do not change.
#
# The guest has no way to set EXT2_IMMUTABLE_FL, so the image is made on
# the host:
#
#   truncate -s 8M imm.img && mkfs.ext2 -q -F imm.img
#   echo hi > immf && debugfs -w -R "write immf immf" imm.img
#   debugfs -w -R "set_inode_field immf flags 0x10" imm.img
#
# and attached as an extra disk.  In the guest:  ext2immutable.sh <device>
#
# ext2_setattr (utimensat, chown) ignored the flag that ext2_chmod and the
# write paths honour, so the times and owner of an immutable file changed.
dev=$1
fails=0
mkdir -p /mnt/imm
mount "$dev" /mnt/imm ext2 || { echo "ext2immutable: cannot mount $dev"; exit 2; }
touch -t 200001010000 /mnt/imm/immf 2>/dev/null && { echo "FAIL  utimes changed an immutable file"; fails=1; }
chown 1:1 /mnt/imm/immf 2>/dev/null && { echo "FAIL  chown changed an immutable file"; fails=1; }
chmod 600 /mnt/imm/immf 2>/dev/null && { echo "FAIL  chmod changed an immutable file"; fails=1; }
umount /mnt/imm
if [ $fails -eq 0 ]; then echo "ext2immutable: PASS"; else echo "ext2immutable: FAIL"; fi
exit $fails
