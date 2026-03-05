#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"
SUM_BIN="$REPO_ROOT/bin/sum/sum"
TMP_DIR="$(mktemp -d -t test_sum_XXXXXX)"
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

old1_ref() {
    python3 - "$1" <<'PY'
import sys
s = sys.argv[1].encode('latin1')
ck = 0
for b in s:
    ck = ((ck >> 1) + (0x8000 if (ck & 1) else 0))
    ck = (ck + b) & 0xffff
blocks = (len(s) + 1023) // 1024
print(f"{ck} {blocks}")
PY
}

old2_ref() {
    python3 - "$1" <<'PY'
import sys
s = sys.argv[1].encode('latin1')
total = sum(s)
s32 = total & 0xffffffff
r = (s32 & 0xffff) + (s32 >> 16)
ck = ((r & 0xffff) + (r >> 16)) & 0xffff
blocks = (len(s) + 511) // 512
print(f"{ck} {blocks}")
PY
}

check_boundary() {
    local n="$1"
    local file="$TMP_DIR/boundary_${n}.bin"
    python3 - "$n" "$file" <<'PY'
import sys
n = int(sys.argv[1])
path = sys.argv[2]
with open(path, 'wb') as f:
    f.write(bytes([x % 256 for x in range(n)]))
PY

    local old1_out old2_out
    old1_out=$("$SUM_BIN" "$file" | awk '{print $1 " " $2}')
    old2_out=$("$SUM_BIN" --sysv "$file" | awk '{print $1 " " $2}')

    local ref1 ref2
    ref1=$(python3 - "$file" <<'PY'
import sys
p = sys.argv[1]
b = open(p, 'rb').read()
ck = 0
for x in b:
    ck = ((ck >> 1) + (0x8000 if (ck & 1) else 0))
    ck = (ck + x) & 0xffff
blocks = (len(b) + 1023) // 1024
print(f"{ck} {blocks}")
PY
)
    ref2=$(python3 - "$file" <<'PY'
import sys
p = sys.argv[1]
b = open(p, 'rb').read()
s32 = sum(b) & 0xffffffff
r = (s32 & 0xffff) + (s32 >> 16)
ck = ((r & 0xffff) + (r >> 16)) & 0xffff
blocks = (len(b) + 511) // 512
print(f"{ck} {blocks}")
PY
)

    assert_eq "$old1_out" "$ref1" "old1 boundary n=$n"
    assert_eq "$old2_out" "$ref2" "old2 boundary n=$n"
}

echo "Building sum..."
make -C "$REPO_ROOT/bin/sum" NATIVE_BUILD=1 >/dev/null

echo "--- Legacy vectors ---"
vec='hello\n'
old1_line=$(printf "$vec" | "$SUM_BIN")
old2_line=$(printf "$vec" | "$SUM_BIN" --sysv)
assert_eq "$old1_line" "36979 1" "old1 reference vector (hello\\n)"
assert_eq "$old2_line" "542 1" "old2 reference vector (hello\\n)"

echo "--- Block rounding boundaries ---"
for n in 0 1 511 512 1023 1024 1025; do
    check_boundary "$n"
done

echo "--- Operand handling with '-' ---"
printf 'abc' > "$TMP_DIR/file_a.txt"
mixed=$(printf 'Z' | "$SUM_BIN" "$TMP_DIR/file_a.txt" -)
line_count=$(printf '%s\n' "$mixed" | wc -l | awk '{print $1}')
assert_eq "$line_count" "2" "mixed file/stdin line count"
last_name=$(printf '%s\n' "$mixed" | tail -n 1 | awk '{print $3}')
assert_eq "$last_name" "-" "stdin operand should print '-' as name"

echo "--- BSD conflict policy for -s/-r ---"
bsd_s=$("$SUM_BIN" -s hello)
bsd_ref=$(old1_ref "hello")
assert_eq "$bsd_s" "$bsd_ref" "-s must checksum literal string in BSD mode"

bsd_r=$(printf "$vec" | "$SUM_BIN" -r)
assert_eq "$bsd_r" "36979 1" "-r must not alter legacy checksum math in BSD mode"

echo "--- Fresh optional compatibility mode ---"
gnu_s=$(printf "$vec" | "$SUM_BIN" --compat=gnu -s)
assert_eq "$gnu_s" "542 1" "--compat=gnu should map -s to SysV"

gnu_r=$(printf "$vec" | "$SUM_BIN" --compat=gnu -r)
assert_eq "$gnu_r" "36979 1" "--compat=gnu should map -r to BSD algorithm"

echo "--- Fresh optional unified algorithm selector ---"
a1=$(printf "$vec" | "$SUM_BIN" --algorithm=old1)
a2=$(printf "$vec" | "$SUM_BIN" --algorithm=sysv)
assert_eq "$a1" "36979 1" "--algorithm=old1"
assert_eq "$a2" "542 1" "--algorithm=sysv"

dual=$(printf "$vec" | "$SUM_BIN" --algorithm=old1 --algorithm=old2)
dual_lines=$(printf '%s\n' "$dual" | wc -l | awk '{print $1}')
assert_eq "$dual_lines" "2" "multiple --algorithm should emit multiple lines"

first_tag=$(printf '%s\n' "$dual" | head -n 1 | awk '{print $1}')
second_tag=$(printf '%s\n' "$dual" | tail -n 1 | awk '{print $1}')
assert_eq "$first_tag" "old1" "first multi-algorithm tag"
assert_eq "$second_tag" "old2" "second multi-algorithm tag"

echo "All sum tests passed."
