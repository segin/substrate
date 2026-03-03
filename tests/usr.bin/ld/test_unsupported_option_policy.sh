#!/bin/sh
set -eu

# Reqs: LD-U-010, LD-W-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-compat-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int main(void) { return 0; }
SRC

gcc -m64 -c -o "$TMP/main64.o" "$TMP/main.c"

if ! "$LDX" --unknown-flag -m64 -r -o "$TMP/out_gnu_default.o" "$TMP/main64.o" >"$TMP/gnu_default.out" 2>&1; then
	echo "FAIL: default GNU mode should warn-and-continue on unknown option" >&2
	cat "$TMP/gnu_default.out" >&2
	exit 1
fi
if ! grep -q "warning: unsupported option ignored (gnu mode)" "$TMP/gnu_default.out"; then
	echo "FAIL: missing GNU-mode warning diagnostic" >&2
	cat "$TMP/gnu_default.out" >&2
	exit 1
fi

if ! "$LDX" --compat=gnu --unknown-flag -m64 -r -o "$TMP/out_gnu_explicit.o" "$TMP/main64.o" >"$TMP/gnu_explicit.out" 2>&1; then
	echo "FAIL: explicit GNU mode should warn-and-continue on unknown option" >&2
	cat "$TMP/gnu_explicit.out" >&2
	exit 1
fi
if ! grep -q "warning: unsupported option ignored (gnu mode)" "$TMP/gnu_explicit.out"; then
	echo "FAIL: missing explicit GNU-mode warning diagnostic" >&2
	cat "$TMP/gnu_explicit.out" >&2
	exit 1
fi

if "$LDX" --compat=lld --unknown-flag -m64 -r -o "$TMP/out_lld.o" "$TMP/main64.o" >"$TMP/lld.err" 2>&1; then
	echo "FAIL: lld mode should error on unknown option" >&2
	exit 1
fi
if ! grep -q "error: unsupported option in lld mode" "$TMP/lld.err"; then
	echo "FAIL: missing lld-mode error diagnostic" >&2
	cat "$TMP/lld.err" >&2
	exit 1
fi

if "$LDX" --compat=notreal -m64 -r -o "$TMP/out_bad_compat.o" "$TMP/main64.o" >"$TMP/bad_compat.err" 2>&1; then
	echo "FAIL: invalid --compat value unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "unsupported compatibility mode 'notreal'" "$TMP/bad_compat.err"; then
	echo "FAIL: missing invalid --compat diagnostic" >&2
	cat "$TMP/bad_compat.err" >&2
	exit 1
fi

echo "ok: ld unsupported-option policy matrix by compatibility mode"
