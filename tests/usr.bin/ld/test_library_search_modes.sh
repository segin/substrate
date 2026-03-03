#!/bin/sh
set -eu

# Reqs: LD-U-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-libsearch-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

mkdir -p "$TMP/lib1" "$TMP/lib2" "$TMP/libso" "$TMP/libbar"

cat > "$TMP/caller_pick.c" <<'SRC'
int pick(void);
int call_pick(void) { return pick(); }
SRC
gcc -m64 -c -o "$TMP/caller_pick.o" "$TMP/caller_pick.c"

cat > "$TMP/caller_so.c" <<'SRC'
int so_only_symbol(void);
int call_so(void) { return so_only_symbol(); }
SRC
gcc -m64 -c -o "$TMP/caller_so.o" "$TMP/caller_so.c"

cat > "$TMP/caller_bar.c" <<'SRC'
int bar_from_archive(void);
int call_bar(void) { return bar_from_archive(); }
SRC
gcc -m64 -c -o "$TMP/caller_bar.o" "$TMP/caller_bar.c"

cat > "$TMP/pick1.c" <<'SRC'
int pick(void) { return 1; }
int pick_src_lib1(void) { return 11; }
SRC
cat > "$TMP/pick2.c" <<'SRC'
int pick(void) { return 2; }
int pick_src_lib2(void) { return 22; }
SRC
gcc -m64 -c -o "$TMP/pick1.o" "$TMP/pick1.c"
gcc -m64 -c -o "$TMP/pick2.o" "$TMP/pick2.c"
ar rcs "$TMP/lib1/libpick.a" "$TMP/pick1.o"
ar rcs "$TMP/lib2/libpick.a" "$TMP/pick2.o"

"$LDX" -m64 -r -L"$TMP/lib1" -L"$TMP/lib2" -o "$TMP/out_pick12.o" "$TMP/caller_pick.o" -lpick
if ! nm "$TMP/out_pick12.o" | grep -q "pick_src_lib1"; then
	echo "FAIL: -L order did not prefer first directory" >&2
	exit 1
fi

"$LDX" -m64 -r -L"$TMP/lib2" -L"$TMP/lib1" -o "$TMP/out_pick21.o" "$TMP/caller_pick.o" -lpick
if ! nm "$TMP/out_pick21.o" | grep -q "pick_src_lib2"; then
	echo "FAIL: reversed -L order did not prefer first directory" >&2
	exit 1
fi

cat > "$TMP/foo.c" <<'SRC'
int so_only_symbol(void) { return 7; }
SRC
gcc -m64 -fPIC -c -o "$TMP/foo_pic.o" "$TMP/foo.c"
gcc -shared -o "$TMP/libso/libfoo.so" "$TMP/foo_pic.o"

if "$LDX" -m64 -r -L"$TMP/libso" -Bstatic -o "$TMP/out_static_fail.o" "$TMP/caller_so.o" -lfoo >"$TMP/static.err" 2>&1; then
	echo "FAIL: -Bstatic unexpectedly accepted .so-only library" >&2
	exit 1
fi
if ! grep -q "cannot find -lfoo" "$TMP/static.err"; then
	echo "FAIL: missing -Bstatic cannot-find diagnostic" >&2
	cat "$TMP/static.err" >&2
	exit 1
fi

if "$LDX" -m64 -r -L"$TMP/libso" -Bdynamic -lfoo -Bstatic -o "$TMP/out_order_fail.o" "$TMP/caller_so.o" >"$TMP/order.err" 2>&1; then
	echo "FAIL: -Bdynamic .so-only library unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "cannot use shared library" "$TMP/order.err"; then
	echo "FAIL: missing ordered -Bdynamic diagnostic for .so candidate" >&2
	cat "$TMP/order.err" >&2
	exit 1
fi

cat > "$TMP/bar.c" <<'SRC'
int bar_from_archive(void) { return 9; }
SRC
gcc -m64 -c -o "$TMP/bar.o" "$TMP/bar.c"
gcc -m64 -fPIC -c -o "$TMP/bar_pic.o" "$TMP/bar.c"
ar rcs "$TMP/libbar/libbar.a" "$TMP/bar.o"
gcc -shared -o "$TMP/libbar/libbar.so" "$TMP/bar_pic.o"

"$LDX" -m64 -r -L"$TMP/libbar" -Bdynamic -o "$TMP/out_bar.o" "$TMP/caller_bar.o" -lbar
if ! nm "$TMP/out_bar.o" | grep -q "bar_from_archive"; then
	echo "FAIL: -Bdynamic did not fall back to archive when shared input is unsupported" >&2
	exit 1
fi

echo "ok: ld -L/-l with ordered -Bstatic/-Bdynamic semantics"
