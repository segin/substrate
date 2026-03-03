#!/bin/sh
set -eu

# Reqs: LD-U-005, LD-E-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-symprec-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/dup_a.c" <<'SRC'
int dup_sym(void) { return 1; }
SRC
cat > "$TMP/dup_b.c" <<'SRC'
int dup_sym(void) { return 2; }
SRC
gcc -m64 -c -o "$TMP/dup_a.o" "$TMP/dup_a.c"
gcc -m64 -c -o "$TMP/dup_b.o" "$TMP/dup_b.c"

if "$LDX" -m64 -r -o "$TMP/out_dup.o" "$TMP/dup_a.o" "$TMP/dup_b.o" >"$TMP/dup.err" 2>&1; then
	echo "FAIL: duplicate strong definitions unexpectedly linked" >&2
	exit 1
fi
if ! grep -q "duplicate strong definition of" "$TMP/dup.err"; then
	echo "FAIL: missing duplicate-strong diagnostic" >&2
	cat "$TMP/dup.err" >&2
	exit 1
fi

cat > "$TMP/weak.c" <<'SRC'
__attribute__((weak)) int precedence_sym(void) { return 3; }
SRC
cat > "$TMP/strong.c" <<'SRC'
int precedence_sym(void) { return 4; }
SRC
gcc -m64 -c -o "$TMP/weak.o" "$TMP/weak.c"
gcc -m64 -c -o "$TMP/strong.o" "$TMP/strong.c"
"$LDX" -m64 -r -o "$TMP/out_weak_strong.o" "$TMP/weak.o" "$TMP/strong.o"

cat > "$TMP/common.c" <<'SRC'
int common_overridden;
SRC
cat > "$TMP/common_strong.c" <<'SRC'
int common_overridden = 9;
SRC
gcc -m64 -fcommon -c -o "$TMP/common.o" "$TMP/common.c"
gcc -m64 -c -o "$TMP/common_strong.o" "$TMP/common_strong.c"
"$LDX" -m64 -r --warn-common -o "$TMP/out_common_override.o" "$TMP/common.o" "$TMP/common_strong.o" >"$TMP/common_warn.out" 2>&1
if ! grep -q "common symbol .* overridden by strong definition" "$TMP/common_warn.out"; then
	echo "FAIL: missing SHN_COMMON override warning" >&2
	cat "$TMP/common_warn.out" >&2
	exit 1
fi

echo "ok: ld strong/weak/common precedence and conflicts"
