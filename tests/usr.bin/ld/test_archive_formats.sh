#!/bin/sh
set -eu

# Reqs: LD-U-004, LD-R-002

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-archivefmt-$$
EXT=${TMPDIR:-/tmp}/ldx86-archivefmt-ext-$$
mkdir -p "$TMP" "$EXT"
trap 'rm -rf "$TMP" "$EXT"' EXIT INT TERM

cat > "$TMP/thin_member.c" <<'SRC'
int thin_member(void) { return 1234; }
SRC
gcc -m64 -c -o "$TMP/thin_member.o" "$TMP/thin_member.c"

cat > "$TMP/caller_thin.c" <<'SRC'
int thin_member(void);
int thin_call(void) { return thin_member(); }
SRC
gcc -m64 -c -o "$TMP/caller_thin.o" "$TMP/caller_thin.c"

ar crs "$TMP/libregular.a" "$TMP/thin_member.o"
"$LDX" -m64 -r -L"$TMP" -o "$TMP/out_regular.o" "$TMP/caller_thin.o" -lregular

ar crsT "$TMP/libthin.a" "$TMP/thin_member.o"
"$LDX" -m64 -r -L"$TMP" -o "$TMP/out_thin.o" "$TMP/caller_thin.o" -lthin
if ! nm "$TMP/out_thin.o" | grep -q "thin_member"; then
	echo "FAIL: thin archive member was not resolved/extracted" >&2
	exit 1
fi

cat > "$EXT/ext_member.c" <<'SRC'
int ext_member(void) { return 777; }
SRC
gcc -m64 -c -o "$EXT/ext_member.o" "$EXT/ext_member.c"
cat > "$TMP/caller_bad.c" <<'SRC'
int ext_member(void);
int bad_call(void) { return ext_member(); }
SRC
gcc -m64 -c -o "$TMP/caller_bad.o" "$TMP/caller_bad.c"

ar crsT "$TMP/libbad.a" "$EXT/ext_member.o"
if "$LDX" -m64 -r -L"$TMP" -o "$TMP/out_bad.o" "$TMP/caller_bad.o" -lbad >"$TMP/bad_thin.err" 2>&1; then
	echo "FAIL: thin archive with out-of-tree member path unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "invalid thin archive member path" "$TMP/bad_thin.err"; then
	echo "FAIL: missing thin archive safety diagnostic" >&2
	cat "$TMP/bad_thin.err" >&2
	exit 1
fi

echo "ok: ld handles regular/thin archives and enforces thin path safety"
