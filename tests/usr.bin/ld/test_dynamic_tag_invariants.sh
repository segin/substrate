#!/bin/sh
set -eu

# Reqs: LD-S-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-dyntag-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/dep.c" <<'SRC'
int dep(void) { return 5; }
SRC
gcc -m64 -fPIC -c -o "$TMP/dep_pic.o" "$TMP/dep.c"
gcc -shared -o "$TMP/libdep.so" "$TMP/dep_pic.o"

cat > "$TMP/main.c" <<'SRC'
int dep(void);
int _start(void) { return dep(); }
SRC
gcc -m64 -fPIC -c -o "$TMP/main.o" "$TMP/main.c"

"$LDX" -m64 -shared -o "$TMP/out.so" "$TMP/main.o" -L"$TMP" -ldep
readelf -d "$TMP/out.so" > "$TMP/dynamic.txt"

for tag in "(NEEDED)" "(STRTAB)" "(SYMTAB)" "(STRSZ)" "(SYMENT)"; do
	if ! grep -q "$tag" "$TMP/dynamic.txt"; then
		echo "FAIL: missing dynamic tag $tag" >&2
		cat "$TMP/dynamic.txt" >&2
		exit 1
	fi
done

null_count=$(grep -c "(NULL)" "$TMP/dynamic.txt" || true)
if [ "$null_count" -ne 1 ]; then
	echo "FAIL: expected exactly one DT_NULL terminator, found $null_count" >&2
	cat "$TMP/dynamic.txt" >&2
	exit 1
fi

echo "ok: ld emits consistent .dynamic core tags and single DT_NULL terminator"
