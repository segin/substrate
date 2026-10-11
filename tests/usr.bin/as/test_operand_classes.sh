#!/bin/sh
# A register that is not of a class and a width the instruction takes
# where it is written (AS-T-171).
#
# The encoders took a register by its number and asked no more of it:
#
#   mov %al, %ebx            was mov %eax, %ebx
#   seta %ax                 was seta %al
#   push %al                 pushed %eax
#   fldl (%al)               loaded through %eax
#   movl %eax, (%r8d)        in 32-bit code, stored through %eax
#   movq $1, %mm0            put the one in %rax
#   movl 0xffffffff(%rax),.. read from -1(%rax), the displacement cut
#                            down to the 32 bits it has
#   pushl $1, push %eax      in 64-bit code, which has neither
#
# GNU as refuses each, and so does this assembler now.  Beside every
# refusal here is the instruction as it may be written, which is still
# assembled.
#
# A line is the modes it is tried in, the bytes, which are GNU as's (or
# `refused`), and the source, `|` between its statements.  GNU as passes
# this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

while IFS='	' read -r modes want src; do
    case $modes in '' | '#'*) continue ;; esac
    for mode in $modes; do
        cases=$((cases + 1))
        { echo '	.text'; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
        rm -f t.o
        if "$AS" "--$mode" -o t.o t.s > out 2>&1; then
            got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        else
            got=refused
        fi
        [ "$got" = "$want" ] || { echo "FAIL $mode [$src]: $got, and GNU as: $want"; fail=1; }
    done
done <<'EOF'
# A move between general registers is between two of one width; with a
# segment register, not of a byte, and to memory as a word; with a
# control or debug register, of the width of the mode.
32 64	refused	mov %al,%ebx
32 64	refused	mov %eax,%bl
32	6689c388c389c38ed8668cd88ed88cd88c188c008e00	movw %ax,%bx|movb %al,%bl|mov %eax,%ebx|mov %ax,%ds|mov %ds,%ax|mov %eax,%ds|mov %ds,%eax|movw %ds,(%eax)|mov %es,(%eax)|movw (%eax),%es
64	6689c388c389c38ed8668cd88ed88cd8678c18678c00678e00	movw %ax,%bx|movb %al,%bl|mov %eax,%ebx|mov %ax,%ds|mov %ds,%ax|mov %eax,%ds|mov %ds,%eax|movw %ds,(%eax)|mov %es,(%eax)|movw (%eax),%es
32 64	refused	movb %al,%es
32 64	refused	mov %es,%al
32 64	refused	movl %es,(%eax)
32 64	refused	movl (%eax),%ds
32	0f20c00f22d80f21fb0f23c1	mov %cr0,%eax|mov %eax,%cr3|mov %dr7,%ebx|mov %ecx,%dr0
64	0f20c00f22d80f21fb0f23c1450f20c1	mov %cr0,%rax|mov %rax,%cr3|mov %dr7,%rbx|mov %rcx,%dr0|mov %cr8,%r9
32	refused	mov %cr0,%ax
64	refused	mov %cr0,%eax
64	refused	mov %dr0,%eax
# The arithmetic of two registers.
32 64	refused	add %al,%bx
32 64	refused	cmp %eax,%bx
32 64	refused	test %al,%eax
32 64	refused	xchg %ax,%ebx
32 64	00c36609c331c384ca936639c3	add %al,%bl|or %ax,%bx|xor %eax,%ebx|test %cl,%dl|xchg %eax,%ebx|cmpw %ax,%bx
# Nothing of one byte is pushed, and in 64-bit code nothing of four.
32 64	refused	push %al
32 64	refused	pushb %al
32 64	refused	pushb $1
32 64	refused	popb (%ebx)
32 64	refused	pop %bh
32	505b6a016650665b6650ff308f0500000000061f	push %eax|pop %ebx|pushl $1|pushw %ax|popw %bx|push %ax|pushl (%eax)|popl sym|push %es|pop %ds
64	505b6a016650665b6650ff308f0425000000000fa00fa9	push %rax|pop %rbx|pushq $1|pushw %ax|popw %bx|push %ax|pushq (%rax)|popq sym|push %fs|pop %gs
64	refused	push %eax
64	refused	pushl $1
64	refused	popl sym
64	refused	pushl (%rax)
64	refused	pop %r9d
32	refused	push %rax
# A byte is set, and never moved on a condition.
32 64	refused	seta %ax
32	refused	seta %eax
32	0f97c00f95000f94c30f9fc6	seta %al|setne (%eax)|setz %bl|setg %dh
64	0f97c0670f95000f94c30f9fc6	seta %al|setne (%eax)|setz %bl|setg %dh
32 64	refused	cmovab %al,%bl
32	660f47d80f45d80f4c08	cmova %ax,%bx|cmovne %eax,%ebx|cmovl (%eax),%ecx
64	660f47d80f45d8670f4c08	cmova %ax,%bx|cmovne %eax,%ebx|cmovl (%eax),%ecx
# Instructions that have no form of a byte, or of a word.
32 64	refused	bswap %ax
32 64	refused	bswapw %ax
32 64	0fc80fcb	bswap %eax|bswapl %ebx
64	480fc8490fc9	bswap %rax|bswapq %r9
32 64	refused	lea (%eax),%al
32 64	refused	leab (%eax),%al
32	668d008d5804668d08	lea (%eax),%ax|leal 4(%eax),%ebx|leaw (%eax),%cx
64	67668d00678d580467668d08	lea (%eax),%ax|leal 4(%eax),%ebx|leaw (%eax),%cx
32 64	refused	btb $1,%al
32 64	refused	btsb $1,%al
32	0fbae001660fbae8010fba30010fbbc3	bt $1,%eax|btsw $1,%ax|btrl $1,(%eax)|btcl %eax,%ebx
64	0fbae001660fbae801670fba30010fbbc3	bt $1,%eax|btsw $1,%ax|btrl $1,(%eax)|btcl %eax,%ebx
32 64	refused	movsxb %al,%bl
32 64	refused	movzxb %al,%bl
32 64	0fbec00fb7d80fbec00fb7c0660fbed8	movsbl %al,%eax|movzwl %ax,%ebx|movsx %al,%eax|movzx %ax,%eax|movsbw %al,%bx
# What is in memory and nowhere else, and what is a word.
32 64	refused	lgdt %eax
32 64	refused	sgdt %eax
32 64	refused	invlpg %eax
32 64	refused	lmsw %eax
32	0f01f00f01300f01100f01080f01380f01e0660f01e0	lmsw %ax|lmsw (%eax)|lgdt (%eax)|sidt (%eax)|invlpg (%eax)|smsw %eax|smsw %ax
64	0f01f0670f0130670f0110670f0108670f01380f01e0660f01e0	lmsw %ax|lmsw (%eax)|lgdt (%eax)|sidt (%eax)|invlpg (%eax)|smsw %eax|smsw %ax
32 64	refused	cmpxchg8b %eax
32 64	refused	fnstsw %eax
32	dfe09bdfe0dd38	fnstsw %ax|fstsw %ax|fnstsw (%eax)
64	dfe09bdfe067dd38	fnstsw %ax|fstsw %ax|fnstsw (%eax)
32	refused	rdfsbase %eax
64	f30faec0f3480faed8	rdfsbase %eax|wrgsbase %rax
# A branch through a register goes through one as wide as an address,
# and to no number with a `$` on it.
32 64	refused	jmp *%al
32 64	refused	jmp *$1
32 64	refused	ja $1
32	ffe0ffd3ff20ff150000000066ffe0	jmp *%eax|call *%ebx|jmp *(%eax)|call *sym|jmp *%ax
64	ffe0ffd3ff20ff150000000066ffe0	jmp *%rax|call *%rbx|jmp *(%rax)|call *sym(%rip)|jmp *%ax
64	refused	call *%eax
64	refused	jmp *%eax
64	refused	jmpl *(%rax)
64	refused	calll *(%rax)
# A count in a register is in %cl.
32 64	refused	shll %cx,%eax
32 64	refused	shl %al,%eax
32	d3e066d3f8d208c1e003d1e8d3d3	shll %cl,%eax|sarw %cl,%ax|rorb %cl,(%eax)|shl $3,%eax|shr %eax|rcl %cl,%ebx
64	d3e066d3f867d208c1e003d1e8d3d3	shll %cl,%eax|sarw %cl,%ax|rorb %cl,(%eax)|shl $3,%eax|shr %eax|rcl %cl,%ebx
# A port is read into the accumulator, in the width the name says.
32 64	refused	in $0x60,%rax
32 64	refused	inb %dx,%ax
32 64	refused	outb %ax,%dx
32 64	refused	in %dx,%bl
32 64	e46066ededece501	inb $0x60,%al|inw %dx,%ax|inl (%dx),%eax|in %dx,%al|in $1,%eax
32 64	e68066efefee	outb %al,$0x80|outw %ax,%dx|out %eax,%dx|out %al,(%dx)
# What an address is made of, and the registers 32-bit code has not.
32 64	refused	fldl (%al)
32 64	refused	movl (%es),%eax
32 64	refused	movl (%eax,%mm0),%eax
32 64	refused	movl (%cr0),%eax
32	refused	fldl (%rax)
32	refused	movl %eax,(%r8d)
32	refused	movl (%eax,%r8d),%ecx
32	refused	movl %r8d,%eax
32	refused	movb %sil,%al
32	refused	addl %eax,%xmm8
32	refused	movaps %xmm8,%xmm0
# A displacement that 32 bits do not hold -- but an address computed
# into 32 bits is cut down to them anyway, and with a 32-bit base the
# sum is.
64	refused	movl %eax,0x123456789(%rax)
64	refused	movl %eax,0x80000000(%rax)
64	refused	movl -0x80000001(%rax),%ecx
64	refused	movl 0xffffffff(%rax),%ecx
64	refused	movq $1,0xffffffff(%rax,%rbx)
64	refused	leaq 0x123456789(%rax),%rdx
64	8980ffffff7f8b8800000080678b48ff8d90896745238d50ff678b8800000080	movl %eax,0x7fffffff(%rax)|movl -0x80000000(%rax),%ecx|movl 0xffffffff(%eax),%ecx|leal 0x123456789(%rax),%edx|leal 0xffffffff(%rax),%edx|movl 0x80000000(%eax),%ecx
# movq of a number is a move to a general register.
64	refused	movq $1,%mm0
64	refused	movq $0x123456789abcdef0,%mm0
32 64	refused	movq $1,%xmm0
64	48c7c0010000000f6fc8f30f7ed166480f6ec00f6f00	movq $1,%rax|movq %mm0,%mm1|movq %xmm1,%xmm2|movq %rax,%xmm0|movq (%rax),%mm0
# The instructions of integers take general registers and no others, of
# one width where there are two, and some of no byte.
32 64	refused	inc %xmm0
32 64	refused	mul %mm0
32 64	refused	add $1,%xmm0
32 64	refused	mov %xmm0,%eax
32 64	refused	bsf %xmm1,%xmm2
32 64	refused	bsf %al,%bl
32 64	refused	bsr %eax,%bx
32 64	refused	popcnt %al,%bl
32 64	refused	imul %al,%bl
32 64	refused	xadd %al,%ebx
32 64	refused	cmpxchg %ax,%ebx
32 64	refused	cmova %eax,%bx
32 64	f6eb0fafc36bc3030fc0c30fb1c3660fbcd8f30fb8d8	imul %bl|imul %ebx,%eax|imul $3,%ebx,%eax|xadd %al,%bl|cmpxchg %eax,%ebx|bsf %ax,%bx|popcnt %eax,%ebx
# What is of one kind of vector register is of no other; movd is
# between a vector register and a general one; movsd of two operands
# is of %xmm, and with none is the string instruction.
32 64	refused	movd %xmm1,%xmm2
32 64	refused	movd %mm0,%mm1
32 64	refused	movd %eax,%ebx
32 64	refused	movsd %xmm1,%mm2
32 64	refused	movsd %xmm1,%eax
32 64	refused	paddb %xmm0,%mm1
32 64	refused	cvtsi2sd %xmm0,%xmm1
32	660f6ec00f7ec8f20f10c8f30f1000f20f2ac00f2ac8f30fd6c8f20fd6c8a5	movd %eax,%xmm0|movd %mm1,%eax|movsd %xmm0,%xmm1|movss (%eax),%xmm0|cvtsi2sd %eax,%xmm0|cvtpi2ps %mm0,%xmm1|movq2dq %mm0,%xmm1|movdq2q %xmm0,%mm1|movsd
64	660f6ec00f7ec8f20f10c867f30f1000f20f2ac00f2ac8f30fd6c8f20fd6c8a5	movd %eax,%xmm0|movd %mm1,%eax|movsd %xmm0,%xmm1|movss (%eax),%xmm0|cvtsi2sd %eax,%xmm0|cvtpi2ps %mm0,%xmm1|movq2dq %mm0,%xmm1|movdq2q %xmm0,%mm1|movsd
# A general register where a vector one belongs was taken by its number:
# `vaddps %eax, %xmm0, %xmm1` was `vaddps %xmm0, %xmm0, %xmm1`.  Sixty-one
# instructions have one of each -- the moves between the two, the
# conversions of an integer, the extractions and insertions of an
# element, the masks of signs -- and no other does.
32 64	refused	vaddps %eax,%xmm0,%xmm1
32 64	refused	pshufb %eax,%xmm0
32 64	refused	vfmadd231pd %xmm0,%eax,%xmm1
32 64	refused	addps %eax,%xmm0
32 64	refused	paddb %eax,%mm0
32 64	refused	pxor %xmm0,%eax
32 64	refused	vpxor %eax,%ymm1,%ymm2
32 64	refused	sqrtsd %eax,%xmm0
32 64	refused	aesenc %eax,%xmm1
32 64	refused	pcmpeqb %xmm1,%ebx
32 64	refused	roundps $1,%eax,%xmm0
32 64	refused	vblendps $1,%eax,%xmm1,%xmm2
32 64	refused	crc32 %xmm0,%eax
32 64	refused	vpaddd %zmm0,%eax,%zmm1
32 64	660f6ec0660f7ecb0f6ec0660fc5c001660fc4c001660fd7c00f50c9f20f2ac0f20f2cc0f30f2ac8	movd %eax,%xmm0|movd %xmm1,%ebx|movd %eax,%mm0|pextrw $1,%xmm0,%eax|pinsrw $1,%eax,%xmm0|pmovmskb %xmm0,%eax|movmskps %xmm1,%ecx|cvtsi2sd %eax,%xmm0|cvttsd2si %xmm0,%eax|cvtsi2ssl %eax,%xmm1
32 64	660f3a16c001660f3a22c001660f3a17c001660f3a14c902660f3a20d203	pextrd $1,%xmm0,%eax|pinsrd $1,%eax,%xmm0|extractps $1,%xmm0,%eax|pextrb $2,%xmm1,%ecx|pinsrb $3,%edx,%xmm2
32	c4e37122d001c5f32ad0	vpinsrd $1,%eax,%xmm1,%xmm2|vcvtsi2sd %eax,%xmm1,%xmm2
64	66480f6ec066480f7ec0f2480f2ac066480f3a16c00166480f3a22c001	movq %rax,%xmm0|movq %xmm0,%rax|cvtsi2sdq %rax,%xmm0|pextrq $1,%xmm0,%rax|pinsrq $1,%rax,%xmm0
# Nor general registers alone, in an instruction of vectors: one named
# with a v, one of the 61 with no vector register and no address, one of
# SSE3 to SSE4.1, a comparison by name.
32 64	refused	vaddps %eax,%ebx,%ecx
32 64	refused	vpxor %eax,%ebx,%ecx
32 64	refused	vfmadd231pd %eax,%ebx,%ecx
32 64	refused	vcvtsi2sd %eax,%ebx,%ecx
32 64	refused	cvtsi2sd %eax,%ebx
32 64	refused	pshufb %eax,%ebx
32 64	refused	haddps %eax,%ebx
32 64	refused	pmovzxbw %eax,%ebx
32 64	refused	ptest %eax,%ebx
32 64	refused	cmpeqsd %eax,%ebx
32 64	refused	cmpsd %eax,%ebx
32	660f3800c8f20f7c08660f3817c8f20fc2c800660f3830c8f30f7ec8	pshufb %xmm0,%xmm1|haddps (%eax),%xmm1|ptest %xmm0,%xmm1|cmpeqsd %xmm0,%xmm1|pmovzxbw %xmm0,%xmm1|movq %xmm0,%xmm1
64	660f3800c867f20f7c08660f3817c8f20fc2c800660f3830c8f30f7ec8	pshufb %xmm0,%xmm1|haddps (%eax),%xmm1|ptest %xmm0,%xmm1|cmpeqsd %xmm0,%xmm1|pmovzxbw %xmm0,%xmm1|movq %xmm0,%xmm1
32	c5f32a10c5f058d0	vcvtsi2sd (%eax),%xmm1,%xmm2|vaddps %xmm0,%xmm1,%xmm2
64	4889c348c7c001000000	movq %rax,%rbx|movq $1,%rax
# What has one operand in memory always; verr and verw, which are named
# with a v and are of no vector.
32	refused	movbe %eax,%ebx
32 64	refused	movnti %eax,%ebx
32 64	refused	aadd %eax,%ebx
32	0f00e00f00e80f38f0180f38f1180fc303f20f2a00f20f2c18	verr %ax|verw %ax|movbe (%eax),%ebx|movbe %ebx,(%eax)|movnti %eax,(%ebx)|cvtsi2sd (%eax),%xmm0|cvttsd2si (%eax),%ebx
64	0f00e00f00e8670f38f018670f38f118670fc30367f20f2a0067f20f2c18	verr %ax|verw %ax|movbe (%eax),%ebx|movbe %ebx,(%eax)|movnti %eax,(%ebx)|cvtsi2sd (%eax),%xmm0|cvttsd2si (%eax),%ebx
# No operand where there is none, and no number where a register is.
32 64	refused	hlt %eax
32 64	refused	nop $1
32 64	refused	mov $1,%es
# An address of 64-bit code is made of no register of 16 bits; `(%dx)`
# is how a port is written, and is no address.
64	refused	fldl (%cx)
64	refused	movl (%bx),%eax
64	refused	movl (%rax,%si),%eax
64	refused	incl (%dx)
64	ed6d6e	inl (%dx),%eax|insl (%dx),%es:(%rdi)|outsb (%rsi),(%dx)
# A conditional move of the stack is to its top, and says so.
32 64	refused	fcmovb %st(1)
32 64	dac1dbdb	fcmovb %st(1),%st|fcmovnu %st(3),%st
# The third interrupt has an instruction of one byte to itself.
32 64	cccd80cd04cc	int $3|int $0x80|int $4|int3
# Instructions 64-bit code has not.
64	refused	ljmpq *(%rax)
64	refused	lcallq *(%rax)
64	ff28ff18ff2866ff18	ljmp *(%rax)|lcall *(%rax)|ljmpl *(%rax)|lcallw *(%rax)
64	refused	lgdtl (%rax)
64	refused	sidtw (%rax)
32	0f0110660f01100f0108	lgdtl (%eax)|lgdtw (%eax)|sidtl (%eax)
64	refused	insq
64	refused	outsq
32 64	6c666d6d6e666f6f	insb|insw|insl|outsb|outsw|outsl
64	refused	into
64	refused	salc
64	refused	pushfl
64	refused	popfl
64	refused	leavel
64	refused	retl
32	ced69c9dc9c3	into|salc|pushfl|popfl|leavel|retl
EOF

[ "$fail" -eq 0 ] && echo "ok: operand classes ($cases cases)"
exit "$fail"
