#!/bin/sh
set -eu

# Reqs: LD-E-005, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-needed-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/used.c" <<'SRC'
int used_sym(void) { return 1; }
SRC
gcc -m64 -fPIC -c -o "$TMP/used_pic.o" "$TMP/used.c"
gcc -shared -o "$TMP/libused.so" "$TMP/used_pic.o"

cat > "$TMP/unused.c" <<'SRC'
int unused_sym(void) { return 2; }
SRC
gcc -m64 -fPIC -c -o "$TMP/unused_pic.o" "$TMP/unused.c"
gcc -shared -o "$TMP/libunused.so" "$TMP/unused_pic.o"

cat > "$TMP/caller.c" <<'SRC'
int used_sym(void);
int call_used(void) { return used_sym(); }
SRC
gcc -m64 -c -o "$TMP/caller.o" "$TMP/caller.c"

"$LDX" -m64 -shared --as-needed -o "$TMP/out_as_needed.so" "$TMP/caller.o" -L"$TMP" -lused -lunused
readelf -d "$TMP/out_as_needed.so" > "$TMP/as_needed.dynamic"
if ! grep -q "Shared library: \\[libused.so\\]" "$TMP/as_needed.dynamic"; then
	echo "FAIL: DT_NEEDED missing required libused.so under --as-needed" >&2
	cat "$TMP/as_needed.dynamic" >&2
	exit 1
fi
if grep -q "Shared library: \\[libunused.so\\]" "$TMP/as_needed.dynamic"; then
	echo "FAIL: --as-needed did not drop unused libunused.so DT_NEEDED" >&2
	cat "$TMP/as_needed.dynamic" >&2
	exit 1
fi

"$LDX" -m64 -shared --no-as-needed -o "$TMP/out_no_as_needed.so" "$TMP/caller.o" -L"$TMP" -lused -lunused
readelf -d "$TMP/out_no_as_needed.so" > "$TMP/no_as_needed.dynamic"
if ! grep -q "Shared library: \\[libused.so\\]" "$TMP/no_as_needed.dynamic"; then
	echo "FAIL: DT_NEEDED missing libused.so without --as-needed" >&2
	cat "$TMP/no_as_needed.dynamic" >&2
	exit 1
fi
if ! grep -q "Shared library: \\[libunused.so\\]" "$TMP/no_as_needed.dynamic"; then
	echo "FAIL: DT_NEEDED missing libunused.so without --as-needed" >&2
	cat "$TMP/no_as_needed.dynamic" >&2
	exit 1
fi

echo "ok: ld plans DT_NEEDED entries with --as-needed gating"
