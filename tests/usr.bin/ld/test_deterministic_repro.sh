#!/bin/sh
set -eu

# Reqs: LD-U-007

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-deterministic-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

digest() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | awk '{print $1}'
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 "$1" | awk '{print $1}'
	else
		cksum "$1" | awk '{print $1 ":" $2}'
	fi
}

cat > "$TMP/a.c" <<'SRC'
int da(void) { return 1; }
SRC
cat > "$TMP/b.c" <<'SRC'
int db(void) { return 2; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"
gcc -m64 -c -o "$TMP/b.o" "$TMP/b.c"

ref=""
for n in 1 2 3 4 5; do
	out="$TMP/relink_$n.o"
	"$LDX" -m64 -r -o "$out" "$TMP/a.o" "$TMP/b.o"
	h=$(digest "$out")
	if [ -z "$ref" ]; then
		ref="$h"
	elif [ "$h" != "$ref" ]; then
		echo "FAIL: repeated link output is non-deterministic" >&2
		echo "  expected digest: $ref" >&2
		echo "  got digest     : $h" >&2
		exit 1
	fi
done

cat > "$TMP/tie1.c" <<'SRC'
int tie_pick(void) { return 11; }
SRC
cat > "$TMP/tie2.c" <<'SRC'
int tie_pick(void) { return 22; }
SRC
cat > "$TMP/caller.c" <<'SRC'
int tie_pick(void);
int call_tie(void) { return tie_pick(); }
SRC
gcc -m64 -c -o "$TMP/tie1.o" "$TMP/tie1.c"
gcc -m64 -c -o "$TMP/tie2.o" "$TMP/tie2.c"
gcc -m64 -c -o "$TMP/caller.o" "$TMP/caller.c"
ar crs "$TMP/libtie.a" "$TMP/tie1.o" "$TMP/tie2.o"

ref=""
for n in 1 2 3 4 5; do
	out="$TMP/tie_$n.o"
	"$LDX" -m64 -r -L"$TMP" -o "$out" "$TMP/caller.o" -ltie
	h=$(digest "$out")
	if [ -z "$ref" ]; then
		ref="$h"
	elif [ "$h" != "$ref" ]; then
		echo "FAIL: archive extraction/tie-breaking is non-deterministic" >&2
		echo "  expected digest: $ref" >&2
		echo "  got digest     : $h" >&2
		exit 1
	fi
done

echo "ok: repeated links and archive resolution are deterministic"
