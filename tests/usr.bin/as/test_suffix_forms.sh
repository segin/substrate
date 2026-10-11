#!/bin/sh
# Instructions written with a size suffix their instruction takes
# (AS-T-172), and the instructions found on the way that were not
# assembled at all.
#
# The suffix was taken off a list of some sixty mnemonics and no other,
# so `shldl`, `larl`, `movntil`, `lssl`, `iretl`, `movbew`, `adcxl`,
# `andnl`, `bzhiq` and `tzcntw` were each "unsupported mnemonic" where
# the same instruction without its suffix was assembled.  And four
# things were not assembled with or without one:
#
#   - `rorx`, and in 32-bit code `blsi`, `blsr` and `blsmsk`: the
#     encoders had them and nothing sent a statement there;
#   - `adcx` and `adox` in 64-bit code;
#   - `tzcnt` of 16 bits in 64-bit code.
#
# A line is the modes it is tried in, the bytes, which are GNU as's (or
# `refused`), and the source, `|` between its statements.  In 64-bit mode
# this assembler is asked for -march=x86-64-v3, without which it refuses
# BMI; GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
gnu=0
"$AS" --version 2> /dev/null | grep -q '^GNU assembler' && gnu=1

while IFS='	' read -r modes want src; do
    case $modes in '' | '#'*) continue ;; esac
    for mode in $modes; do
        cases=$((cases + 1))
        { echo '	.text'; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
        rm -f t.o
        march=
        [ "$mode" = 64 ] && [ "$gnu" = 0 ] && march=-march=x86-64-v3
        # shellcheck disable=SC2086
        if "$AS" "--$mode" $march -o t.o t.s > out 2>&1; then
            got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        else
            got=refused
        fi
        [ "$got" = "$want" ] || { echo "FAIL $mode [$src]: $got, and GNU as: $want"; fail=1; }
    done
done <<'EOF'
# Double shifts.
32 64	0fa4c301660facc301660fa5c3	shldl $1,%eax,%ebx|shrdw $1,%ax,%bx|shldw %cl,%ax,%bx
32	0fad03	shrdl %cl,%eax,(%ebx)
64	480fa4c301480fad03	shldq $1,%rax,%rbx|shrdq %cl,%rax,(%rbx)
# Access rights and segment limits.
32 64	0f02d80f03d8660f02d8	larl %eax,%ebx|lsll %eax,%ebx|larw %ax,%bx
32	660f0318	lslw (%eax),%bx
# Non-temporal store.
32	0fc303	movntil %eax,(%ebx)
64	480fc303	movntiq %rax,(%rbx)
# Bounds, and loads of a far pointer.
32	6203666203	boundl %eax,(%ebx)|boundw %ax,(%ebx)
32	0fb2180fb4180fb518660fb218660fb418	lssl (%eax),%ebx|lfsl (%eax),%ebx|lgsl (%eax),%ebx|lssw (%eax),%bx|lfsw (%eax),%bx
32	c518c41866c51866c418	ldsl (%eax),%ebx|lesl (%eax),%ebx|ldsw (%eax),%bx|lesw (%eax),%bx
# Returns.
32 64	cfcb66cf66cb	iretl|lretl|iretw|lretw
64	48cf48cb	iretq|lretq
32 64	ca040066ca0400	lretl $4|lretw $4
# Byte-swapping moves.
32	660f38f1030f38f0030f38f103	movbew %ax,(%ebx)|movbel (%ebx),%eax|movbel %eax,(%ebx)
64	480f38f103480f38f003	movbeq %rax,(%rbx)|movbeq (%rbx),%rax
# Add with carry and with overflow, which 64-bit code did not have.
32 64	660f38f6d8f30f38f6d8	adcxl %eax,%ebx|adoxl %eax,%ebx
32	660f38f618	adcxl (%eax),%ebx
64	66480f38f6d8f3480f38f6d8	adcxq %rax,%rbx|adoxq %rax,%rbx
64	660f38f6d8f34d0f38f6c8664a0f38f61c40f30f38f618	adcx %eax,%ebx|adox %r8,%r9|adcx (%rax,%r8,2),%rbx|adox (%rax),%ebx
# BMI: by name, with the suffix and without.
32 64	c4e260f2c8c4e278f5cbc4e260f3d8c4e260f3c8c4e260f3d0	andnl %eax,%ebx,%ecx|bzhil %eax,%ebx,%ecx|blsil %eax,%ebx|blsrl %eax,%ebx|blsmskl %eax,%ebx
32 64	c4e278f7cbc4e263f5c8c4e262f5c8c4e263f6c8	bextrl %eax,%ebx,%ecx|pdepl %eax,%ebx,%ecx|pextl %eax,%ebx,%ecx|mulxl %eax,%ebx,%ecx
32 64	c4e37bf0d801c4e27af7cbc4e279f7cbc4e27bf7cb	rorxl $1,%eax,%ebx|sarxl %eax,%ebx,%ecx|shlxl %eax,%ebx,%ecx|shrxl %eax,%ebx,%ecx
64	c4e2e0f2c8c4e2f8f5cbc4e2e0f3d8c4e2e0f3c8c4e2e0f3d0	andnq %rax,%rbx,%rcx|bzhiq %rax,%rbx,%rcx|blsiq %rax,%rbx|blsrq %rax,%rbx|blsmskq %rax,%rbx
64	c4e2f8f7cbc4e2e3f5c8c4e2e2f5c8c4e2e3f6c8	bextrq %rax,%rbx,%rcx|pdepq %rax,%rbx,%rcx|pextq %rax,%rbx,%rcx|mulxq %rax,%rbx,%rcx
64	c4e3fbf0d801c4e2faf7cbc4e2f9f7cbc4e2fbf7cb	rorxq $1,%rax,%rbx|sarxq %rax,%rbx,%rcx|shlxq %rax,%rbx,%rcx|shrxq %rax,%rbx,%rcx
32	c4e260f208c4e260f318c4e278f50b	andnl (%eax),%ebx,%ecx|blsil (%eax),%ebx|bzhil %eax,(%ebx),%ecx
32 64	c4e37bf0d801c4e260f3d8c4e260f3c8c4e260f3d0	rorx $1,%eax,%ebx|blsi %eax,%ebx|blsr %eax,%ebx|blsmsk %eax,%ebx
64	c4e3fbf0d801c4e37bf01801c443fbf0f807c4c230f3d8c4e2e0f308c4e2a8f3d0	rorx $1,%rax,%rbx|rorx $1,(%rax),%ebx|rorx $7,%r8,%r15|blsi %r8d,%r9d|blsr (%rax),%rbx|blsmsk %rax,%r10
# Counts of 16 bits.
32 64	66f30fbcd8	tzcntw %ax,%bx
32	f30fbc18	tzcntl (%eax),%ebx
64	66f30fbcd866f30fbdd866f30fb8d866f30fbc1866f3450fbcc8	tzcnt %ax,%bx|lzcnt %ax,%bx|popcnt %ax,%bx|tzcnt (%rax),%bx|tzcntw %r8w,%r9w
# shld and shrd with the count left out: it is %cl (AS-T-173).
32 64	0fa5c30fadc3660fa5c3	shld %eax,%ebx|shrd %eax,%ebx|shld %ax,%bx
32	0fa5030fad03	shld %eax,(%ebx)|shrdl %eax,(%ebx)
64	480fa5c3480fad03	shld %rax,%rbx|shrd %rax,(%rbx)
# A suffix that a register denies (AS-T-170): each was assembled, the
# suffix or the register winning as it fell out.
32 64	refused	movl %ax,%ebx
32 64	refused	addw %al,%bx
32 64	refused	movb %eax,%bl
32 64	refused	movw %eax,%ebx
32 64	refused	addl %al,%ebx
32 64	refused	subb %ax,%bl
32 64	refused	incl %ax
32 64	refused	negw %eax
32 64	refused	notb %ax
32 64	refused	pushw %eax
32 64	refused	movl $1,%ax
32 64	refused	addb $1,%eax
32 64	refused	cmovel %ax,%bx
32 64	refused	shll $1,%ax
64	refused	movq %eax,%rbx
64	refused	addq %ax,%rbx
32 64	refused	movsbl %ax,%ebx
32 64	refused	movzbl %al,%bx
32 64	refused	movswl %eax,%ebx
64	refused	movslq %ax,%rbx
# And one that no register denies.
32 64	d3e066d3e0d2e0	shll %cl,%eax|shlw %cl,%ax|shlb %cl,%al
32 64	8ed88cd8	movw %ax,%ds|movl %ds,%eax
32	0f22c00f20c0	movl %eax,%cr0|movl %cr0,%eax
32 64	0fbed80fb7d8660fbed8	movsbl %al,%ebx|movzwl %ax,%ebx|movsbw %al,%bx
64	4863d8480fbed8480fb7d8	movslq %eax,%rbx|movsbq %al,%rbx|movzwq %ax,%rbx
# The shorter of two encodings (some of AS-T-186): a push of a number a
# byte holds, a shift by one, an exchange with the accumulator.
32 64	6a016a806a7f6880000000	push $1|push $-128|push $127|push $128
32 64	666a01666834126800000000	pushw $1|pushw $0x1234|push $sym
32	6a01	pushl $1
64	6a01	pushq $1
32 64	d0e1d1e066d1e8d1c0d0db	shl $1,%cl|shl $1,%eax|shr $1,%ax|rol $1,%eax|rcr $1,%bl
32	d138d12066d138d008	sar $1,(%eax)|shll $1,(%eax)|sarw $1,(%eax)|rorb $1,(%eax)
32 64	c1e002d1e0d3e0	shl $2,%eax|shl %eax|shl %cl,%eax
32 64	939366936693916690	xchg %eax,%ebx|xchg %ebx,%eax|xchg %ax,%bx|xchg %bx,%ax|xchgl %eax,%ecx|xchg %ax,%ax
32	90	xchg %eax,%eax
64	87c0	xchg %eax,%eax
64	4893489349904990904190664190	xchg %rax,%rbx|xchg %rbx,%rax|xchg %rax,%r8|xchg %r8,%rax|xchg %rax,%rax|xchg %eax,%r8d|xchg %r8w,%ax
32 64	86c3	xchg %al,%bl
32	8703	xchg %eax,(%ebx)
# The ALU group and test: a byte of immediate where a signed byte holds
# the number, and with %al or the accumulator the form that names no
# register.  In 32-bit code every one of these was the long form.
32	83c00183c37f81c3800000008328ff6683f80183e4f0	addl $1,%eax|addl $127,%ebx|addl $128,%ebx|subl $-1,(%eax)|cmpw $1,%ax|andl $-16,%esp
32 64	04016683c00183c001054523010083f801a901000000a801	add $1,%al|add $1,%ax|add $1,%eax|add $0x12345,%eax|cmp $1,%eax|test $1,%eax|test $1,%al
32 64	1c0180db0183d8011d00010000	sbb $1,%al|sbb $1,%bl|sbb $1,%eax|sbb $256,%eax
32	f6c30166a9010066a934128403840b	test $1,%bl|testw $1,%ax|test $0x1234,%ax|test %al,(%ebx)|testb (%ebx),%cl
32	85c0850385038503	test %eax,%eax|test (%ebx),%eax|testl (%ebx),%eax|test %eax,(%ebx)
64	85c0488503488503	test %eax,%eax|test (%rbx),%rax|test %rax,(%rbx)
# A push or a pop of 64 bits has no REX.W: `pushq %rbp` was 48 55.
64	554154ff305b415f8f006650665b	pushq %rbp|push %r12|pushq (%rax)|popq %rbx|pop %r15|popq (%rax)|pushw %ax|popw %bx
64	4958	rex.WB popq %rax
# imul by a number, with the register it goes to left out.
32 64	6bc0026bd80269d82c010000	imul $2,%eax|imul $2,%eax,%ebx|imul $300,%eax,%ebx
32 64	666bdbfd6669c9e80369c000000000	imul $-3,%bx|imulw $1000,%cx|imul $sym,%eax
32	6b1802	imull $2,(%eax),%ebx
64	486bc0024d6bc002456bc902	imul $2,%rax|imulq $2,%r8|imul $2,%r9d
# The rest of AS-T-184: xacquire and xrelease, which were "rep/repne
# prefix not allowed", the instructions of VMX with no operand, and a
# segment register moved to and from an address in 64-bit code.
32	f2f0830001f3f0832801f38903	xacquire lock addl $1,(%eax)|xrelease lock subl $1,(%eax)|xrelease movl %eax,(%ebx)
32	f2f08703f28703f3f0ff08f2f00fb119	xacquire lock xchg %eax,(%ebx)|xacquire xchg %eax,(%ebx)|xrelease lock decl (%eax)|xacquire lock cmpxchg %ebx,(%ecx)
32	f2f00fc119f2f00fba2901f3f00fc709	xacquire lock xadd %ebx,(%ecx)|xacquire lock btsl $1,(%ecx)|xrelease lock cmpxchg8b (%ecx)
32	64f2f083000166f38903	xacquire lock addl $1,%fs:(%eax)|xrelease movw %ax,(%ebx)
64	67f2f0ff00	xacquire lock incl (%eax)
32 64	refused	xacquire addl $1,(%eax)
32 64	refused	xacquire lock addl $1,%eax
32 64	refused	xrelease movl (%ebx),%eax
32 64	refused	xacquire nop
32 64	0f01c10f01c20f01c30f01c40f01d4	vmcall|vmlaunch|vmresume|vmxoff|vmfunc
64	8e0425000000008c0425000000008e1c25000000008e208c6b088e042500100000	mov sym,%es|mov %es,sym|movw sym,%ds|mov (%rax),%fs|mov %gs,8(%rbx)|mov 0x1000,%es
64	8e05000000008c1d00000000	mov sym(%rip),%es|mov %ds,sym(%rip)
64	440f20c0440f22c0	mov %cr8,%rax|mov %rax,%cr8
32 64	f3c3f3c3	repz ret|rep ret
# A suffix the instruction does not take, or that its register denies.
32 64	refused	movntib %al,(%ebx)
32 64	refused	shldb $1,%al,%bl
64	refused	andnl %rax,%rbx,%rcx
64	refused	bzhiq %eax,%ebx,%ecx
EOF

[ "$fail" -eq 0 ] && echo "ok: suffix forms ($cases cases)"
exit "$fail"
