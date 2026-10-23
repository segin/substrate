#!/bin/sh
# Linux's alternative padding, sized in a scan inside a scan.
#
# Labels the assembler places from a table built inside another layout
# scan counted a .skip there as nothing, the padding of an alternative
# among them.  In Linux's entry_32.S the IRQ entry stubs (a .rept with
# `.fill 0b + 8 - .`) make that nesting, and every .altinstructions
# record after an alternative with padding pointed that much short of
# its instruction: patched at boot, the int $0x80 entry jumped into the
# middle of one, and /init died with an invalid opcode.
#
# The second record's place is 1795 (the first alternative pads its
# empty original to clac's 3 bytes); GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

cat > t.s <<'EOF'
.macro ANNOTATE_DATA type
.endm
.macro altinstr_entry orig alt ft_flags orig_len alt_len
	.long \orig - .
	.long \alt - .
	.4byte \ft_flags
	.byte \orig_len
	.byte \alt_len
.endm
.macro ALTERNATIVE oldinstr, newinstr, ft_flags
	740:										\oldinstr	;							741:										.skip -(((744f-743f)-(741b-740b)) > 0) * ((744f-743f)-(741b-740b)),0x90	;742:										.pushsection .altinstructions, "aM", @progbits, 14   ;	altinstr_entry 740b,743f,\ft_flags,742b-740b,744f-743f ;			.popsection ;								.pushsection .altinstr_replacement,"ax"	;			743:										ANNOTATE_DATA type=1 ;							\newinstr	;							744:										.popsection ;
.endm
.macro idtentry vector asmsym cfunc has_error_code:req
	ALTERNATIVE "", "clac", ( 9*32+20)  
.endm
.macro idtentry_irq vector cfunc
 ;					.balign 4, 0x90; ;						asm_\cfunc:
.endm
    .rept (256 - 0x20)
0 :
	jmp	asm_common_interrupt
	.fill 0b + (8 * (1 + 0)) - ., 1, 0xcc
    .endr
idtentry 3 asm_exc_int3 exc_int3 has_error_code=0;
idtentry 14 asm_exc_page_fault exc_page_fault has_error_code=1;
idtentry_irq 0xFFFF common_interrupt;
EOF
if ! "$AS" --32 -o t.o t.s 2> err; then
    echo "FAIL: $(tail -1 err)"; exit 1
fi
second=$(objcopy -O binary -j .altinstructions t.o /dev/stdout | od -An -v -tu4 -j 14 -N 4 | tr -d ' ')
[ "$second" = "1795" ] || { echo "FAIL: the second alternative's record says $second, not 1795"; exit 1; }
echo "ok: alternative padding in a nested layout scan"
