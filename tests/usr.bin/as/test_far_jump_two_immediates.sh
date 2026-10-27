#!/bin/sh
# `jmp $seg, $label` and `call $seg, $label`: the far jump and call,
# written with two numbers as GNU as takes them, the same instructions
# as ljmp and lcall.
#
# By their names every pass after the parser took them for a jump and a
# call to a place, and the offset went out with no relocation: the
# Substrate kernel's `jmp $0x08, $.flush`, which reloads %cs, jumped to
# 0.  Linked, each must go where GNU as's object goes.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
GNU_AS=${GNU_AS:-as}
GNU_LD=${GNU_LD:-ld}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if ! "$GNU_AS" --version 2>/dev/null | grep -q 'GNU assembler' ||
    ! "$GNU_LD" --version 2>/dev/null | grep -q 'GNU ld'; then
    echo "skip: GNU as and ld are the reference"
    exit 0
fi

cat > far.s <<'EOF'
	.text
	nop
	jmp $0x08, $.flush
	call $0x10, $there
	jmpl $0x08, $there + 4
	ljmp $0x08, $there
.flush:
	nop
there:
	nop
EOF

fail=0
"$AS" --32 -o ours.o far.s > out 2>&1 || { echo "FAIL: $(tail -1 out)"; exit 1; }
"$GNU_AS" --32 -o gnu.o far.s || exit 1
"$GNU_LD" -m elf_i386 -Ttext=0x1000 -e 0x1000 -o ours ours.o || exit 1
"$GNU_LD" -m elf_i386 -Ttext=0x1000 -e 0x1000 -o gnu gnu.o || exit 1
objcopy -O binary -j .text ours ours.bin
objcopy -O binary -j .text gnu gnu.bin
if ! cmp -s ours.bin gnu.bin; then
    echo "FAIL: linked, the far jumps are not GNU as's:"
    echo "  ours $(od -An -v -tx1 ours.bin | tr -d '\n')"
    echo "  gnu  $(od -An -v -tx1 gnu.bin | tr -d '\n')"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: far jumps and calls with two immediates"
exit "$fail"
