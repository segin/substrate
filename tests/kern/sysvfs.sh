#!/bin/sh
# sysvfs.sh -- the sysv driver on an image from mksysvimg.py.
#
#   sysvfs.sh <device> <a copy of /big, as mksysvimg.py --big writes it>
#
# Run it on both layouts (mksysvimg.py and mksysvimg.py --svr4).
# Mounts the image, checks its size, reads every kind of file on it, and checks that a
# corrupt entry (an inode past the inode table) and a corrupt file (a block
# past the volume) fail instead of returning other data.  Then looks every
# file up again and unmounts: every node the lookups made must have been
# released, or the unmount is refused (EBUSY).
dev=$1
want=$2
fails=0
check() { if [ "$1" = "$2" ]; then echo "ok    $3"; else echo "FAIL  $3 (got '$1')"; fails=$((fails+1)); fi; }

mkdir -p /mnt/sysv
mount "$dev" /mnt/sysv sysv; check $? 0 "mount"
# 512 blocks, the first data block 10: 502 KiB of data blocks.  A Release 4
# superblock (mksysvimg.py --svr4) has s_fsize two bytes further on.
check "$(df -k /mnt/sysv | awk 'NR == 2 { print $2 }')" 502 "the volume's size"
check "$(cat /mnt/sysv/small)" hello "small file"
check "$(cat /mnt/sysv/a/b/c/d/e/target)" deep "deep path"
check "$(ls /mnt/sysv/many | wc -l | tr -d ' ')" 60 "60 entries"
check "$(cat /mnt/sysv/many/f42)" "file 42" "a file among many"
cmp -s /mnt/sysv/big "$want"; check $? 0 "file with an indirect block"
cat /mnt/sysv/badino > /dev/null 2>&1; check $? 1 "entry with an inode past the table fails"
check "$(cat /mnt/sysv/badblk 2>/dev/null | wc -c | tr -d ' ')" 0 "block past the volume reads nothing"
i=0
while [ $i -lt 3 ]; do
    for f in /mnt/sysv/many/*; do cat "$f" > /dev/null; done
    ls -l /mnt/sysv/a/b/c/d/e > /dev/null
    i=$((i+1))
done
umount /mnt/sysv; check $? 0 "unmount (every lookup's node released)"
if [ $fails -eq 0 ]; then echo "sysvfs: PASS"; else echo "sysvfs: FAIL"; fi
