#!/bin/sh
# Two labels' distance is that of the code they are in, not the code of
# the instruction that uses it.
#
# Linux's x86-64 decompressor copies a trampoline that begins in 64-bit
# code and ends, after .code32, with
#     ljmpl $__KERNEL_CS, $(.Lret - trampoline_32bit_src)
# where both labels are in the 64-bit part.  The assembler measured the
# instructions between them as the instruction's 32-bit code, in which
# those encoded as they were read (as 64-bit code) have no size, and the
# jump went to the trampoline's start: the kernel reset before it printed
# anything.  Every immediate of such a difference was 0 when the labels
# were in the object's own code and the instruction after a .code16,
# .code32 or .code64 that changed it.
#
# GNU as is the judge: the sections' bytes, --32 and --64, with the
# labels in the object's own code and after a .code of it.
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
GAS=${AS_GNU:-as}
if ! "$GAS" --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as is the judge"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

bytes_of() { objcopy -O binary -j "$2" "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }

for m in 32 64; do
    for use in 16 32 64; do
        [ "$use" = "$m" ] && continue
        for lead in "" ".code$m"; do
            reg=%ebx
            [ "$m" = 64 ] && reg=%rbx
            # (64-bit code has no far jump or call to an immediate)
            far="ljmpl \$0x10, \$(.Lret - src); lcall \$0x10, \$(.Lret - src)"
            [ "$use" = 64 ] && far=nop
            push=pushl
            [ "$use" = 64 ] && push=pushq
            cat > t.s <<EOF
	.section .rodata,"a"
	$lead
src:	push $reg
	pop $reg
	nop
.Lret:	ret
	.code$use
	$far
	movl \$(.Lret - src), %eax
	movl \$((.Lret - src) << 2), %eax
	addl \$(.Lret - src), %ecx
	.text
	.code$m
a:	nop
	nop
b:	nop
	.code$use
	movl \$(b - a), %eax
	$push \$(b - a + 1)
EOF
            what="--$m, labels after '${lead:-nothing}', used after .code$use"
            if ! "$GAS" "--$m" -o g.o t.s 2> err; then
                echo "FAIL $what: GNU as refused it: $(head -1 err)"; fail=1; continue
            fi
            if ! "$AS" "--$m" -o o.o t.s 2> err; then
                echo "FAIL $what: $(head -1 err | sed 's/.*: //')"; fail=1; continue
            fi
            for s in .rodata .text; do
                if [ "$(bytes_of o.o $s)" != "$(bytes_of g.o $s)" ]; then
                    echo "FAIL $what, $s: $(bytes_of o.o $s), GNU as $(bytes_of g.o $s)"; fail=1
                fi
            done
        done
    done
done

if [ $fail = 0 ]; then
    echo "test_label_diff_code_mode: ok"
fi
exit $fail
