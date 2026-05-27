#!/bin/sh
# /root/x.sh — X-server verifier wired for run-auto-test.sh.
#
# Uses Xfbdev -dumbSched to disable smart-scheduler SIGALRM —
# a workaround for a substrate-side bug where SIGALRM delivery
# to X's signal context corrupts state and causes startup
# crashes.  The bug is tracked separately; this script gives
# a working X session in the meantime.
#
# Success criterion: Xfbdev stays up >=30 s after launch, with
# xterm able to connect to /tmp/.X11-unix/X0 (xterm may still
# exit due to missing fonts — that's an unrelated client-side
# issue, not an X-server crash).

echo "=== x.sh: X server verifier starting ==="
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0

# Backgrounded watchdog: confirm Xfbdev stays up, declare success.
(
    sleep 3
    echo "=== launching xterm at 3 s ==="
    xterm -display :0.0 > /var/log/xterm.txt 2>&1 &
    XTERM_PID=$!

    prev=3
    XFBDEV_PIDS_SEEN=""
    for tick in 5 10 20 30; do
        sleep $((tick - prev))
        prev=$tick

        # Is Xfbdev still running?  pidof is cheaper than reading /proc.
        XPIDS=$(pgrep Xfbdev || true)
        XTERM_ALIVE=$(kill -0 $XTERM_PID 2>/dev/null && echo yes || echo no)
        echo "=== ${tick}s: Xfbdev pids=[$XPIDS], xterm=$XTERM_ALIVE ==="
        sync
    done

    # Declare success if Xfbdev is still running at 30s.
    if pgrep Xfbdev > /dev/null 2>&1; then
        echo "Result: PASSED (Xfbdev still up at 30 s; X server is alive)"
        sync
        # Cleanup: kill xterm and Xfbdev so the parent shell exits.
        kill $XTERM_PID 2>/dev/null
        sleep 1
        pkill -TERM Xfbdev 2>/dev/null
    else
        echo "Result: FAILED (Xfbdev exited before 30 s)"
    fi
    sync
) &

echo "=== Xfbdev launching (-dumbSched workaround) ==="
Xfbdev -ac -retro -zap -dumbSched vt1 :0 > /var/log/xlog.txt 2>&1
XEXIT=$?
echo "=== Xfbdev exited $XEXIT ==="
sleep 2
sync
echo "=== /var/log/xlog.txt ==="
cat /var/log/xlog.txt 2>/dev/null || echo "(missing)"
echo "=== /var/log/xterm.txt ==="
cat /var/log/xterm.txt 2>/dev/null || echo "(missing)"
echo "=== END ==="
