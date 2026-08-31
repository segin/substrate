#!/bin/sh
#
# host_test_libsys_syscall_audit.sh
#
# Enforce that every syscall number reference in lib/sys is via a
# SYS_* identifier defined in sys/arch/i386/syscall.h, and not via
# a raw integer literal handed to syscall().
#
# Catches:
#   - typos / drift between wrapper and kernel
#   - "I'll just hardcode 209 and add a comment" temptations
#   - undocumented private syscall numbers
#
# Exits 0 on clean audit, non-zero with a diagnostic on violation.

set -eu

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
LIBSYS="$ROOT/lib/sys"
HDR="$ROOT/sys/arch/i386/syscall.h"

fail=0

# 1. Every SYS_* used must be #define'd in syscall.h.
#
# Comments are stripped first.  The audit is about code, and prose naming a
# family -- "the SYS_PROC_* family" -- otherwise scans as a reference to a
# constant called SYS_PROC_, since the * is not part of the identifier
# pattern.  Three such phantoms were failing this audit.
strip_comments() {
    python3 -c 'import re,sys; sys.stdout.write(re.sub(r"/\*.*?\*/|//[^\n]*", " ", sys.stdin.read(), flags=re.S))'
}
used="$(cat "$LIBSYS"/*.c | strip_comments | grep -hoE 'SYS_[A-Za-z0-9_]+' | sort -u)"
defined="$(grep -hoE '^#define[ \t]+SYS_[A-Za-z0-9_]+' "$HDR" \
            | awk '{print $2}' | sort -u)"

missing="$(comm -23 <(echo "$used") <(echo "$defined"))"
if [ -n "$missing" ]; then
    echo "FAIL: lib/sys references SYS_* not defined in $HDR:" >&2
    echo "$missing" | sed 's/^/  - /' >&2
    fail=$((fail + 1))
fi

# 2. No raw integer first-arg to syscall(): only SYS_* identifiers
#    or named constants (uppercase).  A literal like syscall(42, ...)
#    is forbidden.  We allow `0` and identifier patterns.
raw="$(grep -nE 'syscall\([[:space:]]*[0-9]+[[:space:]]*,' \
        "$LIBSYS"/*.c || true)"
if [ -n "$raw" ]; then
    echo "FAIL: lib/sys contains raw-integer syscall numbers:" >&2
    echo "$raw" | sed 's/^/  /' >&2
    fail=$((fail + 1))
fi

if [ $fail -eq 0 ]; then
    echo "lib/sys syscall audit OK ($(echo "$used" | wc -l) SYS_* refs, all defined)"
    exit 0
fi
exit 1
