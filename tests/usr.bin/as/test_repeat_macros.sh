#!/bin/sh
# Repeats and macros, one inside the other.
#
# A .rept at the top of a source had its lines given to the macros, and
# nothing else did: a macro called in the body of an .irp or an .irpc,
# or in a repeat inside a repeat, or in a repeat in a macro's body, was
# written out as it stood for a pass that knows no macros, and was
# "unsupported mnemonic".  And an .irp ended at the first .endr, so a
# repeat in its body had none; a .rept took the .endr of an .irp in its
# body for its own.
#
# What each of these assembles to is what GNU as 2.47 makes of it, and
# GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is NAME BYTES: NAME.s assembles, and its .data is BYTES.
is() {
    cases=$((cases + 1))
    rm -f t.o t.bin
    if ! "$AS" --64 -o t.o "$1.s" > out 2>&1; then
        echo "FAIL $1: $(tail -1 out)"; fail=1; return
    fi
    objcopy -O binary -j .data t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL $1: [$got], not [$2]"; fail=1; }
}

# A macro called in an .irp and an .irpc, each in a .rept; and a repeat
# in an .irp in a repeat in a macro's body.
cat > mixed.s <<'EOF'
	.data
	.macro fn n
	.byte \n
	.endm
	.rept 2
	.irp x, 1, 2
	.byte \x
	fn 4\x
	.endr
	.byte 9
	.irpc y, ab
	.byte 0x\y
	.endr
	.endr
	.macro mm
	.rept 2
	.irp z, 5
	fn \z
	.endr
	.endr
	.endm
	mm
EOF
is mixed 0129022a090a0b0129022a090a0b0505

# Repeats three deep in a macro's body, with the macro's argument and
# each repeat's value in what they call; a macro called in a repeat in
# a repeat; an .exitm in a repeat, which ends the repeat and no more;
# and a repeat whose count is the macro's argument.
cat > deep.s <<'EOF'
	.data
	.macro fn n
	.byte \n
	.endm
	.macro deep a
	.rept 2
	fn \a
	.irp z, 5, 6
	fn \z
	.rept 2
	fn \a\z
	.endr
	.endr
	.irpc w, 12
	fn 3\w
	.endr
	.endr
	.endm
	deep 1
	.rept 2
	.rept 2
	fn 8
	.endr
	deep 2
	.endr
	.macro ex n
	.rept 3
	fn \n
	.if \n
	.exitm
	.endif
	.endr
	fn 99
	.endm
	ex 0
	ex 1
	.macro cnt n
	.rept \n
	fn \n
	.endr
	.endm
	cnt 3
	cnt 0
EOF
is deep 01050f0f0610101f2001050f0f0610101f20080802051919061a1a1f2002051919061a1a1f20080802051919061a1a1f2002051919061a1a1f20000000630163030303

# A macro, and a macro that calls it, in an .irp; a repeat in an
# .irpc; a macro defined in an .irp and called after it; an .irp in an
# .irp.
cat > irp.s <<'EOF'
	.data
	.macro fn n
	.byte \n, \n + 1
	.endm
	.macro two a, b
	fn \a
	fn \b
	.endm
	.irp r, 5, 6
	fn \r
	two \r, 9
	.endr
	.irpc c, 12
	fn \c
	.rept 2
	fn 7\c
	.endr
	.endr
	.irp r, 1
	.macro made\r
	.byte 0x\r\r
	.endm
	.endr
	made1
	.irp q, 3, 4
	.irp p, a, b
	.byte 0x\p, \q
	fn 0x\p
	.endr
	.endr
EOF
is irp 05060506090a06070607090a010247484748020348494849110a030a0b0b030b0c0a040a0b0b040b0c

# An .exitm in an .irp in a macro ends the .irp, and the macro goes on.
cat > exit.s <<'EOF'
	.data
	.macro fn n
	.byte \n
	.endm
	.macro e2 n
	.irp q, 1, 2, 3
	fn \q
	.ifc \q, 2
	.exitm
	.endif
	.endr
	fn 77
	.if \n
	.exitm
	.endif
	fn 88
	.endm
	e2 0
	e2 1
	.rept 2
	fn 5
	.endr
EOF
is exit 01024d5801024d0505

# What is wrong in a macro called in an .irp is at the line it was
# called on.
printf '\t.text\n\t.macro fn a\n\tmovl %%eax, \\a\n\t.endm\n\tnop\n\t.irp r, %%ebx, %%bx\n\tnop\n\tfn \\r\n\t.endr\n' > where.s
cases=$((cases + 1))
if "$AS" --64 -o t.o where.s > out 2>&1; then
    echo "FAIL where: assembled"; fail=1
elif "$AS" --version 2>/dev/null | grep -qi 'GNU assembler'; then
    :
else
    # (GNU as names the line of the macro's body, and the call after it.)
    grep -q 'where\.s:[89]:' out || { echo "FAIL where: $(head -1 out)"; fail=1; }
fi

[ "$fail" -eq 0 ] && echo "ok: repeats and macros ($cases cases)"
exit "$fail"
