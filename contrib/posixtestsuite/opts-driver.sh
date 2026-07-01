#!/bin/sh
# opts-driver.sh — Open POSIX Test Suite baseline driver for Substrate.
#
# Runs as init (PID 1).  Mounts the OPTS test image (second disk,
# /dev/storage/sata1) at /mnt, walks the manifest, runs each test binary
# under a watchdog timeout, maps its exit code to a POSIX-test verdict,
# and streams one framed line per test to the console/serial.
#
# Framing (the host runner greps these):
#   OPTS|BEGIN
#   OPTS|START|<area>|<name>            (emitted BEFORE running -> panic culprit)
#   OPTS|RESULT|<area>|<name>|<verdict>|<rc>
#   OPTS|DONE
#
# Verdict mapping (OPTS exit codes, see include/posixtest.h):
#   0 PASS  1 FAIL  2 UNRESOLVED  4 UNSUPPORTED  5 UNTESTED  6 NORESULT
#   137 (128+SIGKILL) -> TIMEOUT (killed by watchdog)
#   other 128+N       -> CRASH   (test died on a signal)
#
# A skip-list (/mnt/skip.txt, one "<area>/<name>" per line) lets the host
# runner exclude tests that panicked the kernel on a previous boot.  A
# progress file (/mnt/done.txt) lets a re-boot after a panic resume where
# it left off instead of restarting the whole run.

TESTDEV=/dev/storage/sata1
MNT=/mnt
TIMEOUT="${OPTS_TIMEOUT:-10}"

echo "OPTS|BEGIN"

/bin/mkdir -p "$MNT" 2>/dev/null
if ! /bin/mount "$TESTDEV" "$MNT" ext2 2>/dev/null; then
    echo "OPTS|FATAL|cannot mount $TESTDEV"
    echo "OPTS|DONE"
    while : ; do /bin/sleep 5; done
fi

MANIFEST="$MNT/manifest.txt"
SKIP="$MNT/skip.txt"
DONE="$MNT/done.txt"
[ -f "$DONE" ] || : > "$DONE" 2>/dev/null

is_listed() {   # is_listed <needle> <file>
    [ -f "$2" ] || return 1
    while IFS= read -r l; do
        [ "$l" = "$1" ] && return 0
    done < "$2"
    return 1
}

run_one() {   # run_one <binpath> ; returns child rc (or 137 on watchdog kill)
    "$1" >/dev/null 2>&1 &
    p=$!
    ( /bin/sleep "$TIMEOUT"; /bin/kill -9 "$p" 2>/dev/null ) &
    w=$!
    wait "$p"
    rc=$?
    /bin/kill -9 "$w" 2>/dev/null
    wait "$w" 2>/dev/null
    return $rc
}

verdict() {   # verdict <rc>
    case "$1" in
        0)   echo PASS ;;
        1)   echo FAIL ;;
        2)   echo UNRESOLVED ;;
        4)   echo UNSUPPORTED ;;
        5)   echo UNTESTED ;;
        6)   echo NORESULT ;;
        137) echo TIMEOUT ;;
        *)   echo CRASH ;;
    esac
}

while IFS= read -r entry; do
    case "$entry" in ''|'#'*) continue ;; esac
    area=${entry%%/*}
    name=${entry#*/}
    bin="$MNT/bin/$entry"
    is_listed "$entry" "$SKIP" && { echo "OPTS|RESULT|$area|$name|SKIP|0"; continue; }
    is_listed "$entry" "$DONE" && continue
    [ -x "$bin" ] || { echo "OPTS|RESULT|$area|$name|NOBIN|0"; continue; }
    echo "OPTS|START|$area|$name"
    run_one "$bin"
    rc=$?
    echo "OPTS|RESULT|$area|$name|$(verdict $rc)|$rc"
    echo "$entry" >> "$DONE" 2>/dev/null
done < "$MANIFEST"

echo "OPTS|DONE"
# Park; the host runner tears down QEMU on seeing OPTS|DONE.
while : ; do /bin/sleep 5; done
