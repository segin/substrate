#!/bin/sh
set -eu

# Reqs: LD-W-001

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SRC="$ROOT/usr.bin/ld/ld.c"

if rg -n 'execv?p?\s*\(.*(ld|ld\.bfd|ld\.gold|ld\.lld)' "$SRC" >/dev/null 2>&1; then
	echo "FAIL: backend-forwarding exec path found in usr.bin/ld/ld.c" >&2
	rg -n 'execv?p?\s*\(.*(ld|ld\.bfd|ld\.gold|ld\.lld)' "$SRC" >&2 || true
	exit 1
fi
if rg -n 'system\s*\(.*(ld|ld\.bfd|ld\.gold|ld\.lld)' "$SRC" >/dev/null 2>&1; then
	echo "FAIL: backend-forwarding system() path found in usr.bin/ld/ld.c" >&2
	rg -n 'system\s*\(.*(ld|ld\.bfd|ld\.gold|ld\.lld)' "$SRC" >&2 || true
	exit 1
fi

echo "ok: usr.bin/ld contains no backend-forwarding path to external ld implementations"
