#!/bin/sh
# Relocation operators in i386 code (AS-T-200, AS-T-202, AS-T-203, in
# part).
#
# Position-independent i386 code reaches everything through the global
# offset table: `sym@GOTOFF(%ebx)` for what is in this object,
# `sym@GOT(%ebx)` for what may not be, `_GLOBAL_OFFSET_TABLE_` to find the
# table.  The assembler knew @PLT and no more.  Any other operator stayed
# in the symbol's name: `leal sym@GOTOFF(%ebx), %eax` was an R_386_32
# against an undefined symbol called "sym@GOTOFF", and nothing built
# -fPIC or -fPIE for i386 could be linked.
#
# The bytes and relocations are GNU as's.  Run by run-suite.sh, which sets
# $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# check WHAT SOURCE TEXT-BYTES RELOCATIONS
check() {
    printf "$2" > t.s
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$3" ] || { echo "FAIL $1: $got, not $3"; fail=1; }
    rel=$(readelf -rW t.o | awk '/ R_/ { print $1, $3, $5 }' | sed 's/^0*//' | tr '\n' ';')
    [ "$rel" = "$4" ] || { echo "FAIL $1: relocations '$rel', not '$4'"; fail=1; }
    # No symbol keeps an operator in its name.
    if readelf -sW t.o | awk '{ print $8 }' | grep -q '@'; then
        echo "FAIL $1: a symbol with @ in its name: $(readelf -sW t.o | awk '{ print $8 }' | grep '@')"; fail=1
    fi
}

check "@GOTOFF"   '\tleal sym@GOTOFF(%%ebx), %%eax\n'   8d8300000000   '2 R_386_GOTOFF sym;'
check "@PLT"      '\tcall foo@PLT\n'                    e8fcffffff     '1 R_386_PLT32 foo;'
check "@NTPOFF"   '\tmovl foo@NTPOFF(%%eax), %%ecx\n'   8b8800000000   '2 R_386_TLS_LE foo;'
check "@TLSGD"    '\tleal foo@TLSGD(,%%ebx,1), %%eax\n' 8d041d00000000 '3 R_386_TLS_GD foo;'
check "@GOTNTPOFF" '\tmovl foo@GOTNTPOFF(%%ebx), %%eax\n' 8b8300000000 '2 R_386_TLS_GOTIE foo;'
check "an x87 load of a constant" \
      '\tflds .LC0@GOTOFF(%%ebx)\n\t.section .rodata\n.LC0:\t.long 1\n' \
      d98300000000 '2 R_386_GOTOFF .rodata;'

# The table by name: relative to the field, so the field's place in its
# instruction is added, as GNU as adds it.
check "_GLOBAL_OFFSET_TABLE_" '\taddl $_GLOBAL_OFFSET_TABLE_, %%ebx\n' \
      81c302000000 '2 R_386_GOTPC _GLOBAL_OFFSET_TABLE_;'

# @GOT: GNU as marks a mov's with the relaxable R_386_GOT32X; R_386_GOT32
# says the same to a linker.
printf '\tmovl sym@GOT(%%ebx), %%eax\n' > t.s
"$AS" --32 -o t.o t.s 2> err || { echo "FAIL @GOT: $(head -1 err)"; fail=1; }
case "$(readelf -rW t.o | awk '/ R_/ { print $3, $5 }')" in
"R_386_GOT32 sym"|"R_386_GOT32X sym") ;;
*) echo "FAIL @GOT: $(readelf -rW t.o | awk '/ R_/ { print $3, $5 }')"; fail=1 ;;
esac

# In data: a position-independent jump table's entries.
printf '\t.text\n.L1:\tnop\n\t.data\n\t.long .L1@GOTOFF\n\t.long ext@GOTOFF\n' > t.s
"$AS" --32 -o t.o t.s 2> err || { echo "FAIL @GOTOFF in data: $(head -1 err)"; fail=1; }
rel=$(readelf -rW t.o | awk '/ R_/ { print $3 }' | tr '\n' ';')
[ "$rel" = 'R_386_GOTOFF;R_386_GOTOFF;' ] || { echo "FAIL @GOTOFF in data: '$rel'"; fail=1; }
if readelf -sW t.o | awk '{ print $8 }' | grep -q '@'; then
    echo "FAIL @GOTOFF in data: a symbol with @ in its name"; fail=1
fi

# A versioned name is not an operator.
printf '\t.globl f\nf:\tret\n\t.symver f, f@@V1\n' > t.s
"$AS" --32 -o t.o t.s 2> err || { echo "FAIL .symver: $(head -1 err)"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: i386 relocation operators"
exit "$fail"
