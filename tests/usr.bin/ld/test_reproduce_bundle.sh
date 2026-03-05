#!/bin/sh
set -eu

# Reqs: LD-U-007

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-repro-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/a.c" <<'SRC'
int ra(void) { return 1; }
SRC
cat > "$TMP/b.c" <<'SRC'
int rb(void) { return 2; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"
gcc -m64 -c -o "$TMP/b.o" "$TMP/b.c"

"$LDX" -m64 -r --reproduce "$TMP/repro" -o "$TMP/out.o" "$TMP/a.o" "$TMP/b.o"

if [ ! -f "$TMP/repro/manifest.txt" ] || [ ! -f "$TMP/repro/repro.sh" ]; then
	echo "FAIL: reproduce bundle missing manifest or repro script" >&2
	find "$TMP/repro" -maxdepth 2 -type f >&2 || true
	exit 1
fi
if [ ! -f "$TMP/repro/input_000.o" ] || [ ! -f "$TMP/repro/input_001.o" ]; then
	echo "FAIL: reproduce bundle missing object snapshots" >&2
	find "$TMP/repro" -maxdepth 2 -type f >&2 || true
	exit 1
fi

LD_TOOL="$LDX" sh "$TMP/repro/repro.sh"
if [ ! -f "$TMP/repro/repro.out" ]; then
	echo "FAIL: repro script did not produce output" >&2
	exit 1
fi

echo "ok: --reproduce emits replay bundle with runnable repro script"
