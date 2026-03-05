#!/bin/sh
set -eu

# Reqs: LD-U-010, LD-W-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-diag-parser-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.c" <<'SRC'
int base(void) { return 0; }
SRC
gcc -m64 -c -o "$TMP/base.o" "$TMP/base.c"

"$LDX" --compat=gnu --unknown-flag -m64 -r -o "$TMP/warn.o" "$TMP/base.o" >"$TMP/warn.log" 2>&1
if ! grep -q '^ld: warning: unsupported option ignored (gnu mode): --unknown-flag' "$TMP/warn.log"; then
	echo "FAIL: warning line is not parser-compatible" >&2
	sed -n '1,80p' "$TMP/warn.log" >&2
	exit 1
fi

if "$LDX" --compat=lld --unknown-flag -m64 -r -o "$TMP/err.o" "$TMP/base.o" >"$TMP/err.log" 2>&1; then
	echo "FAIL: lld-mode unsupported option unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q '^ld: error: unsupported option in lld mode: --unknown-flag' "$TMP/err.log"; then
	echo "FAIL: error line is not parser-compatible" >&2
	sed -n '1,80p' "$TMP/err.log" >&2
	exit 1
fi

echo "ok: warning/error text remains parser-compatible"
