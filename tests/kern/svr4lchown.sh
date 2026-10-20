#!/bin/sh
# svr4lchown.sh -- SVR4's lchown (chown -h) changes the link, not EFAULT.
#
#   svr4lchown.sh <device holding the SVR4 image>
#
# NEEDS AN EXTERNAL ASSET: an Intel SVR4.0 root (svr4.img, built from the
# vendor floppies; see tests/perso/svr4/README.md), attached as the device.
#
# svr4_sys_lchown handed its kernel copy of the path to sys_lchown, which
# copies the path in from user memory again: every call failed with
# EFAULT ("Bad address").
dev=$1
mkdir -p /perso/svr4
mount "$dev" /perso/svr4 ext2 || exit 2
mkdir -p /perso/svr4/tmp
rm -f /perso/svr4/tmp/lchown.target /perso/svr4/tmp/lchown.lnk
echo x > /perso/svr4/tmp/lchown.target
ln -s lchown.target /perso/svr4/tmp/lchown.lnk
/perso/svr4/usr/bin/chown -h 7 /tmp/lchown.lnk
rc=$?
link_uid=$(ls -ln /perso/svr4/tmp/lchown.lnk | awk '{ print $3 }')
target_uid=$(ls -ln /perso/svr4/tmp/lchown.target | awk '{ print $3 }')
echo "chown -h rc=$rc link uid=$link_uid target uid=$target_uid"
if [ $rc -eq 0 ] && [ "$link_uid" = 7 ] && [ "$target_uid" = 0 ]; then
    echo "svr4lchown: PASS"
else
    echo "svr4lchown: FAIL"
fi
