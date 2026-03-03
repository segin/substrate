#!/bin/sh
set -eu

# Reqs: LD-U-005, LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-visibility-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/vis.c" <<'SRC'
__attribute__((visibility("hidden"))) int hidden_sym(void) { return 5; }
int default_sym(void) { return 6; }
SRC
gcc -m64 -fPIC -fvisibility=default -c -o "$TMP/vis_pic.o" "$TMP/vis.c"
gcc -shared -o "$TMP/libvis.so" "$TMP/vis_pic.o"

cat > "$TMP/caller_hidden.c" <<'SRC'
int hidden_sym(void);
int call_hidden(void) { return hidden_sym(); }
SRC
gcc -m64 -c -o "$TMP/caller_hidden.o" "$TMP/caller_hidden.c"

cat > "$TMP/caller_default.c" <<'SRC'
int default_sym(void);
int call_default(void) { return default_sym(); }
SRC
gcc -m64 -c -o "$TMP/caller_default.o" "$TMP/caller_default.c"

"$LDX" -m64 -shared --as-needed -o "$TMP/out_hidden.so" "$TMP/caller_hidden.o" -L"$TMP" -lvis
readelf -d "$TMP/out_hidden.so" > "$TMP/out_hidden.dynamic"
if grep -q "Shared library: \\[libvis.so\\]" "$TMP/out_hidden.dynamic"; then
	echo "FAIL: hidden-only reference should not select libvis.so provider under visibility rules" >&2
	cat "$TMP/out_hidden.dynamic" >&2
	exit 1
fi

"$LDX" -m64 -shared --as-needed -o "$TMP/out_default.so" "$TMP/caller_default.o" -L"$TMP" -lvis
readelf -d "$TMP/out_default.so" > "$TMP/out_default.dynamic"
if ! grep -q "Shared library: \\[libvis.so\\]" "$TMP/out_default.dynamic"; then
	echo "FAIL: default-visible symbol reference should select libvis.so provider" >&2
	cat "$TMP/out_default.dynamic" >&2
	exit 1
fi

echo "ok: ld visibility-aware DSO symbol candidate resolution"
