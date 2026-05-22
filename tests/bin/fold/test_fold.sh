#!/bin/sh
# Tests for bin/fold — POSIX.1-2024 + GNU + BSD.
# Compiles fold.c with the host cc so the suite is self-contained.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/fold/fold.c"
BIN="$(mktemp)"
WORK="$(mktemp -d)"
trap 'rm -f "$BIN"; rm -rf "$WORK"' EXIT

cc -O2 -std=c2x -o "$BIN" "$SRC" || { echo "FAIL: compile"; exit 1; }
cd "$WORK"

pass=0 fail=0
check() { # desc expected actual
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %s\n  expected: [%s]\n  actual:   [%s]\n' "$1" "$2" "$3"
    fi
}

# basic wrap at width 4
check "wrap -w4" "abcd|efgh|ij" \
      "$(printf 'abcdefghij\n' | "$BIN" -w4 | tr '\n' '|' | sed 's/|$//')"

# obsolete -width form
check "wrap -4 legacy" "abcd|efgh|ij" \
      "$(printf 'abcdefghij\n' | "$BIN" -4 | tr '\n' '|' | sed 's/|$//')"

# embedded newline ends a line early
check "embedded newline" "ab|cd|ef" \
      "$(printf 'ab\ncdef\n' | "$BIN" -w2 | tr '\n' '|' | sed 's/|$//')"

# -s breaks at last blank within width
check "-s break at blank" "hello |world" \
      "$(printf 'hello world\n' | "$BIN" -s -w8 | tr '\n' '|' | sed 's/|$//')"

# -s with no blank falls back to hard break
check "-s no blank hard break" "abcdefgh|ij" \
      "$(printf 'abcdefghij\n' | "$BIN" -s -w8 | tr '\n' '|' | sed 's/|$//')"

# tab counts to next multiple of 8 by default
# "a<tab>b" : a=col1, tab->col8, b->col9 => with -w8 the b wraps
check "tab column accounting" "2" \
      "$(printf 'a\tb\n' | "$BIN" -w8 | wc -l | tr -d ' ')"

# -b counts bytes: tab is one byte, 10-byte line wraps after 8 bytes
check "-b byte count" "a	bcdefg|hi" \
      "$(printf 'a\tbcdefghi\n' | "$BIN" -b -w8 | tr '\n' '|' | sed 's/|$//')"

# carriage return resets the column
check "cr resets column" "1" \
      "$(printf 'abc\rdef\n' | "$BIN" -w6 | wc -l | tr -d ' ')"

# multiple files
printf 'aaaa\n' > f1; printf 'bbbb\n' > f2
check "two files" "aa|aa|bb|bb" \
      "$("$BIN" -w2 f1 f2 | tr '\n' '|' | sed 's/|$//')"

# default width passes a short line untouched
check "short line untouched" "hello" "$(printf 'hello\n' | "$BIN")"

# invalid width
"$BIN" -w0 </dev/null 2>/dev/null
check "width 0 exits nonzero" "ne0" \
      "$( [ $? -ne 0 ] && echo ne0 || echo zero )"

echo "fold: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
