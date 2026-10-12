#!/bin/sh
# Relocation operators, and instructions with two symbols (AS-T-197,
# 201 to 205, 389).
#
# - An instruction with a symbol for its address and another for its
#   number, `movl $a, b`, was refused: "multiple symbolic relocations in
#   one x86 instruction are not yet supported".  It has a relocation for
#   each.
# - What follows an @ and is no operator, or none the target has, was
#   left on the symbol's name: `movl foo@BOGUS, %eax` was assembled with
#   a relocation against a symbol called `foo@BOGUS`.  It is refused, and
#   so is an operator on what a branch goes to that is not @PLT.
# - @TLSDESC, @TLSCALL and @PLTOFF were not operators.
# - In a field of 64 bits an operator names the 64-bit relocation:
#   `.quad foo@GOT` was R_X86_64_GOT32.
# - A load through the global offset table that a linker may rewrite
#   says so: R_X86_64_GOTPCRELX, R_X86_64_REX_GOTPCRELX, R_386_GOT32X.
# - Square brackets group, as in `$_GLOBAL_OFFSET_TABLE_+[.-.L1]`.
#
# Each case is the bytes of its section and its relocations, which are
# GNU as's; GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE SOURCE BYTES RELOCATIONS [SECTION]: `|` in SOURCE is a newline;
# the relocations are type@offset:symbol+addend, sorted.
is() {
    cases=$((cases + 1))
    sec=${5:-.text}
    { echo "$sec"; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 [$2]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j "$sec" t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
             sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$3" ] || { echo "FAIL --$1 [$2]: bytes $bytes, not $3"; fail=1; }
    [ "$relocs" = "$4" ] || { echo "FAIL --$1 [$2]: relocations [$relocs], not [$4]"; fail=1; }
}

# refused MODE SOURCE
refused() {
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
    if "$AS" "--$1" -o t.o t.s > /dev/null 2> err; then
        echo "FAIL --$1 [$2]: assembled"; fail=1
    fi
}

# Two symbols in one instruction.
is 32 'movl $a,b|cmpl $a,b(%ebx)' c705000000000000000081bb0000000000000000 \
    'R_386_32@10:a R_386_32@2:b R_386_32@6:a R_386_32@c:b'
is 32 'movw $a,b|movb $a,b(%eax)|addl $a+4,b-8(%ebx,%ecx,2)' 66c705000000000000c680000000000081844bf8ffffff04000000 \
    'R_386_16@7:a R_386_32@13:b R_386_32@17:a R_386_32@3:b R_386_32@b:b R_386_8@f:a'
is 64 'movl $sym2, sym(%rip)|movl $a,b' c7050000000000000000c704250000000000000000 \
    'R_X86_64_32@11:a+0 R_X86_64_32@6:sym2+0 R_X86_64_32S@d:b+0 R_X86_64_PC32@2:sym-8'
is 64 'movq $a,b(%rip)|cmpl $a,b(%rax)|addw $a,b(%rip)|imull $a,b(%rip),%eax' 48c705000000000000000081b8000000000000000066810500000000000069050000000000000000 \
    'R_X86_64_16@1c:a+0 R_X86_64_32@11:a+0 R_X86_64_32@24:a+0 R_X86_64_32S@7:a+0 R_X86_64_32S@d:b+0 R_X86_64_PC32@18:b-6 R_X86_64_PC32@20:b-8 R_X86_64_PC32@3:b-8'

# i386: each operator, in an instruction and in data.
is 32 'movl foo@GOT(%ebx),%eax|leal foo@GOTOFF(%ebx),%eax|call foo@PLT|leal foo@TLSGD(,%ebx,1),%eax|leal foo@TLSLDM(%ebx),%eax|leal foo@DTPOFF(%eax),%edx' 8b83000000008d8300000000e8fcffffff8d041d000000008d83000000008d9000000000 \
    'R_386_GOT32X@2:foo R_386_GOTOFF@8:foo R_386_PLT32@d:foo R_386_TLS_GD@14:foo R_386_TLS_LDM@1a:foo R_386_TLS_LDO_32@20:foo'
is 32 'movl foo@GOTTPOFF(%ebx),%eax|movl foo@GOTNTPOFF(%ebx),%eax|movl $foo@TPOFF,%eax|leal foo@TLSDESC(%ebx),%eax|call *foo@TLSCALL(%eax)' 8b83000000008b8300000000b8000000008d8300000000ff10 \
    'R_386_TLS_DESC_CALL@17:foo R_386_TLS_GOTDESC@13:foo R_386_TLS_GOTIE@8:foo R_386_TLS_IE_32@2:foo R_386_TLS_LE_32@d:foo'
is 32 '.long foo@GOT|.long foo@GOTOFF|.long foo@PLT|.long foo@NTPOFF|.long foo@TPOFF|.long foo@DTPOFF' 000000000000000000000000000000000000000000000000 \
    'R_386_GOT32@0:foo R_386_GOTOFF@4:foo R_386_PLT32@8:foo R_386_TLS_LDO_32@14:foo R_386_TLS_LE@c:foo R_386_TLS_LE_32@10:foo' .data
# The table by name, plain and with the distance from a label.
is 32 'addl $_GLOBAL_OFFSET_TABLE_,%ebx' 81c302000000 'R_386_GOTPC@2:_GLOBAL_OFFSET_TABLE_'
is 32 '.L1: popl %ebx|addl $_GLOBAL_OFFSET_TABLE_+[.-.L1],%ebx' 5b81c303000000 'R_386_GOTPC@3:_GLOBAL_OFFSET_TABLE_'
# A load the linker may rewrite, and what is not one.
is 32 'movw foo@GOT(%ebx),%ax|addl foo@GOT(%ebx),%eax|leal foo@GOT(%ebx),%eax|call *foo@GOT(%ebx)|testl %eax,foo@GOT(%ebx)' 668b83000000000383000000008d8300000000ff9300000000858300000000 \
    'R_386_GOT32@3:foo R_386_GOT32@f:foo R_386_GOT32X@15:foo R_386_GOT32X@1b:foo R_386_GOT32X@9:foo'

# x86-64: each operator.
is 64 'movq foo@GOTPCREL(%rip),%rax|leaq foo@TLSGD(%rip),%rdi|leaq foo@TLSLD(%rip),%rdi|leaq foo@DTPOFF(%rax),%rdx|movq foo@GOTTPOFF(%rip),%rax|movq %fs:foo@TPOFF,%rax' 488b0500000000488d3d00000000488d3d00000000488d9000000000488b050000000064488b042500000000 \
    'R_X86_64_DTPOFF32@18:foo+0 R_X86_64_GOTTPOFF@1f:foo-4 R_X86_64_REX_GOTPCRELX@3:foo-4 R_X86_64_TLSGD@a:foo-4 R_X86_64_TLSLD@11:foo-4 R_X86_64_TPOFF32@28:foo+0'
is 64 'leaq foo@TLSDESC(%rip),%rax|call *foo@TLSCALL(%rax)|movabs $foo@GOT,%rax|movabs $foo@GOTOFF,%rax|movabs $foo@PLTOFF,%rax|movl $foo@SIZE,%eax' 488d0500000000ff1048b8000000000000000048b8000000000000000048b80000000000000000b800000000 \
    'R_X86_64_GOT64@b:foo+0 R_X86_64_GOTOFF64@15:foo+0 R_X86_64_GOTPC32_TLSDESC@3:foo-4 R_X86_64_PLTOFF64@1f:foo+0 R_X86_64_SIZE32@28:foo+0 R_X86_64_TLSDESC_CALL@7:foo+0'
is 64 '.quad foo@GOT|.quad foo@GOTOFF|.long foo@GOTPCREL|.long foo@PLT|.quad foo@DTPOFF|.quad foo@TPOFF|.quad foo@SIZE|.quad foo@PLTOFF' 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 \
    'R_X86_64_DTPOFF64@18:foo+0 R_X86_64_GOT64@0:foo+0 R_X86_64_GOTOFF64@8:foo+0 R_X86_64_GOTPCREL@10:foo+0 R_X86_64_PLT32@14:foo+0 R_X86_64_PLTOFF64@30:foo+0 R_X86_64_SIZE64@28:foo+0 R_X86_64_TPOFF64@20:foo+0' .data
is 64 'movl foo@GOTPCREL(%rip),%eax|movq foo@GOTPCREL(%rip),%r8|testq %rax,foo@GOTPCREL(%rip)|addq foo@GOTPCREL(%rip),%rax|call *foo@GOTPCREL(%rip)|jmp *foo@GOTPCREL(%rip)|cmpl foo@GOTPCREL(%rip),%eax' 8b05000000004c8b05000000004885050000000048030500000000ff1500000000ff25000000003b0500000000 \
    'R_X86_64_GOTPCRELX@1d:foo-4 R_X86_64_GOTPCRELX@23:foo-4 R_X86_64_GOTPCRELX@29:foo-4 R_X86_64_GOTPCRELX@2:foo-4 R_X86_64_REX_GOTPCRELX@10:foo-4 R_X86_64_REX_GOTPCRELX@17:foo-4 R_X86_64_REX_GOTPCRELX@9:foo-4'
is 64 'leaq foo@GOTPCREL(%rip),%rax|movw foo@GOTPCREL(%rip),%ax|pushq foo@GOTPCREL(%rip)|movq %rax,foo@GOTPCREL(%rip)' 488d0500000000668b0500000000ff350000000048890500000000 \
    'R_X86_64_GOTPCREL@17:foo-4 R_X86_64_GOTPCREL@3:foo-4 R_X86_64_GOTPCREL@a:foo-4 R_X86_64_GOTPCRELX@10:foo-4'

# What is no operator, or none this target has, or not for a branch.
refused 32 'movl foo@BOGUS,%eax'
refused 64 'movl foo@BOGUS,%eax'
refused 32 'movl foo@GOTPCREL,%eax'
refused 32 'jmp foo@GOTOFF'
refused 64 'jmp foo@GOTOFF'
# An operator of thread-local storage, in an instruction with another
# symbol: none of its forms has one.
refused 64 'movl $sym2,sym@TLSLD(%rip)'
refused 32 'movl $sym2,sym@GOTTPOFF(%ebx)'

# The operator is the expression's and not the symbol's name (AS-T-200):
# the parser takes it off where it reads the symbol.  It was left in the
# name, and whatever the name was handed to looked for an `@` in it.
#
# rel MODE DIRECTIVE WANT: the one relocation of a data directive, as
# type symbol addend, or `refused`.
rel() {
    cases=$((cases + 1))
    printf '\t.data\n\t%s\n' "$2" > d.s
    rm -f d.o
    if "$AS" "--$1" -o d.o d.s 2> err; then
        got=$(readelf -rW d.o | awk '$3 ~ /^R_/ { print $3, $5, $6, $7 }' | sed 's/ *$//')
    else
        got=refused
    fi
    [ "$got" = "$3" ] || { echo "FAIL --$1 [$2]: $got, not $3"; fail=1; }
}
# In a field of four bytes the operator names its 32-bit relocation and
# in one of eight its 64-bit one.  In 64-bit code it was the 64-bit one
# whatever the field -- `.long ext@GOT` had R_X86_64_GOT64 on four bytes
# -- and `.long tv@DTPOFF`, which a compiler writes in debugging
# information, was refused.
rel 64 '.long tv@DTPOFF'       'R_X86_64_DTPOFF32 tv + 0'
rel 64 '.long tv@DTPOFF + 4'   'R_X86_64_DTPOFF32 tv + 4'
rel 64 '.long 4 + tv@DTPOFF'   'R_X86_64_DTPOFF32 tv + 4'
rel 64 '.long tv@TPOFF+4'      'R_X86_64_TPOFF32 tv + 4'
rel 64 '.quad tv@TPOFF + 8'    'R_X86_64_TPOFF64 tv + 8'
rel 64 '.quad tv@DTPOFF'       'R_X86_64_DTPOFF64 tv + 0'
rel 64 '.long ext@GOT'         'R_X86_64_GOT32 ext + 0'
rel 64 '.long ext@GOT - 4'     'R_X86_64_GOT32 ext - 4'
rel 64 '.quad ext@GOT + 8'     'R_X86_64_GOT64 ext + 8'
rel 64 '.long ext@GOTPCREL + 4' 'R_X86_64_GOTPCREL ext + 4'
rel 64 '.quad ext@GOTPCREL'    'R_X86_64_GOTPCREL64 ext + 0'
rel 64 '.long ext@PLT + 4'     'R_X86_64_PLT32 ext + 4'
rel 64 '.quad ext@GOTOFF'      'R_X86_64_GOTOFF64 ext + 0'
rel 64 '.quad ext@PLTOFF'      'R_X86_64_PLTOFF64 ext + 0'
rel 64 '.long ext@SIZE'        'R_X86_64_SIZE32 ext + 0'
rel 64 '.quad ext@SIZE'        'R_X86_64_SIZE64 ext + 0'
rel 32 '.long ext@GOT'         'R_386_GOT32 ext'
rel 32 '.long ext@GOTOFF'      'R_386_GOTOFF ext'
rel 32 '.long tv@NTPOFF'       'R_386_TLS_LE tv'
rel 32 '.long tv@DTPOFF'       'R_386_TLS_LDO_32 tv'
# And where the operator's relocation is not of the field's width there
# is none to make: these had a relocation of four bytes on eight, or of
# eight on four.
rel 64 '.quad ext@PLT'         refused
rel 64 '.long ext@GOTOFF'      refused
rel 64 '.long ext@PLTOFF'      refused
rel 32 '.quad ext@GOT'         refused
rel 32 '.quad ext@GOTOFF'      refused
rel 32 '.quad tv@DTPOFF'       refused
rel 64 '.short ext@GOT'        refused

# No symbol's name has an operator in it, in the object: each operator
# there is, on a symbol of another file, on a label of this one and on a
# compiler's label, in an instruction and in data, and in small letters.
cases=$((cases + 1))
cat > all32.s <<'EOF'
	.text
foo:	nop
.Lloc:	nop
	call foo@PLT
	call .Lloc@PLT
	call ext@plt
	movl foo@GOTOFF(%ebx), %eax
	movl .LC0@GOTOFF(%ebx), %eax
	leal ext@GOTOFF+4(%ebx), %ecx
	movl ext@GOT(%ebx), %eax
	movl $foo@GOTOFF, %eax
	leal tv@TLSGD(,%ebx,1), %eax
	movl tv@GOTNTPOFF(%ebx), %eax
	movl %gs:tv@NTPOFF, %eax
	movl tv@INDNTPOFF, %eax
	leal tv@TLSLDM(%ebx), %eax
	leal tv@DTPOFF(%eax), %edx
	.section .rodata
.LC0:	.string "x"
	.data
	.long foo@GOTOFF, ext@GOT, .LC0@GOTOFF, ext@PLT, foo@GOTOFF - 4
EOF
cat > all64.s <<'EOF'
	.text
foo:	nop
	call foo@PLT
	jmp ext@plt
	movq ext@GOTPCREL(%rip), %rax
	movq foo@GOTPCREL(%rip), %rax
	leaq tv@TLSGD(%rip), %rdi
	leaq tv@TLSLD(%rip), %rdi
	movq tv@GOTTPOFF(%rip), %rax
	movq %fs:tv@TPOFF, %rax
	leaq tv@DTPOFF(%rax), %rdx
	movabsq $ext@GOTOFF, %rax
	movl $ext@SIZE, %eax
	.data
	.quad ext@GOT, foo@GOTOFF, tv@TPOFF
	.long ext@GOTPCREL, ext@PLT, tv@DTPOFF + 4
EOF
for m in 32 64; do
    if ! "$AS" "--$m" -o "all$m.o" "all$m.s" 2> err; then
        echo "FAIL --$m, every operator: $(head -1 err)"; fail=1
    elif readelf -sW "all$m.o" | awk '$1 ~ /^[0-9]+:$/ { print $NF }' | grep -q '@'; then
        echo "FAIL --$m: a symbol with an operator in its name: $(readelf -sW "all$m.o" | grep '@' | head -1)"
        fail=1
    fi
done
# A version is another matter, and is in the name: it is what .symver
# is for.
cases=$((cases + 1))
printf '\t.text\nf1:\tret\n\t.globl f1\n\t.symver f1, f@@V1\n' > ver.s
"$AS" --64 -o ver.o ver.s 2> err && readelf -sW ver.o | grep -q 'f@@V1' ||
    { echo "FAIL .symver: the versioned name is not in the object"; fail=1; }
# And a symbol with an operator on it is not the place a label of that
# name is, though the label is in this section: it is the linker's.
is 32 'foo: movl foo@GOT(%ebx),%eax' 8b8300000000 'R_386_GOT32X@2:foo'

[ "$fail" -eq 0 ] && echo "ok: relocation operators ($cases cases)"
exit "$fail"
