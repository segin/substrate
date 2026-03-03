#!/bin/sh
set -eu

# Reqs: LD-U-010, LD-E-001

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-unres-prov-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/ref_missing.c" <<'SRC'
extern int missing_from_nowhere;
int use_missing(void) { return missing_from_nowhere; }
SRC
gcc -m64 -c -o "$TMP/ref_missing.o" "$TMP/ref_missing.c"

if "$LDX" -m64 -static -e use_missing -o "$TMP/out_fail" "$TMP/ref_missing.o" >"$TMP/unres.err" 2>&1; then
	echo "FAIL: unresolved ET_EXEC link unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "undefined reference to \`missing_from_nowhere\` (referenced by .*ref_missing.o)" "$TMP/unres.err"; then
	echo "FAIL: unresolved diagnostic missing source provenance" >&2
	cat "$TMP/unres.err" >&2
	exit 1
fi

echo "ok: ld unresolved diagnostics include reference provenance"
