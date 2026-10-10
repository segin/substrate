#!/bin/sh
# A symbol given a number is that number in an instruction (AS-T-260).
#
# `.set K, 7` (or `.equ`, or `K = 7`) and then `mov $K, %eax` left the
# immediate 0 with a relocation against K.  A linker makes 7 of that, so
# a linked program was right; but the field could not be the short one,
# an object read without linking was wrong, and `-I` and `-D`, tested
# only by whether the assembler exited 0, were not tested at all.
#
# Each case is assembled twice, once with the symbol and once with the
# number written out, and the two objects' code must be the same bytes
# with no relocation; a few are held to the bytes themselves besides.
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

text_of() { objcopy -O binary -j .text "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }

# same MODE DEFINITION WITH-SYMBOL WITH-NUMBER [BYTES]
same() {
    printf '%s\n\t.text\n\t%s\n' "$2" "$3" > k.s
    printf '\t.text\n\t%s\n' "$4" > n.s
    if ! "$AS" "--$1" -o k.o k.s 2> err; then
        echo "FAIL --$1 '$3': $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    "$AS" "--$1" -o n.o n.s 2> err || { echo "FAIL --$1 '$4': $(head -1 err)"; fail=1; return; }
    k=$(text_of k.o); n=$(text_of n.o)
    [ "$k" = "$n" ] || { echo "FAIL --$1 '$3': $k, and '$4' is $n"; fail=1; }
    if readelf -rW k.o | grep -q ' R_'; then
        echo "FAIL --$1 '$3': a relocation is left"; fail=1
    fi
    if [ $# -ge 5 ] && [ "$k" != "$5" ]; then
        echo "FAIL --$1 '$3': $k, not $5"; fail=1
    fi
}

for m in 32 64; do
    same $m '.set K, 7'   'mov $K, %eax'        'mov $7, %eax'        b807000000
    same $m '.equ K, 7'   'mov $K, %eax'        'mov $7, %eax'        b807000000
    same $m 'K = 7'       'mov $K, %eax'        'mov $7, %eax'        b807000000
    same $m 'K = 7'       'add $K, %ebx'        'add $7, %ebx'
    same $m 'K = 7'       'push $K'             'push $7'
    same $m 'K = 7'       'cmp $-K, %eax'       'cmp $-7, %eax'
    same $m 'K = 7'       'mov $K*2+1, %eax'    'mov $15, %eax'       b80f000000
    same $m 'K = 300'     'add $K, %ebx'        'add $300, %ebx'
    same $m 'K = 7'       'mov K, %ecx'         'mov 7, %ecx'
    same $m 'K = 7'       'mov %ecx, K'         'mov %ecx, 7'
    same $m 'K = 7'       'incl K'              'incl 7'
    # A symbol that is also global is as much a number.
    same $m '.globl K
.set K, 16'               'movl $K, %eax'       'movl $16, %eax'      b810000000
done
same 32 'K = 7'           'mov K(%ebx), %ecx'   'mov 7(%ebx), %ecx'   8b4b07
same 64 'K = 7'           'mov K(%rbx), %ecx'   'mov 7(%rbx), %ecx'   8b4b07
# The one-byte immediate, where the encoder has one: the number is known
# when the instruction is measured.
same 64 'K = 7'           'add $K, %ebx'        'add $7, %ebx'        83c307

# Set twice: each instruction has the value above it.
printf '.set K, 7\n\t.text\n\tmov $K, %%eax\n.set K, 300\n\tmov $K, %%eax\n' > t.s
"$AS" --32 -o t.o t.s 2> err || { echo "FAIL set twice: $(head -1 err)"; fail=1; }
[ "$(text_of t.o)" = "b807000000b82c010000" ] || { echo "FAIL set twice: $(text_of t.o)"; fail=1; }

# Measured ahead of its .set -- a branch over it looks -- and written
# after: the same size both times, or the branch lands short.
printf '\t.text\n\tjmp 1f\n.set K, 7\n\tadd $K, %%ebx\n\t.skip 120\n1:\tret\n' > t.s
"$AS" --64 -o t.o t.s 2> err || { echo "FAIL branch over: $(head -1 err)"; fail=1; }
case "$(text_of t.o)" in
eb7b83c307*c3) ;;
*) echo "FAIL branch over: begins $(text_of t.o | cut -c1-12)"; fail=1 ;;
esac

# A branch to an absolute address is the linker's: it stays a relocation.
printf 'K = 7\n\t.text\n\tjmp K\n' > t.s
"$AS" --32 -o t.o t.s 2> err || { echo "FAIL jmp K: $(head -1 err)"; fail=1; }
readelf -rW t.o | grep -q 'R_386_PC32' || { echo "FAIL jmp K: no R_386_PC32"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: absolute symbols in instructions"
exit "$fail"
