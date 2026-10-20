#!/bin/sh
# 9pmount.sh -- mount and unmount a 9P share served by QEMU.
#
# Boot with a host directory exported over virtio-9p, e.g.
#   -virtfs local,path=<dir>,mount_tag=host0,security_model=none
# and run in the guest.  The mount negotiates 9P2000.L (msize and version
# from Rversion) and attaches the root; both used to fail against QEMU --
# the .L Tattach lacked n_uname, and the polling driver left the device's
# interrupt asserted -- so the mount never returned.
mkdir -p /mnt/9p
mount none /mnt/9p 9p || { echo "9pmount: FAIL (mount)"; exit 1; }
umount /mnt/9p || { echo "9pmount: FAIL (umount)"; exit 1; }
echo "9pmount: PASS"
