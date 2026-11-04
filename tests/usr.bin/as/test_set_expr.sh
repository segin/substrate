#!/bin/sh
# `.set` (and `.equ`, `=`) of any expression, as GNU as takes it.
#
# The assembler took only a number, a symbol and an addend, `. - sym` and
# `a - b + n` on the right of a `.set`, and called anything else a
# malformed directive.  GNU as takes any expression and has its value once
# its operands are known: two labels of one section are a number apart,
# and that number may be divided, shifted, compared.  Linux's
# arch/x86/boot/header.S counts its PE sections so,
# `.set section_count, (. - section_table) / 40`, and uses the count
# further up, in the header's NumberOfSections.
#
# And an instruction's operand of such an expression, written in it or put
# there by a `.set`, was given a relocation against its first label and
# lost its value: `movl $((x - st) << 2), %eax` loaded the address of x.
#
# GNU as is the judge: the sections' bytes, --32 and --64, the value of
# the symbol set, and its refusals.
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
# The value and section index of symbol $2.
sym_of() { readelf -sW "$1" | awk -v n="$2" '$8 == n { print $2, $7 }'; }

# same NAME SECTION MODES SYMBOLS...: t.s assembles to GNU as's bytes of
# SECTION in each mode, and each symbol named has GNU as's value.
same() {
    name=$1 sec=$2 modes=$3
    shift 3
    for m in $modes; do
        if ! "$GAS" "--$m" -o g.o t.s 2> err; then
            echo "FAIL $name --$m: GNU as refused it: $(head -1 err)"; fail=1; continue
        fi
        if ! "$AS" "--$m" -o o.o t.s 2> err; then
            echo "FAIL $name --$m: $(head -1 err | sed 's/.*: //')"; fail=1; continue
        fi
        if [ "$(bytes_of o.o "$sec")" != "$(bytes_of g.o "$sec")" ]; then
            echo "FAIL $name --$m: $(bytes_of o.o "$sec"), GNU as $(bytes_of g.o "$sec")"; fail=1
        fi
        for s in "$@"; do
            if [ "$(sym_of o.o "$s")" != "$(sym_of g.o "$s")" ]; then
                echo "FAIL $name --$m: $s is '$(sym_of o.o "$s")', GNU as '$(sym_of g.o "$s")'"; fail=1
            fi
        done
    done
}

# refused NAME: GNU as and this assembler both refuse t.s.
refused() {
    if "$GAS" --32 -o g.o t.s 2> /dev/null; then
        echo "FAIL $1: GNU as takes it, and this test says it does not"; fail=1
    fi
    if "$AS" --32 -o o.o t.s 2> err; then
        echo "FAIL $1: taken, and GNU as refuses it"; fail=1
    elif ! grep -q "$2" err; then
        echo "FAIL $1: refused, but not as '$2': $(head -1 err)"; fail=1
    fi
}

# header.S's shape: the count used before it is set, `.` in it.
cat > t.s <<'EOF'
	.code16
	.section ".header", "a"
	.short sc
st:
	.long 1
x:
	.long 2
	.set sc, (x - st) / 40
	.long sc
	.set sc2, (. - st) / 4 + 1
	.byte sc2
section_table:
	.fill 5 * 40, 1, 0
	.set section_count, (. - section_table) / 40
	.short section_count
EOF
same header .header "32 64" sc sc2 section_count

# Every rank of operator, over distances; .equ and =; a name set twice;
# a name set to an expression of another; forward and backward.
cat > t.s <<'EOF'
	.data
	.long fwd, fwd2
st:
	.long 1
x:
	.long 2
	.set a, ((x - st) * 3 + 1) % 5 | (x - st) << 4 & 0xff ^ 1
	.set b, (x - st) > 3
	.set c, (x - st) == 4 && 1
	.long a, b, c
	.set d, ~(x - st)
	.set e, -(x - st) / 2
	.long d, e
	.equ f, (x - st) / 2
	g = (x - st) * 5
	.long f, g
	.set h, 5
	.long h
	.set h, (x - st) * 3
	.long h
	.set h, 7
	.long h
	.set i, (. - st) * (. - st)
	.long i
	.set fwd, (y - st) >> 1
	.set fwd2, fwd * 2
	.long 9
y:
EOF
same data .data "32 64" a b c d e f g i fwd fwd2

# In an instruction: an immediate and a displacement, before the labels
# and after (where the short form is GNU as's), across a jump, and with
# `.` in what was set.
cat > t.s <<'EOF'
	movl $((x - st) << 2), %eax
	addl $((x - st) * 4), %eax
	movl ((x - st) * 4)(%ebx), %eax
	.set u, (x - st) << 2
	addl $u, %eax
st:
	.long 1
x:
	.long 2
	movl $((x - st) << 2), %eax
	addl $((x - st) * 4), %eax
	addw $((x - st) * 4), %ax
	push $((x - st) * 4)
	movl ((x - st) * 4)(%ebx), %eax
	.set v, (x - st) << 2
	addl $v, %eax
	movl $v, %ecx
	.set w, (. - st) * 4
	addl $w, %eax
	jmp y
z:
	.long 3
	.set k, (z - st) * 4
	addl $k, %eax
	.long k
	.set n, 4
	addl $((x - st) * n), %eax
	.set n, 8
	addl $((x - st) * n), %eax
	.set m, (x - st) * n
	addl $m, %eax
y:
EOF
same insn .text "32 64" u v w k

cat > t.s <<'EOF'
st:
	.long 1
x:
	.long 2
	.set q, (x - st) * 0x100000000
	movabsq $q, %rax
	movq $((x - st) / 2), %rax
	.quad q
EOF
same insn64 .text "64" q

# What GNU as refuses: a difference of two sections, a place multiplied,
# another file's symbol divided, and a .if of what is not yet known.
printf '\t.text\nst:\t.long 1\n\t.data\nx:\t.long 2\n\t.text\n\t.set a, (x - st) / 2\n' > t.s
refused sections "invalid operands for \`-'"
printf 'st:\t.long 1\n\t.set a, st * 2\n\t.long a\n' > t.s
refused place-times "invalid operands for \`\*'"
printf '\t.set a, ext / 2\n\t.long a\n' > t.s
refused extern "is no label"
printf '\t.set a, (x - st) / 2\n\t.if a == 2\n\t.endif\nst:\t.long 1\nx:\t.long 2\n' > t.s
refused if-forward "\.if"

if [ $fail = 0 ]; then
    echo "test_set_expr: ok"
fi
exit $fail
