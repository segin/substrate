#!/bin/sh
# 16-bit code with symbols in it (AS-T-347).
#
# What was wrong, each of it silently:
#
#   je sym               a distance of 32 bits behind 0F 84, of which
#                        the processor reads 16 and runs the rest
#   fldl sym, addps sym,%xmm1, bt $1,sym, cmpxchg %ax,sym ...
#                        the address of 32-bit code, r/m 101 and four
#                        bytes, which in 16-bit code is (%di)
#   fldl (%eax)          no address-size prefix: loaded through %bx+%si
#   movl $sym,%eax       a relocation of two bytes on its four
#   call f ... f:        with a call or a long jump between, f was
#                        thought two bytes further on than it is, and
#                        the call went there
#   jc g ... f: ... g:   a jump over 127 bytes was sized as a short one
#                        when the labels after it were placed
#
# and `movw $sym,%ax`, `push $sym` and `addw $sym,%ax` were refused.
#
# A case is the source (a `|` for each new line, after `.code16`), the
# bytes of .text and its relocations as offset:type; both are GNU as
# 2.46's.  Then a boot sector and a trampoline from real mode to long
# mode, assembled and linked flat: the images are GNU's, byte for byte.
# GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# t SOURCE BYTES RELOCATIONS; BYTES may end in `*` for "and more".
t() {
    cases=$((cases + 1))
    printf '\t.text\n\t.code16\n%s\n' "$1" | tr '|' '\n' > t.s
    rm -f t.o
    if ! "$AS" --32 -o t.o t.s > out 2>&1; then
        echo "FAIL [$1]: $(tail -1 out)"; fail=1; return
    fi
    objcopy -O binary --only-section=.text t.o text.bin 2> /dev/null
    got=$(od -An -v -tx1 text.bin | tr -d ' \n')
    rel=$(readelf -rW t.o | awk '/^0/ { sub(/R_386_/, "", $3); printf "%s%x:%s", sep, strtonum("0x" $1), $3; sep = "," }')
    case "$2" in
    *\*) want=${2%\*}; got=$(printf '%s' "$got" | cut -c1-${#want}) ;;
    *) want=$2 ;;
    esac
    [ "$got" = "$want" ] || { echo "FAIL [$1]: bytes $got, and GNU as: $want"; fail=1; }
    [ "$rel" = "$3" ] || { echo "FAIL [$1]: relocations '$rel', and GNU as: '$3'"; fail=1; }
}

# Branches and calls.
t 'call sym' e8feff '1:PC16'
t 'callw sym' e8feff '1:PC16'
t 'jmp sym' e9feff '1:PC16'
t 'je sym' 0f84feff '2:PC16'
t 'jne sym+4' 0f850200 '2:PC16'
t 'loop sym' e2ff '1:PC8'
t 'call *sym' ff160000 '2:16'
t 'jmp *sym(%bx)' ffa70000 '2:16'
t 'lcall *sym' ff1e0000 '2:16'
t 'ljmp $0x8,$sym' ea00000800 '1:16'
t 'lcall $0x8,$sym' 9a00000800 '1:16'
t 'call 1f|1:' e80000 ''
t 'call f|call f|nop|f: ret' e80400e8010090c3 ''
t 'call f|call g|nop|f: ret|g: ret' e80400e8020090c3c3 ''
t 'call f|jc f|nop|f: ret' e80300720190c3 ''
t 'call f|jmp f|nop|f: ret' e80300eb0190c3 ''
t 'call f|x: call f|f: ret' e80300e80000c3 ''
t 'call f|jc n|n: nop|x: call f|f: ret' e80600720090e80000c3 ''
t 'call f|jc n|jne n|jc x|jmp a|n: jc x|a: nop|x: call f|1: hlt|jmp 1b|f: ret' e81100720675047205eb02720190e80300f4ebfdc3 ''
t 'calll f|calll f|nop|f: ret' 66e80700000066e80100000090c3 ''
t 'call f|jmp g|nop|f: ret|.fill 200|g: ret' 'e80400e9ca0090c3*' ''
t 'call f|jc g|nop|f: ret|.fill 200|g: ret' 'e805000f82ca0090c3*' ''
t 'jmp f|jc g|nop|f: ret|.fill 200|g: ret' 'eb050f82ca0090c3*' ''
t 'jmp f|jmp g|nop|f: ret|.fill 200|g: ret' 'eb04e9ca0090c3*' ''
t 'call f|jmp g|jc g|nop|f: ret|.fill 200|g: ret' 'e80800e9ce000f82ca0090c3*' ''

# A number that is a symbol.
t 'movw $sym,%ax' b80000 '1:16'
t 'mov $sym,%bx' bb0000 '1:16'
t 'mov $sym,%ebx' 66bb00000000 '2:32'
t 'movl $sym,%eax' 66b800000000 '2:32'
t 'push $sym' 680000 '1:16'
t 'pushw $sym' 680000 '1:16'
t 'pushl $sym' 666800000000 '2:32'
t 'addw $sym,%ax' 050000 '1:16'
t 'addl $sym,%eax' 660500000000 '2:32'
t 'addw $sym,(%bx)' 81070000 '2:16'
t 'testw $sym,%cx' f7c10000 '2:16'
t 'imul $sym,%bx,%cx' 69cb0000 '2:16'
t 'cmpw $0x100,sym' 813e00000001 '2:16'

# An address that is a symbol: two bytes, in every emitter.
t 'movw sym,%ax' a10000 '1:16'
t 'movw %ax,sym' a30000 '1:16'
t 'movb sym,%al' a00000 '1:16'
t 'movl sym,%eax' 66a10000 '2:16'
t 'movl %eax,sym' 66a30000 '2:16'
t 'movw sym,%bx' 8b1e0000 '2:16'
t 'mov sym(%eax),%bx' 678b9800000000 '3:32'
t 'lea sym(%bx),%ax' 8d870000 '2:16'
t 'incw sym' ff060000 '2:16'
t 'notw sym' f7160000 '2:16'
t 'shlw $1,sym' d1260000 '2:16'
t 'pushw sym' ff360000 '2:16'
t 'popw sym' 8f060000 '2:16'
t 'xchg %ax,sym' 87060000 '2:16'
t 'xadd %ax,sym' 0fc1060000 '3:16'
t 'cmpxchg %ax,sym' 0fb1060000 '3:16'
t 'bt $1,sym' 0fba26000001 '3:16'
t 'shld $1,%ax,sym' 0fa406000001 '3:16'
t 'imul sym,%ax' 0faf060000 '3:16'
t 'movzbw sym,%ax' 0fb6060000 '3:16'
t 'cmovne sym,%ax' 0f45060000 '3:16'
t 'setne sym' 0f95060000 '3:16'
t 'lds sym,%ax' c5060000 '2:16'
t 'boundw %ax,sym' 62060000 '2:16'
t 'lgdt sym' 0f01160000 '3:16'
t 'lidt sym' 0f011e0000 '3:16'
t 'sgdt sym' 0f01060000 '3:16'
t 'lmsw sym' 0f01360000 '3:16'
t 'invlpg sym' 0f013e0000 '3:16'
t 'prefetcht0 sym' 0f180e0000 '3:16'
t 'fldl sym' dd060000 '2:16'
t 'flds sym' d9060000 '2:16'
t 'flds sym(%bx)' d9870000 '2:16'
t 'fstpl sym' dd1e0000 '2:16'
t 'fildl sym' db060000 '2:16'
t 'fisttpl sym' db0e0000 '2:16'
t 'fldt sym' db2e0000 '2:16'
t 'fadds sym' d8060000 '2:16'
t 'fldcw sym' d92e0000 '2:16'
t 'fnstcw sym' d93e0000 '2:16'
t 'fsave sym' 9bdd360000 '3:16'
t 'fxsave sym' 0fae060000 '3:16'
t 'fldl 4(%bx)' dd4704 ''
t 'fldl (%eax)' 67dd00 ''
t 'movups sym,%xmm0' 0f10060000 '3:16'
t 'addps sym,%xmm1' 0f580e0000 '3:16'
t 'movd sym,%xmm0' 660f6e060000 '4:16'
t 'paddb sym,%mm1' 0ffc0e0000 '3:16'
t 'vaddps sym,%xmm1,%xmm2' c5f058160000 '4:16'
t 'popcnt sym,%ax' f30fb8060000 '4:16'
t 'movbe sym,%ax' 0f38f0060000 '4:16'
t 'crc32b sym,%eax' f20f38f0060000 '5:16'

# The same under .code16gcc, where a call is of 32 bits (AS-T-348).
t '.code16gcc|call sym' 66e8fcffffff '2:PC32'
t '.code16gcc|jmp sym|je sym' e9feff0f84feff '1:PC16,5:PC16'
t '.code16gcc|mov sym,%eax|mov $sym,%eax' 66a1000066b800000000 '2:16,6:32'
t '.code16gcc|call *(%eax)|jmp *sym' 6766ff10ff260000 '6:16'

# A boot sector, and a trampoline from real mode to long mode.
cat > boot.s << 'EOF'
	.code16
	.text
	.globl _start
_start:
	cli
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	$0x7c00, %sp
	sti
	movb	%dl, bootdrv
	movw	$msg, %si
	call	puts
	movb	$0x41, %ah
	movw	$0x55aa, %bx
	int	$0x13
	jc	nolba
	cmpw	$0xaa55, %bx
	jne	nolba
	movw	$dap, %si
	movb	$0x42, %ah
	movb	bootdrv, %dl
	int	$0x13
	jc	fail
	jmp	a20
nolba:
	movw	$0x0201, %ax
	movw	$0x0002, %cx
	xorb	%dh, %dh
	movw	$0x7e00, %bx
	int	$0x13
	jc	fail
a20:
	inb	$0x92, %al
	orb	$2, %al
	outb	%al, $0x92
	lgdt	gdtr
	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	ljmp	$0x08, $pm
fail:
	movw	$err, %si
	call	puts
1:	hlt
	jmp	1b
puts:
	lodsb
	testb	%al, %al
	jz	2f
	movb	$0x0e, %ah
	movw	$7, %bx
	int	$0x10
	jmp	puts
2:	ret
	.code32
pm:
	movw	$0x10, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movl	$0x90000, %esp
	movl	$0x7e00, %eax
	pushl	bootdrv
	call	*%eax
3:	hlt
	jmp	3b
	.code16
msg:	.asciz	"Loading"
err:	.asciz	" error"
bootdrv: .byte	0
	.align	4
dap:	.byte	0x10, 0
	.word	8
	.word	0x7e00, 0
	.quad	1
gdt:	.quad	0
	.quad	0x00cf9a000000ffff
	.quad	0x00cf92000000ffff
gdtr:	.word	gdtr - gdt - 1
	.long	gdt
	.org	510
	.word	0xaa55
EOF
cat > tramp.s << 'EOF'
	.code16
	.text
	.globl tramp_start, tramp_end
tramp_start:
	cli
	cld
	movw	%cs, %ax
	movw	%ax, %ds
	lidtl	tramp_idt - tramp_start
	lgdtl	tramp_gdt - tramp_start
	movl	$0x11, %eax
	lmsw	%ax
	ljmpl	$0x08, $tramp32
	.code32
tramp32:
	movl	$0x10, %eax
	movw	%ax, %ds
	movw	%ax, %ss
	movl	tramp_stack, %esp
	movl	%cr4, %eax
	orl	$0x20, %eax
	movl	%eax, %cr4
	movl	tramp_cr3, %eax
	movl	%eax, %cr3
	movl	$0xc0000080, %ecx
	rdmsr
	btsl	$8, %eax
	wrmsr
	movl	%cr0, %eax
	orl	$0x80000000, %eax
	movl	%eax, %cr0
	pushl	$0x18
	pushl	$tramp64
	lret
tramp64:
	hlt
	.code16
	.align	8
tramp_idt:
	.word	0
	.long	0
tramp_gdt:
	.word	31
	.long	tramp_gdt_table
tramp_gdt_table:
	.quad	0, 0x00cf9a000000ffff, 0x00cf92000000ffff, 0x00af9a000000ffff
tramp_stack:
	.long	0
tramp_cr3:
	.long	0
tramp_end:
EOF
printf 'SECTIONS { . = 0x7c00; .text : { *(.text) } /DISCARD/ : { *(*) } }\n' > flat.ld

# image NAME BYTES MD5: the flat image, which is GNU as's.
image() {
    cases=$((cases + 1))
    if ! "$AS" --32 -o "$1.o" "$1.s" 2> err; then
        echo "FAIL $1.s: $(tail -1 err)"; fail=1; return
    fi
    if ! ld -m elf_i386 -T flat.ld --oformat binary -o "$1.img" "$1.o" 2> err; then
        echo "FAIL $1.s: does not link: $(tail -1 err)"; fail=1; return
    fi
    got="$(wc -c < "$1.img" | tr -d ' ') $(md5sum < "$1.img" | cut -c1-32)"
    [ "$got" = "$2 $3" ] || { echo "FAIL $1.s: the image is $got, and GNU as's: $2 $3"; fail=1; }
}
if command -v ld > /dev/null 2>&1 && ld -V 2> /dev/null | grep -q elf_i386; then
    image boot 512 6412dcb17d5515e6e4f46e3fe4840714
    image tramp 156 37e8a0273364bf57fb9c358b973a1e86
fi

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
