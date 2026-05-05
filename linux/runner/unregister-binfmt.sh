#!/bin/sh
set -eu

mountpoint="/proc/sys/fs/binfmt_misc"

for name in substrate-i386-exec substrate-i386-dyn; do
	if [ -e "$mountpoint/$name" ]; then
		printf '%s\n' -1 > "$mountpoint/$name"
		echo "unregistered $name"
	fi
done
