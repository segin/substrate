#!/bin/sh
set -eu

# Reqs: LD-R-002

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-limits-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.c" <<'SRC'
int base(void) { return 0; }
SRC
gcc -m64 -c -o "$TMP/base.o" "$TMP/base.c"

max_depth=66
last="$TMP/inc_$(printf '%03d' "$max_depth").ld"
cat > "$last" <<'SRC'
SECTIONS {
  .text : { *(.text) }
}
SRC

i=$((max_depth - 1))
while [ "$i" -ge 0 ]; do
	cur="$TMP/inc_$(printf '%03d' "$i").ld"
	next="$TMP/inc_$(printf '%03d' $((i + 1))).ld"
	cat > "$cur" <<SRC
INCLUDE "$next"
SRC
	i=$((i - 1))
done

if "$LDX" -m64 -r -T "$TMP/inc_000.ld" -o "$TMP/out.o" "$TMP/base.o" >"$TMP/depth.log" 2>&1; then
	echo "FAIL: include-depth overflow unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q 'INCLUDE depth exceeds limit' "$TMP/depth.log"; then
	echo "FAIL: include-depth limit diagnostic missing" >&2
	sed -n '1,120p' "$TMP/depth.log" >&2
	exit 1
fi

echo "ok: linker script include depth limit is enforced"
