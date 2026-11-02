#!/bin/sh
# An ASSERT about addresses that the link had to be done over to settle.
#
# Linux's vmlinux.lds moves the location counter in .text to
# `srso_alias_untrain_ret | (1 << 2) | ...` and then asserts, at its end,
# that srso_alias_safe_ret is where that put it ("SRSO function pair won't
# alias").  The first pass of the link puts the input sections together
# before anything has an address, so its layout is wrong there and the
# link is done over with the addresses it came to; but the assertion was
# checked, and failed, in the first pass, and the link stopped before it
# could be done over.  GNU ld checks it once the layout is done.
#
# GNU ld is the judge: the link succeeds, and the symbols are GNU ld's,
# x86-64 and i386; and an assertion that fails in the settled layout still
# fails the link, with its message.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GNU_LD:-ld} --version 2>/dev/null | grep -q 'GNU ld'; then
    echo "skip: GNU ld is the judge"
    exit 0
fi
if ! ${GNU_AS:-as} --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as makes the objects"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > a.s <<'EOF'
.section .text.untrain,"ax"
.globl srso
srso: .byte 0xab, 0xac
.section .text.entry,"ax"
.byte 0xe1, 0xe2, 0xe3
.section .text.safe,"ax"
.globl safe
safe: .byte 0x5a
.text
.globl _start
_start: ret
EOF
body='. = 0x400000; .text : { *(.text.untrain) *(.text.entry) . = srso | (1 << 2) | (1 << 8); *(.text.safe) *(.text) }'
printf 'SECTIONS { %s }\nASSERT(((ABSOLUTE(srso) | safe) - (ABSOLUTE(srso) & safe)) == ((1 << 2) | (1 << 8)), "pair will not alias");\n' \
    "$body" > ok.lds
printf 'SECTIONS { %s }\nASSERT(safe == srso, "never so");\n' "$body" > bad.lds

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    ${GNU_AS:-as} --$m -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }
    rm -f gnu our
    ${GNU_LD:-ld} -m $emu -T ok.lds -o gnu a.o || { echo "FAIL: GNU ld did not link"; exit 1; }
    if ! ./ld -m $emu -T ok.lds -o our a.o; then
        echo "FAIL: $emu: an assertion that holds once the layout is done failed the link"; fail=1
    else
        nm gnu | sort > gnu.sym
        nm our | sort > our.sym
        if ! cmp -s gnu.sym our.sym; then
            echo "FAIL: $emu: the symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
        fi
    fi
    if ${GNU_LD:-ld} -m $emu -T bad.lds -o gnu a.o 2>/dev/null; then
        echo "FAIL: GNU ld linked with an assertion that fails"; exit 1
    fi
    if ./ld -m $emu -T bad.lds -o our a.o 2>err; then
        echo "FAIL: $emu: an assertion that fails did not fail the link"; fail=1
    elif ! grep -q 'never so' err || [ "$(grep -c 'never so' err)" != 1 ]; then
        echo "FAIL: $emu: the failed assertion was not said once:"; cat err; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_script_assert_relayout: ok"
fi
exit $fail
