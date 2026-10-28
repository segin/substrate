#!/bin/sh
# The instructions of ARM, Thumb and AArch64 (AS-T-372, 373, 375, 378 to
# 382).
#
# There were seventeen files of them that were fixtures and no encoders:
# a table of one fixed word for a mnemonic, whatever its operands.  They
# are five that read the operands (as_arm.c, as_arm_simd.c,
# as_arm_thumb.c, as_a64.c, as_a64_simd.c).
#
# arm_forms.txt holds, for each statement of a corpus of every class of
# instruction -- data processing, multiply, load and store, load and
# store of several registers, branch, system, VFP and NEON; Thumb's data
# processing in both its lengths; AArch64's data processing with a
# number and with a register, load and store, branch, system, floats
# and vectors -- the bytes llvm-mc 20 makes of it, or ERR where it
# refuses and so does this.  arm_own.txt holds the few where the two
# part, each with its reason.
#
# A statement is assembled at 0x100 of its section, with a label `back`
# at 0x10 before it and `fwd2` at 0x140 and `fwd` at 0x200 after it, and
# an instruction that does nothing behind it: the bytes are to be the
# ones recorded and then that instruction's, so that one too long is
# seen as well as one that is wrong.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if "$AS" --version 2> /dev/null | grep '^GNU assembler' | grep -qv '(Substrate)'; then
    echo "ok: ARM forms (not tried: GNU as for x86 has no ARM)"
    exit 0
fi
ln -s "$AS" arm-as
ln -s "$AS" aarch64-as

fail=0
cases=0
off_arm=
off_a64=

# Where .text is in an object of this shape.
text_offset() {
    readelf -S -W "$1" | awk '$2 == ".text" || $3 == ".text" { for (i = 1; i <= NF; i++) if ($i == "PROGBITS") { print strtonum("0x" $(i + 2)); exit } }'
}

grep -hv '^#' "$here/arm_forms.txt" "$here/arm_own.txt" > forms.txt
while IFS='	' read -r mode want stmt; do
    [ -n "$mode" ] || continue
    cases=$((cases + 1))
    case $mode in
    arm) as=./arm-as; head='	.syntax unified'; nop=00f020e3 ;;
    thumb) as=./arm-as; head='	.syntax unified
	.thumb'; nop=00bf ;;
    a64) as=./aarch64-as; head='	.text'; nop=1f2003d5 ;;
    esac
    printf '%s\n\tnop\n\t.org 0x10\nback:\tnop\n\t.org 0x100\nhere:\t%s\n\tnop\n\t.org 0x140\nfwd2:\tnop\n\t.org 0x200\nfwd:\tnop\n' \
        "$head" "$stmt" > l.s
    rm -f l.o
    if ! $as -o l.o l.s 2> err; then
        [ "$want" = ERR ] || { echo "FAIL $mode [$stmt]: refused: $(head -1 err)"; fail=1; }
        continue
    fi
    if [ "$want" = ERR ]; then
        echo "FAIL $mode [$stmt]: assembled, and it is not an instruction"
        fail=1
        continue
    fi
    if [ "$mode" = a64 ]; then
        [ -n "$off_a64" ] || off_a64=$(text_offset l.o)
        off=$off_a64
    else
        [ -n "$off_arm" ] || off_arm=$(text_offset l.o)
        off=$off_arm
    fi
    n=$(((${#want} + ${#nop}) / 2))
    got=$(od -An -v -tx1 -j $((off + 256)) -N "$n" l.o | tr -d ' \n')
    [ "$got" = "$want$nop" ] || { echo "FAIL $mode [$stmt]: $got, not $want and then $nop"; fail=1; }
done < forms.txt

if [ "$fail" = 0 ]; then
    echo "ok: ARM forms ($cases statements)"
    exit 0
fi
exit 1
