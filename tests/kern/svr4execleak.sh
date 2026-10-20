#!/bin/sh
# svr4execleak.sh -- an SVR4 program's exec frees the kernel's copies of
# its path, argv and envp.
#
#   svr4execleak.sh <device holding the SVR4 image>
#
# NEEDS AN EXTERNAL ASSET: an Intel SVR4.0 root (svr4.img, built from the
# vendor floppies; see the SVR4 notes in tests/perso/svr4/README.md),
# attached as the given device.
#
# The SVR4 and Xenix execve wrappers freed those copies after kern_execve
# returned, which a successful exec never does: each exec an SVR4 shell
# made lost a few hundred bytes.  Warms the caches the first few hundred
# execs fill, then runs 800 more from the SVR4 sh and compares the bytes
# live in the kmalloc zones (/proc/uma) before and after.  The old kernel
# grows by about 320 KiB; a reading also carries up to ~45 KiB that does
# not grow with the run (processes still being reaped, caches), so the
# test allows 128 KiB.
dev=$1
mkdir -p /perso/svr4
mount "$dev" /perso/svr4 ext2 || exit 2
loop() {
    /perso/svr4/sbin/sh -c "i=0; while [ \$i -lt $1 ]; do /usr/bin/true; i=\`/usr/bin/expr \$i + 1\`; done"
}
# Bytes in use in the kmem-* zones: size times items out.
kmem() {
    awk '/^kmem-/ { split($2, s, "="); split($5, c, "="); t += s[2] * c[2] }
         END { print t }' /proc/uma
}
# Caches fill over the first ~600 execs (about 37 KiB) and then hold; warm
# them, then measure 800 more.  A pause before each reading lets exited
# processes be reaped.
loop 200
loop 200
loop 200
sleep 1
before=$(kmem)
loop 200
loop 200
sleep 1
after=$(kmem)
grown=$((after - before))
echo "kmalloc grew ${grown} bytes over 800 execs"
if [ "$grown" -lt 131072 ]; then echo "svr4execleak: PASS"; else echo "svr4execleak: FAIL"; fi
