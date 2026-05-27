#!/bin/sh
# /root/x.sh — user's standard X session launcher with reproducer.
#
# Tracked here under etc/ so it follows the repo; installed to /root/
# at image-build time.
#
# Flags:
#   -ac    disable host-based access control (substrate has no xauth)
#   -retro gray-stipple root background (matches "classic" X look)
#   -zap   Ctrl+Alt+Backspace terminates the server
#   vt1    take VT 1
#   :0     advertise as display :0
#
# Reproducer: -listen tcp removed (AF_UNIX only).  After Xfbdev
# starts, a backgrounded shell waits 3 s and launches xterm against
# the AF_UNIX socket at /tmp/.X11-unix/X0.  This deterministically
# triggers the X-server-crash-on-client-connect symptom.
#
# Logs:
#   /var/log/xlog.txt    Xfbdev stderr
#   /var/log/xterm.txt   xterm output (its stderr will show the
#                        connection-closed error when Xfbdev dies)

rm -f /tmp/.X0-lock

# Backgrounded client launcher: wait for the server to come up,
# then connect.  Output captured for post-mortem.
( sleep 3
  xterm -display :0.0 > /var/log/xterm.txt 2>&1
  echo "xterm exited $? at $(date)" >> /var/log/xterm.txt
) &

Xfbdev -ac -retro -zap vt1 :0 2> /var/log/xlog.txt
