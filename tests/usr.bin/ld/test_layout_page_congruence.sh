#!/bin/sh
set -eu

# Reqs: LD-U-009, LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-layout-page-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/layout.c" <<'SRC'
int global_data = 42;
int helper(void) { return global_data; }
int _start(void) { return helper(); }
SRC

gcc -m64 -ffunction-sections -fdata-sections -c -o "$TMP/layout.o" "$TMP/layout.c"
"$LDX" -m64 -o "$TMP/layout.out" "$TMP/layout.o"

load_count=0
while read -r off_hex vaddr_hex; do
	off=$((off_hex))
	vaddr=$((vaddr_hex))
	load_count=$((load_count + 1))
	if [ $((off % 4096)) -ne $((vaddr % 4096)) ]; then
		echo "FAIL: PT_LOAD offset/vaddr page congruence violated: off=$off_hex vaddr=$vaddr_hex" >&2
		readelf -Wl "$TMP/layout.out" >&2
		exit 1
	fi
done <<EOF
$(readelf -Wl "$TMP/layout.out" | awk '$1 == "LOAD" { print $2, $3 }')
EOF

if [ "$load_count" -eq 0 ]; then
	echo "FAIL: output has no PT_LOAD segments to validate" >&2
	readelf -Wl "$TMP/layout.out" >&2
	exit 1
fi

echo "ok: ld preserves PT_LOAD page congruence (off % page == vaddr % page)"
