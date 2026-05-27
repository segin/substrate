#!/bin/sh
# /root/x.sh — X-server-crash reproducer wired for run-auto-test.sh.
#
# Layout (init mode):
#   1. Print start marker.
#   2. Background a client launcher that waits 3 s and runs xterm
#      against the AF_UNIX socket at /tmp/.X11-unix/X0.
#   3. Run Xfbdev under LD_PRELOAD=/lib/nosegvhandler.so so X's
#      OsSigHandler is NEVER installed for SIGSEGV/SIGBUS/SIGILL/
#      SIGFPE.  The kernel's default-action then catches the
#      first SIGSEGV and prints TRAP/CORE for the ORIGINAL call
#      site (instead of the recursive vpnprintf/ErrorFSigSafe
#      stack we see when OsSigHandler is active).
#   4. After Xfbdev exits, dump captured logs and print Result.

echo "=== x.sh: reproducer mode starting ==="
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0

# Backgrounded client launcher: waits for the server, then connects.
( sleep 3
  echo "=== xterm launching ==="
  xterm -display :0.0 > /var/log/xterm.txt 2>&1
  echo "xterm exit $?" >> /var/log/xterm.txt
) &

echo "=== Xfbdev launching (LD_PRELOAD=/lib/nosegvhandler.so) ==="
LD_PRELOAD=/lib/nosegvhandler.so Xfbdev -ac -retro -zap vt1 :0 > /var/log/xlog.txt 2>&1
XEXIT=$?
echo "=== Xfbdev exited $XEXIT ==="

# Let kernel + backgrounded xterm finalize.
sleep 2
sync

echo "=== /var/log/xlog.txt ==="
cat /var/log/xlog.txt 2>/dev/null || echo "(missing)"
echo "=== /var/log/xterm.txt ==="
cat /var/log/xterm.txt 2>/dev/null || echo "(missing)"
echo "=== END ==="

if [ "$XEXIT" -eq 0 ]; then
    echo "Result: PASSED (Xfbdev exited cleanly, code 0)"
else
    echo "Result: FAILED (Xfbdev exited $XEXIT)"
fi
