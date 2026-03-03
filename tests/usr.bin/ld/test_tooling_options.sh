#!/bin/sh
set -eu

# Reqs: LD-U-011, LD-U-010

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-tooling-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/sym.c" <<'SRC'
int traced_symbol(void) { return 5; }
SRC
gcc -m64 -c -o "$TMP/sym.o" "$TMP/sym.c"

"$LDX" -m64 -r -o "$TMP/out.o" -Map "$TMP/link.map" --trace --trace-symbol=traced_symbol "$TMP/sym.o" >"$TMP/trace.out" 2>&1

if [ ! -s "$TMP/link.map" ]; then
	echo "FAIL: -Map did not produce map output" >&2
	exit 1
fi
if ! grep -q "Sections:" "$TMP/link.map"; then
	echo "FAIL: map file missing section listing" >&2
	cat "$TMP/link.map" >&2
	exit 1
fi
if ! grep -q "Symbols:" "$TMP/link.map" || ! grep -q "traced_symbol" "$TMP/link.map"; then
	echo "FAIL: map file missing symbol listing for traced_symbol" >&2
	cat "$TMP/link.map" >&2
	exit 1
fi
if ! grep -q "trace: input" "$TMP/trace.out"; then
	echo "FAIL: --trace did not emit input trace lines" >&2
	cat "$TMP/trace.out" >&2
	exit 1
fi
if ! grep -q "trace-symbol: traced_symbol" "$TMP/trace.out"; then
	echo "FAIL: --trace-symbol did not emit symbol trace lines" >&2
	cat "$TMP/trace.out" >&2
	exit 1
fi

"$LDX" -m64 -r -o "$TMP/out_eq.o" -Map="$TMP/link_eq.map" "$TMP/sym.o"
if [ ! -s "$TMP/link_eq.map" ]; then
	echo "FAIL: -Map=<path> did not produce map output" >&2
	exit 1
fi

"$LDX" --help >"$TMP/help.out" 2>&1
if ! grep -q "usage:" "$TMP/help.out"; then
	echo "FAIL: --help output missing usage text" >&2
	cat "$TMP/help.out" >&2
	exit 1
fi

"$LDX" --version >"$TMP/version.out" 2>&1
if ! grep -q "Substrate ld (internal)" "$TMP/version.out"; then
	echo "FAIL: --version output missing version banner" >&2
	cat "$TMP/version.out" >&2
	exit 1
fi

echo "ok: ld tooling options (-Map/--trace/--trace-symbol/--help/--version)"
