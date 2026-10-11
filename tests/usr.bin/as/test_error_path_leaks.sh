#!/bin/sh
# What a source in error leaves allocated (AS-T-152).
#
# The parser's error paths freed some of what they had and not the
# rest: an operand that could not be read kept its text (`movl %nosuch,
# %eax` lost eleven bytes), and an instruction given up on kept the
# operands read before the bad one.  Nothing is the worse for it in a
# program that exits, which is why it went unseen; it is how a leak in
# a path that matters is hidden among sixteen that do not.
#
# The assembler is built with the leak sanitizer and given sources that
# are each wrong in a different place.  It must refuse each, and the
# sanitizer must have nothing to say.
#
# Run by run-suite.sh; it does not use $AS, having to build its own.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

# The sanitizer, if this compiler has it.
printf 'int main(void){return 0;}\n' > probe.c
if ! ${CC:-cc} -fsanitize=leak -o probe probe.c 2> /dev/null || ! ./probe 2> /dev/null; then
    echo "skip: no leak sanitizer here"
    exit 0
fi

INC="-idirafter $top/include -idirafter $top/sys -idirafter $top/sys/include -I$top/usr.lib/elfobj/src"
SAN="-std=gnu17 -O0 -g -w -fsanitize=leak"
export SAN INC
mkdir obj
ncpu=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
# shellcheck disable=SC2016
if ! ls "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c |
        xargs -P "$ncpu" -n 1 sh -c '${CC:-cc} $SAN $INC -c -o "obj/$(basename "$0" .c).o" "$0"' 2> build.err ||
   ! ${CC:-cc} -fsanitize=leak -o as obj/*.o 2>> build.err; then
    echo "FAIL: the assembler does not build with the leak sanitizer ($(head -1 build.err))"
    exit 1
fi

fail=0
cases=0
while IFS= read -r src; do
    for mode in 32 64; do
        cases=$((cases + 1))
        printf '%b\n' "$src" > t.s
        if ./as "--$mode" -o t.o t.s > out 2>&1; then
            echo "FAIL --$mode [$src]: assembled, and it is here for being wrong"; fail=1
        elif grep -q 'LeakSanitizer' out; then
            echo "FAIL --$mode [$src]: $(grep -m1 'SUMMARY' out)"
            grep -m2 '#[12] ' out | sed 's/^/    /'
            fail=1
        fi
    done
done <<'EOF'
movl %nosuch, %eax
movl (%eax,%ebx,3), %ecx
movl (%eax)junk, %ebx
movl (%eax,%ebx,2,junk), %ecx
movl $1,,%eax
frobnicate %eax
.frobnicate 1
jmp 1f\nnop
x: movl (foo), %eax\nmovl ((, %eax
movl $, %eax
movl %fs:, %eax
jmp *
vaddps %zmm0, %zmm1, %zmm2{%k9}
vaddps %zmm0, %zmm1, %zmm2{bogus}
movl (%eax), {z}
a: b: movl %eax,\nc: nop
.long 1+
.set x
x =
movl sym(%rip,%rax,2), %eax
.intel_syntax noprefix\nmov eax, [ebx+]
.intel_syntax noprefix\nmov eax, [ebx*3]
ljmp $1
movl "a b, %eax
fadd %st(9), %st
.long "unterminated
nop\nbadone\nbadtwo %eax, (%ebx,%ecx,9)\n.long\nmovl $1
movl foo@BOGUS, %eax
jmp foo@GOTOFF
.macro m\nnop
.if 1\nnop
.rept x\nnop\n.endr
.include "nosuchfile.inc"
movl %eax, %ebx\nmovl $1, %nosuch\nmovl (%eax,%ebx,3), %ecx\nmovl %a,%b,%c,%d
EOF

[ "$fail" -eq 0 ] && echo "ok: nothing is left allocated by a source in error ($cases cases)"
exit "$fail"
