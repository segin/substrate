#!/bin/sh
set -eu

# Reqs: LD-U-001, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-hashstyle-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int exported_fn(void) { return 9; }
SRC
gcc -m64 -fPIC -c -o "$TMP/main.o" "$TMP/main.c"

"$LDX" -m64 -shared --hash-style=sysv -o "$TMP/sysv.so" "$TMP/main.o"
"$LDX" -m64 -shared --hash-style=gnu -o "$TMP/gnu.so" "$TMP/main.o"
"$LDX" -m64 -shared --hash-style=both -o "$TMP/both.so" "$TMP/main.o"

readelf -WS "$TMP/sysv.so" > "$TMP/sysv.sections"
if ! grep -q "[[:space:]]\\.hash[[:space:]]" "$TMP/sysv.sections"; then
	echo "FAIL: --hash-style=sysv missing .hash section" >&2
	cat "$TMP/sysv.sections" >&2
	exit 1
fi
if grep -q "[[:space:]]\\.gnu.hash[[:space:]]" "$TMP/sysv.sections"; then
	echo "FAIL: --hash-style=sysv unexpectedly emitted .gnu.hash" >&2
	cat "$TMP/sysv.sections" >&2
	exit 1
fi

readelf -WS "$TMP/gnu.so" > "$TMP/gnu.sections"
if ! grep -q "[[:space:]]\\.gnu.hash[[:space:]]" "$TMP/gnu.sections"; then
	echo "FAIL: --hash-style=gnu missing .gnu.hash section" >&2
	cat "$TMP/gnu.sections" >&2
	exit 1
fi
if grep -q "[[:space:]]\\.hash[[:space:]]" "$TMP/gnu.sections"; then
	echo "FAIL: --hash-style=gnu unexpectedly emitted .hash" >&2
	cat "$TMP/gnu.sections" >&2
	exit 1
fi

readelf -WS "$TMP/both.so" > "$TMP/both.sections"
if ! grep -q "[[:space:]]\\.hash[[:space:]]" "$TMP/both.sections"; then
	echo "FAIL: --hash-style=both missing .hash section" >&2
	cat "$TMP/both.sections" >&2
	exit 1
fi
if ! grep -q "[[:space:]]\\.gnu.hash[[:space:]]" "$TMP/both.sections"; then
	echo "FAIL: --hash-style=both missing .gnu.hash section" >&2
	cat "$TMP/both.sections" >&2
	exit 1
fi

if "$LDX" -m64 -shared --hash-style=unknown -o "$TMP/bad.so" "$TMP/main.o" >"$TMP/bad.err" 2>&1; then
	echo "FAIL: invalid --hash-style value unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "unsupported --hash-style value" "$TMP/bad.err"; then
	echo "FAIL: invalid --hash-style diagnostic missing" >&2
	cat "$TMP/bad.err" >&2
	exit 1
fi

echo "ok: ld --hash-style matrix emits expected hash sections"
