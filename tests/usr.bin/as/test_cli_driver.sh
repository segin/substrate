#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-cli-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/simple.s" <<'SRC'
.text
.globl simple_fn
.type simple_fn,@function
simple_fn:
    mov $0, %eax
    ret
.size simple_fn, .-simple_fn
SRC

# 1) baseline -o
"$AS" -32 -o "$TMP/simple32.o" "$TMP/simple.s"
[ -s "$TMP/simple32.o" ]

# 2) explicit -32 / -64
"$AS" -64 -o "$TMP/simple64.o" "$TMP/simple.s"
readelf -h "$TMP/simple32.o" | grep -q "ELF32"
readelf -h "$TMP/simple64.o" | grep -q "ELF64"

# 3) mode inference from invocation name
ln -sf "$AS" "$TMP/as.x64"
ln -sf "$AS" "$TMP/as.x86"
"$TMP/as.x64" -o "$TMP/infer64.o" "$TMP/simple.s"
"$TMP/as.x86" -o "$TMP/infer32.o" "$TMP/simple.s"
readelf -h "$TMP/infer64.o" | grep -q "ELF64"
readelf -h "$TMP/infer32.o" | grep -q "ELF32"

# 4) -g path (use compiler-generated DWARF directives for portability)
cat > "$TMP/debug.c" <<'SRC'
int debug_fn(void) {
    return 7;
}
SRC
gcc -m64 -S -g -o "$TMP/debug.s" "$TMP/debug.c"
"$AS" -64 -g -o "$TMP/debug.o" "$TMP/debug.s"
readelf -S "$TMP/debug.o" | grep -Eq "\\.debug_(line|info)"

# 5) -I include handling (.include)
mkdir -p "$TMP/inc"
cat > "$TMP/inc/defs.inc" <<'SRC'
.equ INC_CONST, 7
SRC
cat > "$TMP/include.s" <<'SRC'
.text
.globl include_fn
.type include_fn,@function
.include "defs.inc"
include_fn:
    mov $INC_CONST, %eax
    ret
.size include_fn, .-include_fn
SRC
"$AS" -32 -I "$TMP/inc" -o "$TMP/include.o" "$TMP/include.s"

# 6) -D predefine handling (cpp path)
cat > "$TMP/define.s" <<'SRC'
#ifndef VALUE
#error VALUE must be defined
#endif
.text
.globl define_fn
.type define_fn,@function
define_fn:
    mov $VALUE, %eax
    ret
.size define_fn, .-define_fn
SRC
"$AS" -32 -DVALUE=11 -o "$TMP/define.o" "$TMP/define.s"

# 7) -Wa passthrough
"$AS" -64 -Wa,--gdwarf-2 -o "$TMP/wa.o" "$TMP/debug.s"
readelf -S "$TMP/wa.o" | grep -Eq "\\.debug_(line|info)"

# 8) -march diagnostics
if "$AS" -32 -march=x86-64-v3 -o "$TMP/badmarch.o" "$TMP/simple.s" >"$TMP/badmarch.out" 2>"$TMP/badmarch.err"; then
    echo "expected -march mismatch to fail"
    exit 1
fi
grep -q "unsupported -march=x86-64-v3 for 32-bit mode" "$TMP/badmarch.err"

# 9) -mtune acceptance/plumbing
"$AS" -64 -march=x86-64-v2 -mtune=generic -o "$TMP/tune.o" "$TMP/simple.s"

# 10) deterministic diagnostics ordering
if "$AS" -32 -march=invalid-cpu -o "$TMP/invalid1.o" "$TMP/simple.s" >"$TMP/inv1.out" 2>"$TMP/inv1.err"; then
    echo "expected invalid -march to fail"
    exit 1
fi
if "$AS" -32 -march=invalid-cpu -o "$TMP/invalid2.o" "$TMP/simple.s" >"$TMP/inv2.out" 2>"$TMP/inv2.err"; then
    echo "expected invalid -march to fail"
    exit 1
fi
cmp "$TMP/inv1.err" "$TMP/inv2.err"

echo "ok: as cli/driver semantics"
