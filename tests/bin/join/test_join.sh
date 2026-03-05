#!/bin/bash
set -ea

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"

JOIN_BIN="$REPO_ROOT/bin/join/join"
export LD_LIBRARY_PATH="$REPO_ROOT/usr.lib/join:$LD_LIBRARY_PATH"
TMP_DIR="$(mktemp -d -t test_join_XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

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

make -C "$REPO_ROOT/bin/join" NATIVE_BUILD=1 >/dev/null

echo "a 1" > "$TMP_DIR/f1.txt"
echo "b 2" >> "$TMP_DIR/f1.txt"
echo "c 3" >> "$TMP_DIR/f1.txt"

echo "a X" > "$TMP_DIR/f2.txt"
echo "b Y" >> "$TMP_DIR/f2.txt"
echo "d Z" >> "$TMP_DIR/f2.txt"

# 1. Standard Join
echo "--- Standard Join ---"
got=$("$JOIN_BIN" "$TMP_DIR/f1.txt" "$TMP_DIR/f2.txt")
exp="a 1 X
b 2 Y"
assert_eq "$got" "$exp" "standard join"

# 2. Outer Join (POSIX -a 1)
got=$("$JOIN_BIN" -a 1 "$TMP_DIR/f1.txt" "$TMP_DIR/f2.txt")
exp="a 1 X
b 2 Y
c 3"
assert_eq "$got" "$exp" "-a 1 outer join"

# 3. BSD Outer Join (BSD -a meaning both)
got=$("$JOIN_BIN" -a "$TMP_DIR/f1.txt" "$TMP_DIR/f2.txt")
exp="a 1 X
b 2 Y
c 3
d Z"
assert_eq "$got" "$exp" "BSD -a operand-less join"

# 4. Difference (POSIX -v)
got=$("$JOIN_BIN" -v 1 "$TMP_DIR/f1.txt" "$TMP_DIR/f2.txt")
assert_eq "$got" "c 3" "-v 1 unpair only"

# 5. Field Delimiters
echo "a,1" > "$TMP_DIR/f1c.txt"
echo "b,2" >> "$TMP_DIR/f1c.txt"
echo "a,X" > "$TMP_DIR/f2c.txt"
echo "b,Y" >> "$TMP_DIR/f2c.txt"
got=$("$JOIN_BIN" -t , "$TMP_DIR/f1c.txt" "$TMP_DIR/f2c.txt")
exp="a,1,X
b,2,Y"
assert_eq "$got" "$exp" "comma delimiter"

# 6. Cartesian Product
echo "a 1" > "$TMP_DIR/cp1.txt"
echo "a 2" >> "$TMP_DIR/cp1.txt"
echo "a X" > "$TMP_DIR/cp2.txt"
echo "a Y" >> "$TMP_DIR/cp2.txt"
got=$("$JOIN_BIN" "$TMP_DIR/cp1.txt" "$TMP_DIR/cp2.txt")
exp="a 1 X
a 1 Y
a 2 X
a 2 Y"
assert_eq "$got" "$exp" "cartesian product"

echo "All join tests passed."
