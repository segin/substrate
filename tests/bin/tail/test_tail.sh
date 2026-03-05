#!/bin/bash
# Tests for bin/tail
# Requirements coverage: TAIL-FR-001 through TAIL-FR-092, key NFRs
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"
TAIL_BIN="$REPO_ROOT/bin/tail/tail"

fail()      { echo "FAIL: $*" >&2; exit 1; }
assert_eq() {
    local got="$1" exp="$2" msg="$3"
    [[ "$got" == "$exp" ]] || fail "$msg (got='$got' exp='$exp')"
}

build_tail() {
    make -C "$REPO_ROOT/bin/tail" NATIVE_BUILD=1 >/dev/null 2>&1
    [[ -x "$TAIL_BIN" ]] || fail "tail binary missing at $TAIL_BIN"
}

main() {
    build_tail

    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    cd "$WORK"

    seq 1 100 > hundred.txt   # 100 lines
    seq 1 5   > five.txt
    seq 1 20  > twenty.txt
    printf 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' > alpha.txt  # 26 bytes, no newline

    # ── TAIL-FR-013 default -n 10 ────────────────────────────────────────────
    got="$("$TAIL_BIN" hundred.txt | paste -sd,)"
    assert_eq "$got" "91,92,93,94,95,96,97,98,99,100" "default -n 10"

    # ── TAIL-FR-010/013: -n N from end ───────────────────────────────────────
    got="$("$TAIL_BIN" -n 3 hundred.txt | paste -sd,)"
    assert_eq "$got" "98,99,100" "-n 3"

    # ── TAIL-FR-012: -n +N from start (origin 1) ─────────────────────────────
    got="$("$TAIL_BIN" -n +98 hundred.txt | paste -sd,)"
    assert_eq "$got" "98,99,100" "-n +98"

    got="$("$TAIL_BIN" -n +1 five.txt | paste -sd,)"
    assert_eq "$got" "1,2,3,4,5" "-n +1 outputs all"

    # ── TAIL-FR-011: -c N bytes from end ─────────────────────────────────────
    got="$("$TAIL_BIN" -c 5 alpha.txt)"
    assert_eq "$got" "VWXYZ" "-c 5 last 5 bytes"

    # ── TAIL-FR-010: -c +N bytes from start ──────────────────────────────────
    got="$("$TAIL_BIN" -c +24 alpha.txt)"
    assert_eq "$got" "XYZ" "-c +24 from byte 24"

    # ── TAIL-FR-012: -b N blocks (512-byte) ──────────────────────────────────
    python3 -c 'print("x"*2000,end="")' > big.txt
    got="$("$TAIL_BIN" -b 1 big.txt | wc -c | tr -d ' ')"
    assert_eq "$got" "512" "-b 1 = 512 bytes"

    # ── TAIL-FR-003: mutual exclusion -n/-c ──────────────────────────────────
    "$TAIL_BIN" -n 3 -c 5 five.txt >/dev/null 2>&1 && fail "-n -c should fail"; true

    # ── TAIL-FR-003: mutual exclusion -c/-b ──────────────────────────────────
    "$TAIL_BIN" -c 10 -b 1 five.txt >/dev/null 2>&1 && fail "-c -b should fail"; true

    # ── TAIL-FR-060: multi-file headers ──────────────────────────────────────
    out="$("$TAIL_BIN" -n 1 five.txt twenty.txt)"
    [[ "$out" == *"==> five.txt <=="*   ]] || fail "missing first header"
    [[ "$out" == *"==> twenty.txt <=="* ]] || fail "missing second header"

    # ── TAIL-FR-061: -q suppresses headers ───────────────────────────────────
    out="$("$TAIL_BIN" -q -n 1 five.txt twenty.txt)"
    [[ "$out" != *"==>"* ]] || fail "-q did not suppress headers"

    # ── TAIL-FR-061: -v forces header on single file ─────────────────────────
    out="$("$TAIL_BIN" -v -n 1 five.txt)"
    [[ "$out" == *"==> five.txt <=="* ]] || fail "-v did not force header"

    # ── BSD rule: -q overrides -v ─────────────────────────────────────────────
    out="$("$TAIL_BIN" -q -v -n 1 five.txt)"
    [[ "$out" != *"==>"* ]] || fail "-q should override -v"

    # ── Historic -NUM BSD syntax ──────────────────────────────────────────────
    got="$("$TAIL_BIN" -5 hundred.txt | paste -sd,)"
    assert_eq "$got" "96,97,98,99,100" "historic -5"

    # ── GNU obsolete packed -NUMf ─────────────────────────────────────────────
    # Just verify parsing doesn't crash (follow would block, so skip execution)
    "$TAIL_BIN" --help >/dev/null   # ensure --help works

    # ── TAIL-FR-050: reverse mode -r ─────────────────────────────────────────
    got="$(printf 'A\nB\nC\n' | "$TAIL_BIN" -r)"
    assert_eq "$got" "C
B
A" "-r reverse lines"

    # ── TAIL-FR-051: -r -n N shows last N reversed ───────────────────────────
    got="$("$TAIL_BIN" -r -n 3 hundred.txt)"
    assert_eq "$(echo "$got" | paste -sd,)" "100,99,98" "-r -n 3"

    # ── TAIL-FR-052: -r -f conflict ──────────────────────────────────────────
    "$TAIL_BIN" -r -f five.txt >/dev/null 2>&1 && fail "-r -f should fail"; true
    "$TAIL_BIN" -r -F five.txt >/dev/null 2>&1 && fail "-r -F should fail"; true

    # ── TAIL-FR-070: -z NUL delimiter ────────────────────────────────────────
    got="$(printf 'a\0b\0c\0d\0e\0' | "$TAIL_BIN" -z -n3 | cat -v)"
    assert_eq "$got" "c^@d^@e^@" "-z -n3 NUL delimiter"

    # ── Long options --lines / --bytes ────────────────────────────────────────
    got="$("$TAIL_BIN" --lines=3 hundred.txt | paste -sd,)"
    assert_eq "$got" "98,99,100" "--lines=3"

    got="$("$TAIL_BIN" --bytes=3 alpha.txt)"
    assert_eq "$got" "XYZ" "--bytes=3"

    # ── BSD suffixes ──────────────────────────────────────────────────────────
    python3 -c 'print("x"*6000,end="")' > sixk.txt
    got="$("$TAIL_BIN" -c 4K sixk.txt | wc -c | tr -d ' ')"
    assert_eq "$got" "4096" "suffix K=1024"

    # ── Stdin operand (-) ─────────────────────────────────────────────────────
    got="$(echo hello | "$TAIL_BIN" -n1 -)"
    assert_eq "$got" "hello" "stdin operand -"

    # ── -- end of options ────────────────────────────────────────────────────
    echo first > "--tricky"
    got="$("$TAIL_BIN" -n1 -- "--tricky")"
    assert_eq "$got" "first" "-- end of options"
    rm -f -- "--tricky"

    # ── Non-seekable (pipe) lines ─────────────────────────────────────────────
    got="$(seq 1 100 | "$TAIL_BIN" -n 3 | paste -sd,)"
    assert_eq "$got" "98,99,100" "pipe -n 3"

    # ── Non-seekable (pipe) bytes ─────────────────────────────────────────────
    got="$(printf 'ABCDEFGHIJ' | "$TAIL_BIN" -c 3)"
    assert_eq "$got" "HIJ" "pipe -c 3"

    # ── Efficiency: seekable -n doesn't read whole file ───────────────────────
    # (Functional check: -n 3 on 10000-line file gives correct answer)
    seq 1 10000 > biglines.txt
    got="$("$TAIL_BIN" -n 3 biglines.txt | paste -sd,)"
    assert_eq "$got" "9998,9999,10000" "seekable -n 3 on 10000-line file"

    # ── Fewer lines than N — no error ────────────────────────────────────────
    got="$("$TAIL_BIN" -n 20 five.txt | paste -sd,)"
    assert_eq "$got" "1,2,3,4,5" "fewer lines than N ok"

    # ── TAIL-FR-090: error → continue + nonzero exit ─────────────────────────
    out="$("$TAIL_BIN" -n 1 /no/such/file five.txt 2>/dev/null || true)"
    [[ "$out" == *"==> five.txt <=="*$'\n'"5" ]] || fail "did not continue after open error (got '$out')"
    "$TAIL_BIN" -n 1 /no/such/file >/dev/null 2>&1 && fail "should exit nonzero"; true

    # ── -r on non-seekable (pipe) ─────────────────────────────────────────────
    got="$(seq 1 5 | "$TAIL_BIN" -r | paste -sd,)"
    assert_eq "$got" "5,4,3,2,1" "pipe -r"

    # ── --version ────────────────────────────────────────────────────────────
    "$TAIL_BIN" --version | grep -q "tail" || fail "--version missing 'tail'"

    # ── --help ───────────────────────────────────────────────────────────────
    "$TAIL_BIN" --help >/dev/null

    echo "All tail tests passed."
}

main "$@"
