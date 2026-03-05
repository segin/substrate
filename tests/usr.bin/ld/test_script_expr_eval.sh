#!/bin/sh
set -eu

# Reqs: LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-script-eval-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.ld" <<'SRC'
foo = 1 + 2 * 3;
bar = (1 + 2) * 3;
baz = ALIGN(5, 4);
ASSERT(foo == 7, "foo expression mismatch");
ASSERT(bar == 9, "bar expression mismatch");
ASSERT(baz == 8, "baz expression mismatch");
SECTIONS { .text : { *(.text) } }
SRC

cat > "$TMP/a.c" <<'SRC'
int expr_eval(void) { return 11; }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"

"$LDX" -m64 -r -T "$TMP/main.ld" -o "$TMP/out.o" "$TMP/a.o"

readelf -s "$TMP/out.o" > "$TMP/syms.txt"

foo_hex=$(awk '$8=="foo"{print $2; exit}' "$TMP/syms.txt")
bar_hex=$(awk '$8=="bar"{print $2; exit}' "$TMP/syms.txt")
baz_hex=$(awk '$8=="baz"{print $2; exit}' "$TMP/syms.txt")

[ "$foo_hex" = "0000000000000007" ] || { echo "FAIL: foo expected 7 got $foo_hex" >&2; exit 1; }
[ "$bar_hex" = "0000000000000009" ] || { echo "FAIL: bar expected 9 got $bar_hex" >&2; exit 1; }
[ "$baz_hex" = "0000000000000008" ] || { echo "FAIL: baz expected 8 got $baz_hex" >&2; exit 1; }

echo "ok: linker script expression evaluator computes arithmetic/precedence/ALIGN"
