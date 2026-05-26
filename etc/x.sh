#!/bin/sh
# /root/x.sh — user's standard X session launcher.
# Tracked here under etc/ so it follows the repo; installed to /root/
# at image-build time.
#
# Flags:
#   -ac    disable host-based access control (substrate has no xauth)
#   -retro gray-stipple root background (matches "classic" X look)
#   -zap   Ctrl+Alt+Backspace terminates the server
#   vt1    take VT 1
#   :0     advertise as display :0
rm -r /tmp/.X0-lock
Xfbdev -ac -retro -zap vt1 :0 2> /var/log/xlog.txt
