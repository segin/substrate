#!/bin/sh
set -eu

# Reqs: LD-E-005

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-as-needed-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

mkdir -p "$TMP/lib"

cat > "$TMP/caller_none.c" <<'SRC'
int caller_none(void) { return 0; }
SRC
gcc -m64 -c -o "$TMP/caller_none.o" "$TMP/caller_none.c"

cat > "$TMP/caller_need.c" <<'SRC'
int need_fn(void);
int caller_need(void) { return need_fn(); }
SRC
gcc -m64 -c -o "$TMP/caller_need.o" "$TMP/caller_need.c"

cat > "$TMP/unused.c" <<'SRC'
int unused_fn(void) { return 1; }
SRC
gcc -m64 -fPIC -c -o "$TMP/unused_pic.o" "$TMP/unused.c"
gcc -shared -o "$TMP/lib/libunused.so" "$TMP/unused_pic.o"

cat > "$TMP/need.c" <<'SRC'
int need_fn(void) { return 2; }
SRC
gcc -m64 -fPIC -c -o "$TMP/need_pic.o" "$TMP/need.c"
gcc -shared -o "$TMP/lib/libneed.so" "$TMP/need_pic.o"

"$LDX" -m64 -r -L"$TMP/lib" -o "$TMP/out_as_needed_unused.o" "$TMP/caller_none.o" --as-needed -lunused

if "$LDX" -m64 -r -L"$TMP/lib" -o "$TMP/out_no_as_needed_unused.o" "$TMP/caller_none.o" --no-as-needed -lunused >"$TMP/no_as_needed.err" 2>&1; then
	echo "FAIL: --no-as-needed unexpectedly accepted unsupported shared-only input" >&2
	exit 1
fi
if ! grep -q "cannot use shared library" "$TMP/no_as_needed.err"; then
	echo "FAIL: missing --no-as-needed shared-library diagnostic" >&2
	cat "$TMP/no_as_needed.err" >&2
	exit 1
fi

if "$LDX" -m64 -r -L"$TMP/lib" -o "$TMP/out_need.err.o" "$TMP/caller_need.o" --as-needed -lneed >"$TMP/as_needed_need.err" 2>&1; then
	echo "FAIL: --as-needed unexpectedly ignored a needed shared-only input" >&2
	exit 1
fi
if ! grep -q "cannot use shared library" "$TMP/as_needed_need.err"; then
	echo "FAIL: missing needed shared-library diagnostic under --as-needed" >&2
	cat "$TMP/as_needed_need.err" >&2
	exit 1
fi

"$LDX" -m64 -r -L"$TMP/lib" -o "$TMP/out_snapshot.o" "$TMP/caller_none.o" --as-needed -lunused --no-as-needed

echo "ok: ld --as-needed/--no-as-needed policy and ordering"
