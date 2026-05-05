#!/bin/sh
set -eu

CAL_BIN=${1:-./cal}

fail() {
    echo "integration: FAIL: $*" >&2
    exit 1
}

"$CAL_BIN" --help >/dev/null
"$CAL_BIN" --version | grep -q "cal (Substrate)" || fail "missing version output"

OUT=$("$CAL_BIN" 2024)
printf '%s\n' "$OUT" | grep -q "2024" || fail "year output missing year header"
printf '%s\n' "$OUT" | grep -q "January" || fail "year output missing January"
printf '%s\n' "$OUT" | grep -q "December" || fail "year output missing December"

OUT=$("$CAL_BIN" -m 9 2024)
printf '%s\n' "$OUT" | sed -n '2p' | grep -q '^Mo ' || fail "-m did not switch header to Monday"

OUT=$("$CAL_BIN" -3 1 2024)
printf '%s\n' "$OUT" | grep -q "December 2023" || fail "-3 missing previous month"
printf '%s\n' "$OUT" | grep -q "February 2024" || fail "-3 missing next month"

OUT=$("$CAL_BIN" --julian 2 1900)
printf '%s\n' "$OUT" | grep -Eq '(^|[^0-9])29([^0-9]|$)' || fail "Julian February 1900 missing 29"

if "$CAL_BIN" 13 2024 2>/dev/null; then
    fail "invalid month should fail"
fi

echo "integration: PASS"