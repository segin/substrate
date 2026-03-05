#!/bin/bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"

WC_BIN="$REPO_ROOT/bin/wc/wc"

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

assert_eq() {
    local got="$1"
    local exp="$2"
    local msg="$3"
    if [[ "$got" != "$exp" ]]; then
        fail "$msg (got='$got' exp='$exp')"
    fi
}

make -C "$REPO_ROOT/bin/wc" NATIVE_BUILD=1 >/dev/null

TMP_DIR="$(mktemp -d -t test_wc_XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo -n "a b c" > "$TMP_DIR/no_nl.txt"
echo "a b c" > "$TMP_DIR/nl.txt"
echo "hello world" > "$TMP_DIR/words.txt"
printf "line1\nline22\nlongline333\nshort" > "$TMP_DIR/lens.txt"

# 1. Default output
echo "--- Default output ---"
got=$("$WC_BIN" "$TMP_DIR/words.txt")
exp="       1       2      12 $TMP_DIR/words.txt"
assert_eq "$got" "$exp" "default words.txt format"

# 2. No trailing newline lines
got=$("$WC_BIN" -l "$TMP_DIR/no_nl.txt")
assert_eq "$got" "       0 $TMP_DIR/no_nl.txt" "no newline lines"

# 3. Exclusivity of -c and -m
got=$("$WC_BIN" -cm "$TMP_DIR/words.txt")
assert_eq "$got" "      12 $TMP_DIR/words.txt" "-cm gives characters"

# 4. Longest line
got=$("$WC_BIN" -L "$TMP_DIR/lens.txt")
assert_eq "$got" "      11 $TMP_DIR/lens.txt" "longest line in lens.txt"

# 5. Stdin no filename
got=$(cat "$TMP_DIR/words.txt" | "$WC_BIN" -c)
assert_eq "$got" "      12" "stdin no filename output"

# 6. Totals
got=$("$WC_BIN" -l "$TMP_DIR/no_nl.txt" "$TMP_DIR/nl.txt" | tail -n1)
exp="       1 total"
assert_eq "$got" "$exp" "totals line output"

# 7. --total=only
got=$("$WC_BIN" -l --total=only "$TMP_DIR/no_nl.txt" "$TMP_DIR/nl.txt")
exp="       1"
assert_eq "$got" "$exp" "--total=only output"

# 8. --files0-from
# Note: we use printf to generate NUL delimited safely
printf "%s\0%s\0" "$TMP_DIR/no_nl.txt" "$TMP_DIR/nl.txt" > "$TMP_DIR/list"
got=$("$WC_BIN" -l --files0-from="$TMP_DIR/list" | tail -n1)
assert_eq "$got" "$exp total" "--files0-from output"

echo "All wc tests passed."
