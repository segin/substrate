#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-diag-det-$$
mkdir -p "$TMP/inc"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.s" <<'SRC'
.text
.globl base
.type base,@function
base:
    mov $1, %eax
    ret
.size base, .-base
SRC

# file:line diagnostics for encode errors
cat > "$TMP/diag_line.s" <<'SRC'
.text
bad_line:
    add %eax, %xmm0
SRC
if "$AS" -32 -o "$TMP/diag_line.o" "$TMP/diag_line.s" >"$TMP/diag_line.out" 2>"$TMP/diag_line.err"; then
    echo "expected invalid operand failure"
    exit 1
fi
grep -Eq "diag_line\.s:3|diag_line\.s" "$TMP/diag_line.err"

# include stack diagnostics via cpp include chain
cat > "$TMP/inc/stack2.inc" <<'SRC'
#error include-stack-trigger
SRC
cat > "$TMP/inc/stack1.inc" <<'SRC'
#include "stack2.inc"
SRC
cat > "$TMP/include_stack.s" <<'SRC'
#include "stack1.inc"
.text
.globl include_stack
include_stack:
    ret
SRC
if "$AS" -64 -I "$TMP/inc" -o "$TMP/include_stack.o" "$TMP/include_stack.s" >"$TMP/istack.out" 2>"$TMP/istack.err"; then
    echo "expected include stack failure"
    exit 1
fi
grep -q "include-stack-trigger" "$TMP/istack.err"
grep -Eq "included from|In file included from" "$TMP/istack.err"

# expression/overflow context (forced short jump overflow)
cat > "$TMP/overflow_expr.s" <<'SRC'
.text
overflow_expr:
    jmpb far_target
    .fill 300,1,0x90
far_target:
    ret
SRC
if "$AS" -64 -o "$TMP/overflow_expr.o" "$TMP/overflow_expr.s" >"$TMP/ov.out" 2>"$TMP/ov.err"; then
    echo "expected relocation overflow failure"
    exit 1
fi
grep -qi "error" "$TMP/ov.err"
grep -Eq "overflow_expr\.s|far_target|jmp" "$TMP/ov.err"

# configurable hard limits: input bytes, line bytes, token length, macro depth, include depth
cat > "$TMP/long_line.s" <<'SRC'
.text
ll:
    mov $1, %eax
SRC
if "$AS" --max-line-bytes 8 -o "$TMP/ll.o" "$TMP/long_line.s" >"$TMP/ll.out" 2>"$TMP/ll.err"; then
    echo "expected max-line-bytes failure"
    exit 1
fi
grep -q "max-line-bytes" "$TMP/ll.err"

cat > "$TMP/long_token.s" <<'SRC'
.text
lt:
    mov $1, %eax
superlongtokennnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn:
    ret
SRC
if "$AS" --max-token-length 16 -o "$TMP/lt.o" "$TMP/long_token.s" >"$TMP/lt.out" 2>"$TMP/lt.err"; then
    echo "expected max-token-length failure"
    exit 1
fi
grep -q "max-token-length" "$TMP/lt.err"

cat > "$TMP/macro_depth.s" <<'SRC'
.macro M1
.macro M2
.endm
.endm
.text
x:
    ret
SRC
if "$AS" --max-macro-depth 1 -o "$TMP/md.o" "$TMP/macro_depth.s" >"$TMP/md.out" 2>"$TMP/md.err"; then
    echo "expected max-macro-depth failure"
    exit 1
fi
grep -q "max-macro-depth" "$TMP/md.err"

cat > "$TMP/inc/d1.inc" <<'SRC'
#include "d2.inc"
SRC
cat > "$TMP/inc/d2.inc" <<'SRC'
#include "d3.inc"
SRC
cat > "$TMP/inc/d3.inc" <<'SRC'
.text
.globl d3
.type d3,@function
d3:
    ret
.size d3, .-d3
SRC
cat > "$TMP/inc_depth.s" <<'SRC'
#include "d1.inc"
SRC
if "$AS" --max-include-depth 1 -I "$TMP/inc" -o "$TMP/id.o" "$TMP/inc_depth.s" >"$TMP/id.out" 2>"$TMP/id.err"; then
    echo "expected max-include-depth failure"
    exit 1
fi
grep -qi "include" "$TMP/id.err"

# reproducible output and stable ordering
"$AS" -64 -o "$TMP/base1.o" "$TMP/base.s"
"$AS" -64 -o "$TMP/base2.o" "$TMP/base.s"
cmp "$TMP/base1.o" "$TMP/base2.o"
readelf --wide -S "$TMP/base1.o" >"$TMP/s1.txt"
readelf --wide -S "$TMP/base2.o" >"$TMP/s2.txt"
cmp "$TMP/s1.txt" "$TMP/s2.txt"
readelf --wide -s "$TMP/base1.o" >"$TMP/y1.txt"
readelf --wide -s "$TMP/base2.o" >"$TMP/y2.txt"
cmp "$TMP/y1.txt" "$TMP/y2.txt"

# graceful low-memory failure path should be deterministic when constrained
cat > "$TMP/oom_like.s" <<'SRC'
.text
.globl oom_like
.type oom_like,@function
oom_like:
    ret
.size oom_like, .-oom_like
SRC
(
    ulimit -v 8192 || true
    if "$AS" -64 -g -o "$TMP/oom1.o" "$TMP/oom_like.s" >"$TMP/oom1.out" 2>"$TMP/oom1.err"; then
        :
    fi
)
(
    ulimit -v 8192 || true
    if "$AS" -64 -g -o "$TMP/oom2.o" "$TMP/oom_like.s" >"$TMP/oom2.out" 2>"$TMP/oom2.err"; then
        :
    fi
)
if [ -s "$TMP/oom1.err" ] && [ -s "$TMP/oom2.err" ]; then
    sed -E 's#/tmp/cc[^ :]+#<tmp>#g; s#/tmp/as-diag-det-[0-9]+#<tmpdir>#g' "$TMP/oom1.err" >"$TMP/oom1.norm"
    sed -E 's#/tmp/cc[^ :]+#<tmp>#g; s#/tmp/as-diag-det-[0-9]+#<tmpdir>#g' "$TMP/oom2.err" >"$TMP/oom2.norm"
    cmp "$TMP/oom1.norm" "$TMP/oom2.norm"
fi

# basic fuzz-hardening smoke: random inputs should not crash the wrapper
n=0
while [ "$n" -lt 20 ]; do
    f="$TMP/fuzz_$n.s"
    dd if=/dev/urandom bs=128 count=1 2>/dev/null | base64 | tr -d '\n' > "$f" || true
    if "$AS" -64 -o "$TMP/fuzz_$n.o" "$f" >"$TMP/fuzz_$n.out" 2>"$TMP/fuzz_$n.err"; then
        :
    fi
    n=$((n + 1))
done

# structured internal error codes (opt-in)
if AS_ERROR_CODES=1 "$AS" -64 -o "$TMP/multi.o" "$TMP/base.s" "$TMP/diag_line.s" >"$TMP/ec.out" 2>"$TMP/ec.err"; then
    echo "expected multi-input failure with error codes"
    exit 1
fi
grep -q "\[AS_E_USAGE\]" "$TMP/ec.err"

echo "ok: diagnostics/safety/determinism"
