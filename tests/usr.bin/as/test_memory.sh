#!/bin/sh
# The memory an assembly takes is a few hundred bytes for each statement
# of the source (AS-T-229).
#
# 200,000 lines of `addl $1,%eax` -- the audit's input -- are assembled,
# and the most memory the assembler had resident may be no more than
# 60 MB.  (It was 258 MB: a statement was 168 bytes in a vector that was
# doubled and copied to get there, four copies of the source were kept
# to the end, and every instruction kept its operands.  It is under 50.)
#
# What is counted is the assembler's own: the measure is taken of a
# process that became it, and what that process had before is taken off.
# Needs python3, for wait4(); without it the test is not run.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
N=${AS_MEMORY_N:-200000}
LIMIT_KB=${AS_MEMORY_LIMIT_KB:-61440}

if ! command -v python3 > /dev/null 2>&1; then
    echo "ok: memory for each statement (not run: no python3)"
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

awk -v n="$N" 'BEGIN { for (i = 0; i < n; i++) print "addl $1,%eax" }' > big.s

python3 - "$AS" "$N" "$LIMIT_KB" <<'PY'
import os, sys

def peak_kb(argv):
    pid = os.fork()
    if pid == 0:
        try:
            os.execv(argv[0], argv)
        finally:
            os._exit(127)
    _, status, usage = os.wait4(pid, 0)
    return usage.ru_maxrss, status

asm, n, limit = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
# What a process that does nothing has, having been this one.
floor, _ = peak_kb(["/bin/sh", "-c", ":"])
kb, status = peak_kb([asm, "--32", "-o", "big.o", "big.s"])
if status != 0:
    print("FAIL: %d statements were not assembled (status %d)" % (n, status))
    sys.exit(1)
if kb <= floor:
    # Nothing can be told: the process was no larger than what it began as.
    print("ok: memory for each statement (not measured: under the %d KB the test itself has)" % floor)
    sys.exit(0)
if kb > limit:
    print("FAIL: %d statements took %d KB, more than %d" % (n, kb, limit))
    sys.exit(1)
print("ok: memory for each statement (%d statements, %d KB, %d bytes each)" % (n, kb, kb * 1024 // n))
PY
