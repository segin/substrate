#!/bin/sh
set -eu

# Reqs: LD-E-001, LD-E-002

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-unres-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/unres.c" <<'SRC'
extern int missing_symbol;
int unresolved_entry(void) { return missing_symbol; }
SRC
gcc -m64 -c -o "$TMP/unres.o" "$TMP/unres.c"

if "$LDX" -m64 -static -e unresolved_entry -o "$TMP/out_exec_default" "$TMP/unres.o" >"$TMP/exec_default.err" 2>&1; then
	echo "FAIL: ET_EXEC default unresolved policy unexpectedly succeeded" >&2
	exit 1
fi

"$LDX" -m64 -shared -o "$TMP/out_shared_default.so" "$TMP/unres.o"

if "$LDX" -m64 -shared --no-undefined -o "$TMP/out_shared_no_undef.so" "$TMP/unres.o" >"$TMP/shared_no_undef.err" 2>&1; then
	echo "FAIL: ET_DYN --no-undefined unexpectedly succeeded" >&2
	exit 1
fi

"$LDX" -m64 -shared --allow-undefined -o "$TMP/out_shared_allow_undef.so" "$TMP/unres.o"

if "$LDX" -m64 -shared --unresolved-symbols=report-all -o "$TMP/out_shared_report_all.so" "$TMP/unres.o" >"$TMP/shared_report_all.err" 2>&1; then
	echo "FAIL: ET_DYN --unresolved-symbols=report-all unexpectedly succeeded" >&2
	exit 1
fi

"$LDX" -m64 -shared --unresolved-symbols=ignore-all -o "$TMP/out_shared_ignore_all.so" "$TMP/unres.o"

echo "ok: ld unresolved-symbol policy matrix by output type and flags"
