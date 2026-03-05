#!/bin/bash
# Tests for bin/head
# Note: explicit exit-code checks for must-fail cases; no set -e reliance there.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"
HEAD_BIN="$REPO_ROOT/bin/head/head"

fail() { echo "FAIL: $*" >&2; exit 1; }

assert_eq() {
    local got="$1" exp="$2" msg="$3"
    [[ "$got" == "$exp" ]] || fail "$msg (got '$got', expected '$exp')"
}

assert_exit() {
    local exit_code="$1" msg="${2:-expected exit $1}"
    [[ $? -eq $exit_code ]] || fail "$msg"
}

build_head() {
    make -C "$REPO_ROOT/bin/head" NATIVE_BUILD=1 >/dev/null 2>&1
    [[ -x "$HEAD_BIN" ]] || fail "head binary missing at $HEAD_BIN"
}

main() {
    build_head

    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    cd "$WORK"

    # Generate test files
    seq 1 20 > twenty.txt     # 20 lines: 1..20
    seq 1 5  > five.txt       # 5  lines: 1..5
    printf 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' > alpha.txt   # 26 bytes, no newline

    # --- HEAD-F-006: default = first 10 lines ---
    got="$("$HEAD_BIN" twenty.txt | paste -sd,)"
    assert_eq "$got" "1,2,3,4,5,6,7,8,9,10" "default -n 10"

    # --- HEAD-F-003: -n N ---
    got="$("$HEAD_BIN" -n 3 twenty.txt | paste -sd,)"
    assert_eq "$got" "1,2,3" "-n 3"

    # --- HEAD-F-004: -c N bytes ---
    got="$(printf 'ABCDE' | "$HEAD_BIN" -c 3 || true)"
    assert_eq "$got" "ABC" "-c 3"

    # --- HEAD-F-007: fewer lines than N – no error ---
    got="$("$HEAD_BIN" -n 10 five.txt | paste -sd,)"
    assert_eq "$got" "1,2,3,4,5" "shorter file no error"

    # --- HEAD-F-005: -n and -c conflict ---
    rc=0; "$HEAD_BIN" -n 3 -c 5 twenty.txt >/dev/null 2>&1 || rc=$?
    [[ $rc -ne 0 ]] || fail "-n -c should fail"

    # --- HEAD-F-008/009: multi-file headers ---
    out="$("$HEAD_BIN" -n 1 five.txt twenty.txt)"
    [[ "$out" == *"==> five.txt <=="*   ]] || fail "missing first header"
    [[ "$out" == *"==> twenty.txt <=="* ]] || fail "missing second header"
    # No leading newline before first header; leading newline before second
    first_char="$(echo "$out" | head -c1)"
    [[ "$first_char" == "=" ]] || fail "leading newline before first header"

    # --- HEAD-B-001: -q suppresses headers ---
    out="$("$HEAD_BIN" -q -n 1 five.txt twenty.txt)"
    [[ "$out" != *"==>"* ]] || fail "-q did not suppress headers"

    # --- HEAD-B-002: -v forces header on single file ---
    out="$("$HEAD_BIN" -v -n 1 five.txt)"
    [[ "$out" == *"==> five.txt <=="* ]] || fail "-v did not force header on single file"

    # --- BSD rule: -q overrides -v ---
    out="$("$HEAD_BIN" -v -q -n 1 five.txt)"
    [[ "$out" != *"==>"* ]] || fail "-q should override -v"
    out="$("$HEAD_BIN" -q -v -n 1 five.txt)"
    [[ "$out" != *"==>"* ]] || fail "-q -v order: -q should still win"

    # --- HEAD-B-004: historic -NUM BSD syntax ---
    got="$("$HEAD_BIN" -3 twenty.txt | paste -sd,)"
    assert_eq "$got" "1,2,3" "historic -3"

    # --- HEAD-G-007: obsolete packed syntax ---
    # -3c = 3 bytes (byte mode, ×1)
    got="$(printf 'ABCDEFGH' | "$HEAD_BIN" -3c || true)"
    assert_eq "$got" "ABC" "packed -3c"
    # -2k = 2048 bytes
    got="$(dd if=/dev/zero bs=3000 count=1 2>/dev/null | "$HEAD_BIN" -2k | wc -c | tr -d ' ' || true)"
    assert_eq "$got" "2048" "packed -2k"
    # -1b = 512 bytes
    got="$(dd if=/dev/zero bs=600 count=1 2>/dev/null | "$HEAD_BIN" -1b | wc -c | tr -d ' ' || true)"
    assert_eq "$got" "512" "packed -1b"
    # -1m = 1048576 bytes
    got="$(dd if=/dev/zero bs=2097152 count=1 2>/dev/null | "$HEAD_BIN" -1m | wc -c | tr -d ' ' || true)"
    assert_eq "$got" "1048576" "packed -1m"
    # -3q = 3 lines, quiet
    out="$("$HEAD_BIN" -3q -n 3 five.txt twenty.txt)"
    [[ "$out" != *"==>"* ]] || fail "packed -3q did not suppress headers"
    # -3v = 3 lines, verbose
    out="$("$HEAD_BIN" -3v five.txt)"
    [[ "$out" == *"==>"* ]] || fail "packed -3v did not force header"

    # --- HEAD-G-001: --lines / --bytes long opts ---
    got="$("$HEAD_BIN" --lines=3 twenty.txt | paste -sd,)"
    assert_eq "$got" "1,2,3" "--lines=3"
    got="$(printf 'ABCDE' | "$HEAD_BIN" --bytes=2 || true)"
    assert_eq "$got" "AB" "--bytes=2"

    # --- HEAD-G-002: -z NUL delimiter ---
    got="$(printf 'a\0b\0c\0d\0' | "$HEAD_BIN" -z -n2 | cat -v || true)"
    [[ "$got" == "a^@b^@" ]] || fail "-z NUL delimiter (got '$got')"

    # --- HEAD-G-003: negative counts (all but last K) ---
    got="$("$HEAD_BIN" -n -3 twenty.txt | paste -sd,)"
    assert_eq "$got" "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17" "-n -3 all but last 3"

    got="$(printf 'ABCDEFGHIJ' | "$HEAD_BIN" -c -3 || true)"
    assert_eq "$got" "ABCDEFG" "-c -3"

    # --- BSD suffixes ---
    got="$(dd if=/dev/zero bs=5000 count=1 2>/dev/null | "$HEAD_BIN" -c 4K | wc -c | tr -d ' ' || true)"
    assert_eq "$got" "4096" "suffix K=1024"
    got="$(dd if=/dev/zero bs=5000 count=1 2>/dev/null | "$HEAD_BIN" -c 4KiB | wc -c | tr -d ' ' || true)"
    assert_eq "$got" "4096"  "suffix KiB=1024"
    # kB: BSD precedence → 1024 (trailing B ignored)
    got="$(dd if=/dev/zero bs=3000 count=1 2>/dev/null | "$HEAD_BIN" -c 2kB | wc -c | tr -d ' ' || true)"
    assert_eq "$got" "2048" "suffix kB→BSD 1024"

    # --- stdin operand (-) ---
    got="$(echo hello | "$HEAD_BIN" -n1 - || true)"
    assert_eq "$got" "hello" "stdin operand -"

    # --- HEAD-P-001: -- end of options ---
    echo one > "--weird-file"
    got="$("$HEAD_BIN" -n1 -- "--weird-file")"
    assert_eq "$got" "one" "-- end of options"
    rm -f -- "--weird-file"

    # --- HEAD-G-005: --help ---
    "$HEAD_BIN" --help >/dev/null

    # --- HEAD-G-006: --version ---
    "$HEAD_BIN" --version | grep -q "head" || fail "--version missing 'head'"

    # --- HEAD-P-004: continue after error, exit nonzero ---
    out="$("$HEAD_BIN" -n1 /no/such/file five.txt 2>/dev/null || true)"
    [[ "$out" == *"==> five.txt <=="*$'\n'"1" ]] || fail "did not continue after open error (got '$out')"
    rc=0; "$HEAD_BIN" -n1 /no/such/file >/dev/null 2>&1 || rc=$?
    [[ $rc -ne 0 ]] || fail "should exit nonzero on error"

    echo "All head tests passed."
}

main "$@"
