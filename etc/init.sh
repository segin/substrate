#!/bin/sh
# System initialization script

echo "Substrate initializing..."

# Mount filesystems (Kernel already mounts /proc, /sys, /dev)
# mount proc /proc procfs

# Start getty on console
/sbin/getty /dev/console

exec /bin/sh
