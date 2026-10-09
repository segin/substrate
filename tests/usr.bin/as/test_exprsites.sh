#!/bin/sh
# One expression, the same value wherever it is written.
#
# The assembler had an evaluator for each place an expression could
# stand -- an operand, a data directive, .set, .size, .rept, .if, a
# section's argument -- and they did not agree: `1+2<<3` was 17 in one
# and 24 in another and an undefined symbol in a third.  They are one now
# (usr.bin/as/as_expr.c, itself tested by test_expr.sh); this holds each
# place to it.  The bytes are those GNU as gives.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

${CC:-cc} -O0 -w -o "$work/as" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c 2>/dev/null ||
    { echo "FAIL: the assembler does not build for the host"; exit 1; }

cd "$work" || exit 1
command -v objcopy > /dev/null || { echo "SKIP: no objcopy to take the bytes out with"; exit 0; }

# bytes WHAT SECTION SOURCE BYTES   (\n in SOURCE is a new line)
bytes() {
    printf "$3\n" > t.s
    rm -f t.o
    if ! ./as --32 -o t.o t.s 2> err; then echo "FAIL $1: $(head -1 err)"; fail=1; return; fi
    objcopy -O binary -j "$2" t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    if [ "$got" = "$4" ]; then echo "ok   $1"; else echo "FAIL $1: $got, not $4"; fail=1; fi
}

# refused WHAT SOURCE
refused() {
    printf "$2\n" > t.s
    if ./as --32 -o t.o t.s 2> /dev/null; then echo "FAIL $1: assembled"; fail=1; else echo "ok   $1"; fi
}

# The one expression, 1+2<<3: shifts bind tighter than addition, so 17.
bytes "an immediate"        .text '\tmovl $1+2<<3, %%eax'                     b811000000
bytes "a displacement"      .text '\tmovl 1+2<<3(%%ebx), %%eax'               8b4311
bytes ".long"               .data '\t.data\n\t.long 1+2<<3'                  11000000
bytes ".byte"               .data '\t.data\n\t.byte 1+2<<3'                  11
bytes ".set"                .data '\t.data\n\t.set K, 1+2<<3\n\t.long K'     11000000
bytes "="                   .data '\t.data\nK = 1+2<<3\n\t.long K+1'         12000000
bytes ".rept"               .data '\t.data\n\t.rept 1+2<<3\n\t.byte 7\n\t.endr' \
                                  0707070707070707070707070707070707
bytes ".if, true"           .data '\t.data\n\t.if 1+2<<3 == 17\n\t.byte 1\n\t.else\n\t.byte 2\n\t.endif' 01
bytes ".if, false"          .data '\t.data\n\t.if 1+2<<3 == 24\n\t.byte 1\n\t.else\n\t.byte 2\n\t.endif' 02
bytes ".ifeq"               .data '\t.data\n\t.ifeq 1-1\n\t.byte 1\n\t.else\n\t.byte 2\n\t.endif' 01
bytes ".iflt"               .data '\t.data\n\t.iflt 0-1\n\t.byte 1\n\t.else\n\t.byte 2\n\t.endif' 01
bytes ".fill count"        .data '\t.data\n\t.fill 1+2<<1, 1, 0x66'         6666666666
bytes ".balign"             .data '\t.data\n\t.byte 1\n\t.balign 2+1<<1\n\t.byte 2' 0100000002

# The rest of the grammar, through a directive.
bytes "| above +"           .data '\t.data\n\t.long 1|1+1'                   02000000
bytes "comparison is -1"    .data '\t.data\n\t.long 1==1, 3<>4, 1==2'        ffffffffffffffff00000000
bytes "&& and || are 1"     .data '\t.data\n\t.long 1&&2, 0||0'              0100000000000000
bytes "not"                 .data '\t.data\n\t.long !0, ~0'                  01000000ffffffff
bytes "a character"         .data "\t.data\n\t.byte 'a', 'a'+1"              6162
bytes "binary and octal"    .data '\t.data\n\t.byte 0b101, 017'              050f

# .set of an expression is a value, not a symbol named for its text.
bytes ".set of a product"   .data '\t.data\n\t.set g1, 2*8\n\t.set g2, g1+4\n\t.long g2' 14000000
if command -v readelf > /dev/null; then
    printf '\t.set K, 3*4\n\t.globl K\n' > t.s
    ./as --32 -o t.o t.s 2> err
    if readelf -sW t.o | grep -q ' 0000000c .* ABS K$' && ! readelf -sW t.o | grep -q 'UND 3'; then
        echo "ok   .set K, 3*4 is absolute 12"
    else
        echo "FAIL .set K, 3*4: $(readelf -sW t.o | tail -n +4 | tr -s ' ' | tr '\n' ';')"; fail=1
    fi
    # .size takes the difference of two symbols and a number.
    printf '\t.text\nf:\tnop\n\tnop\ne:\n\t.size f, e - f + 4\n\t.globl f\n' > t.s
    ./as --32 -o t.o t.s 2> err
    if readelf -sW t.o | grep -q ' 6 .* f$'; then echo "ok   .size f, e - f + 4"
    else echo "FAIL .size f, e - f + 4: $(readelf -sW t.o | grep ' f$')"; fail=1; fi
    printf '\t.text\nf:\tnop\n\tnop\n\t.size f, ( . - f )\n\t.globl f\n' > t.s
    ./as --32 -o t.o t.s 2> err
    if readelf -sW t.o | grep -q ' 2 .* f$'; then echo "ok   .size f, ( . - f )"
    else echo "FAIL .size f, ( . - f ): $(readelf -sW t.o | grep ' f$')"; fail=1; fi
fi

# A symbol and an addend in data: one relocation, the addend in place.
bytes "symbol + number"     .data '\t.data\na:\t.long 1\n\t.long a+2*2'      0100000004000000
bytes "number + symbol"     .data '\t.data\na:\t.long 1\n\t.long 4+a'        0100000004000000

# What is not an expression is an error, and never a crash.
refused "two counts to .rept"       '\t.rept 2 3\n\thlt\n\t.endr'
refused "an unclosed parenthesis"   '\t.long undefined_thing('
python3 -c 'print("\t.long " + "("*100000 + "1" + ")"*100000)' > deep.s 2> /dev/null
if [ -s deep.s ]; then
    ./as --32 -o t.o deep.s > /dev/null 2>&1; rc=$?
    if [ "$rc" -eq 1 ]; then echo "ok   100000 parentheses are refused"
    else echo "FAIL 100000 parentheses: exit $rc"; fail=1; fi
    python3 -c 'print("\t.long " + "1+"*100000 + "1")' > long.s
    ./as --32 -o t.o long.s > /dev/null 2>&1; rc=$?
    if [ "$rc" -eq 1 ]; then echo "ok   100000 terms are refused"
    else echo "FAIL 100000 terms: exit $rc"; fail=1; fi
fi
printf '\t.set x, -9223372036854775808/-1\n\tmovl $1/0, %%eax\n' > t.s
./as --32 -o t.o t.s > /dev/null 2>&1; rc=$?
if [ "$rc" -le 1 ]; then echo "ok   a quotient that overflows and a division by zero do not trap"
else echo "FAIL division: exit $rc"; fail=1; fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
