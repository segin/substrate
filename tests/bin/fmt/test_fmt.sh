#!/bin/sh
# Tests for bin/fmt — BSD + GNU.
# Compiles fmt.c with the host cc so the suite is self-contained.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SRC="$REPO/bin/fmt/fmt.c"
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
pipe() { tr '\n' '|' | sed 's/|$//'; }

# reflow: join short lines up to the width
check "reflow -w20" "one two three four|five" \
      "$(printf 'one two\nthree four\nfive\n' | "$BIN" -w20 | pipe)"

# blank line separates paragraphs and is preserved
check "blank-line paragraphs" "a b||c d" \
      "$(printf 'a b\n\nc d\n' | "$BIN" -w20 | pipe)"

# -s splits long lines but never joins short ones
check "-s split only" "aaa bbb|ccc ddd|short" \
      "$(printf 'aaa bbb ccc ddd\nshort\n' | "$BIN" -s -w8 | pipe)"

# positional goal operand
check "positional goal" "one two three four|five" \
      "$(printf 'one two three four five\n' | "$BIN" 20 | pipe)"

# prefix preserved on every wrapped line (non-crown)
check "prefix preserved" "   alpha|   beta|   gamma" \
      "$(printf '   alpha beta\n   gamma\n' | "$BIN" -w12 | pipe)"

# crown margin: line 1 keeps its indent, rest use line 2's indent
check "crown margin" "Title words|    body words|    here" \
      "$(printf 'Title words\n    body words here\n' | "$BIN" -c -w14 | pipe)"

# lines beginning with '.' pass through verbatim
check "dot-line passthrough" ".PP|x" \
      "$(printf '.PP\nx\n' | "$BIN" | pipe)"

# two spaces after a sentence-ending period
check "sentence spacing" "Hi there.  Next word" \
      "$(printf 'Hi there. Next word\n' | "$BIN" -w40 | pipe)"

# -p: an indentation change starts a new paragraph
check "-p indent splits" "aa bb|    cc dd" \
      "$(printf 'aa bb\n    cc dd\n' | "$BIN" -p -w40 | pipe)"

# --version
check "--version" "fmt (Substrate) 1.0" "$("$BIN" --version)"

echo "fmt: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
