#!/bin/bash
# Host-build regression test for bin/whoami.  Run from the project root.
# Don't use `set -e` — we run commands expected to exit non-zero and
# bash's `rc=$?` pattern would still trigger the trap.

SRC="bin/whoami/whoami.c"
BIN="/tmp/test_whoami_host"
cc -O2 -Wall -Wextra -o "$BIN" "$SRC" || exit 1

PASS=0
FAIL=0

ok()   { echo "PASS: $1"; PASS=$((PASS+1)); }
bad()  { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

# 1. Default behaviour matches /usr/bin/whoami.
got=$("$BIN")
exp=$(/usr/bin/whoami)
[ "$got" = "$exp" ] && ok "whoami matches /usr/bin/whoami" || bad "whoami != /usr/bin/whoami ('$got' vs '$exp')"

# 2. Should equal `id -un`.
got=$("$BIN")
exp=$(/usr/bin/id -un)
[ "$got" = "$exp" ] && ok "whoami == id -un" || bad "whoami != id -un ('$got' vs '$exp')"

# 3. --help exits zero and prints something containing 'Usage'.
out=$("$BIN" --help 2>&1)
rc=$?
{ [ $rc -eq 0 ] && echo "$out" | grep -q Usage; } && ok "--help OK" || bad "--help broken (rc=$rc)"

# 4. --version exits zero.
out=$("$BIN" --version 2>&1); rc=$?
[ $rc -eq 0 ] && ok "--version OK" || bad "--version broken (rc=$rc)"

# 5. Extra operand rejected.
out=$("$BIN" extra 2>&1); rc=$?
[ $rc -ne 0 ] && ok "extra arg rejected" || bad "extra arg accepted (rc=$rc)"

# 6. Bare -- followed by operand rejected; bare -- alone is OK.
out=$("$BIN" -- 2>&1); rc=$?
[ $rc -eq 0 ] && ok "bare -- accepted" || bad "bare -- rejected (rc=$rc, out=$out)"
out=$("$BIN" -- extra 2>&1); rc=$?
[ $rc -ne 0 ] && ok "-- then arg rejected" || bad "-- then arg accepted (rc=$rc)"

if [ $FAIL -gt 0 ]; then
    echo "$FAIL tests failed."
    exit 1
fi
echo "All $PASS tests passed."
