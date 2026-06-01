#!/bin/sh
# sdm — substrate display manager.  Supervises an Xfbdev server plus the
# graphical greeter (sgreet): start X, run the greeter, and when the
# user's session ends, tear X down and loop back to a fresh greeter.
#
# Uses -dumbSched as a workaround for the qemu+KVM SIGALRM coherence bug
# (see docs / memory): under KVM the smart-scheduler SIGALRM triggers a
# host-side stale-read that crashes the server at startup.  Harmless
# under TCG; remove -dumbSched once the host KVM issue is resolved.
#
# -noreset is REQUIRED, not cosmetic.  The greeter (sgreet) is the X
# server's sole client; when it exits at the greeter->session handoff the
# server would, by default, regenerate (server reset), which tears down
# and recreates every input device through kdrive's EvdevPtrDisable ->
# EvdevPtrEnable.  That teardown/recreate is racy: a wakeup already in
# flight reaches EvdevPtrRead with the freed pointer's pi, reads a junk
# driverPrivate, and evdev's defensive guard then silently drains the
# mouse forever -- the "mouse dies the moment xterm appears, keyboard
# still works" bug.  -noreset keeps the server alive across the handoff
# (no regeneration, no input-device cycle, no race); sdm tears X down
# explicitly when the session ends, so nothing is orphaned.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
SGREET=/usr/sbin/sgreet
DISP=:0

# Tear the X server down on TERM/INT (e.g. `rc.d/60-sdm stop`) so we
# don't orphan Xfbdev when the supervisor is killed.
XPID=
cleanup() { [ -n "$XPID" ] && kill "$XPID" 2>/dev/null; exit 0; }
trap cleanup TERM INT

while :; do
    rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 2>/dev/null
    mkdir -p /tmp/.X11-unix

    Xfbdev -ac -retro -noreset -dumbSched vt1 "$DISP" > /var/log/xlog.txt 2>&1 &
    XPID=$!

    # sgreet retries XOpenDisplay for ~10s, so it tolerates a slow start.
    DISPLAY="$DISP" "$SGREET"

    kill "$XPID" 2>/dev/null
    wait "$XPID" 2>/dev/null
    sleep 1
done
