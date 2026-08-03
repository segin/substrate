#!/bin/sh
# init for the crossexec guest: mount the scratch disk, run the probe binary
# it carries, and frame the output so crossexec can pick it out of the serial
# stream the kernel is also writing to.
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p /mnt 2>/dev/null
mount /dev/storage/sata1 /mnt ext2
cd /tmp
cp /mnt/cmd ./cmd 2>/dev/null
chmod +x ./cmd 2>/dev/null
./cmd $(cat /mnt/args 2>/dev/null) < /dev/null > /tmp/out.txt 2>/dev/null
rc=$?
while IFS= read -r line; do
    echo "@@IFFE@@$line"
done < /tmp/out.txt
echo "@@IFFE-RC@@$rc"
# Nothing reaps this init; crossexec kills the VM once it has the status.
while :; do sleep 1000; done
