#!/bin/sh
set -eu

# Reqs: LD-R-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-overflow-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.c" <<'SRC'
int g;
int f(void) { return g; }
SRC
gcc -m64 -c -o "$TMP/base.o" "$TMP/base.c"

if "$LDX" -m64 -r --defsym TOO_BIG=18446744073709551616 -o "$TMP/defsym.out" "$TMP/base.o" >"$TMP/defsym.log" 2>&1; then
	echo "FAIL: out-of-range --defsym unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q 'invalid --defsym' "$TMP/defsym.log"; then
	echo "FAIL: --defsym overflow diagnostic missing" >&2
	sed -n '1,120p' "$TMP/defsym.log" >&2
	exit 1
fi

echo "ok: integer-overflow guard paths are enforced with diagnostics"
