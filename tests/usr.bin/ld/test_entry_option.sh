#!/bin/sh
set -eu

# Reqs: LD-U-001, LD-U-009

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-entry-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/entry.c" <<'SRC'
void alt(void) { }
SRC

gcc -m64 -c -o "$TMP/entry64.o" "$TMP/entry.c"

if "$LDX" -m64 -static -o "$TMP/default.out" "$TMP/entry64.o" >"$TMP/default.err" 2>&1; then
	echo "FAIL: default entry unexpectedly succeeded without _start" >&2
	exit 1
fi
if ! grep -q "entry symbol '_start' not found" "$TMP/default.err"; then
	echo "FAIL: missing default entry diagnostic" >&2
	cat "$TMP/default.err" >&2
	exit 1
fi

"$LDX" -m64 -static -e alt -o "$TMP/e_alt.out" "$TMP/entry64.o"
"$LDX" -m64 -static --entry alt -o "$TMP/entry_alt_sep.out" "$TMP/entry64.o"
"$LDX" -m64 -static --entry=alt -o "$TMP/entry_alt_eq.out" "$TMP/entry64.o"

if "$LDX" -m64 -static --entry= -o "$TMP/entry_empty.out" "$TMP/entry64.o" >"$TMP/entry_empty.err" 2>&1; then
	echo "FAIL: empty --entry value unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q -- "--entry requires a non-empty symbol name" "$TMP/entry_empty.err"; then
	echo "FAIL: missing empty --entry diagnostic" >&2
	cat "$TMP/entry_empty.err" >&2
	exit 1
fi

echo "ok: ld supports -e and --entry forms"
