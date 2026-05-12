#!/bin/bash
# Host-build regression test: compare bin/id against /usr/bin/id for
# all axes the substrate implementation claims to match.  Run from
# the project root.
set -e

ID_SRC="bin/id/id.c"
ID_BIN="/tmp/test_id_host"
cc -O2 -Wall -Wextra -D_GNU_SOURCE -o "$ID_BIN" "$ID_SRC"

TESTS_PASSED=0
TESTS_FAILED=0

assert_eq() {
    local name="$1" got="$2" exp="$3"
    if [ "$got" = "$exp" ]; then
        echo "PASS: $name"
        TESTS_PASSED=$((TESTS_PASSED+1))
    else
        echo "FAIL: $name"
        echo "  Got: '$got'"
        echo "  Exp: '$exp'"
        TESTS_FAILED=$((TESTS_FAILED+1))
    fi
}

assert_exit_nonzero() {
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "FAIL: $name (expected non-zero exit)"
        TESTS_FAILED=$((TESTS_FAILED+1))
    else
        echo "PASS: $name"
        TESTS_PASSED=$((TESTS_PASSED+1))
    fi
}

# Standard POSIX modes (single user) — must match /usr/bin/id byte-for-byte.
for arg in "" "-u" "-g" "-G" "-un" "-gn" "-Gn" "-ur" "-gr" "-u -r" "-u -n"; do
    got=$($ID_BIN $arg $USER 2>/dev/null || echo "FAIL_GOT")
    exp=$(/usr/bin/id $arg $USER 2>/dev/null || echo "FAIL_EXP")
    if [ -z "$arg" ]; then
        # /usr/bin/id may append context=... — substrate doesn't.
        exp=$(echo "$exp" | sed 's/ context=.*//')
    fi
    assert_eq "id $arg" "$got" "$exp"
done

# Multi-user (properly quoted)
assert_eq "id -u root segin"  "$($ID_BIN -u root segin)"  "$(/usr/bin/id -u root segin)"
assert_eq "id -G root segin"  "$($ID_BIN -G root segin)"  "$(/usr/bin/id -G root segin)"
assert_eq "id -Gn root segin" "$($ID_BIN -Gn root segin)" "$(/usr/bin/id -Gn root segin)"

# Zero-term — compare as hex so the test reads
assert_eq "id -Gnz (bytes)"        "$($ID_BIN -Gnz | xxd)"           "$(/usr/bin/id -Gnz | xxd)"
assert_eq "id -uz (bytes)"         "$($ID_BIN -uz  | xxd)"           "$(/usr/bin/id -uz  | xxd)"
assert_eq "id -G -z multi (bytes)" "$($ID_BIN -G -z root segin | xxd)" \
                                   "$(/usr/bin/id -G -z root segin | xxd)"

# POSIX violations must be rejected.
assert_exit_nonzero "id -r alone"  $ID_BIN -r
assert_exit_nonzero "id -n alone"  $ID_BIN -n
assert_exit_nonzero "id -uG mixed" $ID_BIN -uG
assert_exit_nonzero "id -z default" $ID_BIN -z
assert_exit_nonzero "id no-such-user" $ID_BIN no-such-user-substrate-xyz

if [ $TESTS_FAILED -gt 0 ]; then
    echo "$TESTS_FAILED tests failed."
    exit 1
fi
echo "All $TESTS_PASSED tests passed."
