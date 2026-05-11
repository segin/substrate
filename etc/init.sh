#!/bin/sh
# /etc/init.sh — runs as PID 1.
#
# Spawns getty on /dev/tty2 and /dev/tty3 in the background, then
# hands over to the console getty in the foreground.  When the
# console getty's child (login → shell) eventually exits, we exit
# too — for a real init we'd want to respawn each line in a loop,
# but until init learns proper supervision this is enough to get
# multiple login terminals up.

echo "Substrate initializing..."

# Background getty on the auxiliary VT lines.
/sbin/getty /dev/tty2 &
/sbin/getty /dev/tty3 &

# Foreground getty on the serial / first VT line.
exec /sbin/getty /dev/console
