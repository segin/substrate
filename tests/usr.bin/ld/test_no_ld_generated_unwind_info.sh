#!/bin/sh
# --no-ld-generated-unwind-info is taken: the linker makes no unwind
# information of its own, which is what it asks for.
#
# The option was taken as an unknown one and ignored ("unsupported option
# ignored"); Linux's x86-64 decompressor is linked with it.  GNU ld's
# default is an FDE for the PLT; this linker never makes one.
#
# GNU ld is the judge: a program with a PLT and no unwind information of
# its inputs' has, with the option, no .eh_frame and no .eh_frame_hdr, as
# GNU ld's has none; and nothing is said of the option.
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
printf '\t.globl f\n\t.text\nf:\tret\n' > b.s
printf '\t.text\n\t.globl _start\n_start:\tcall f@PLT\n' > a.s
${GNU_AS:-as} --64 -o a.o a.s && ${GNU_AS:-as} --64 -o b.o b.s || { echo "FAIL: as"; exit 1; }
${GNU_LD:-ld} -shared -o libb.so b.o || { echo "FAIL: GNU ld -shared"; exit 1; }

frames() { readelf -SW "$1" | grep -oE ' \.eh_frame(_hdr)? ' | tr -d ' ' | sort | tr '\n' ' '; }

for flags in "-pie --eh-frame-hdr" "--eh-frame-hdr" "-pie"; do
    ${GNU_LD:-ld} $flags --no-ld-generated-unwind-info -dynamic-linker /lib/ld-x.so -o gnu a.o libb.so 2>gnu.err ||
        { echo "FAIL: GNU ld: $flags"; cat gnu.err; exit 1; }
    if ! ./ld $flags --no-ld-generated-unwind-info -dynamic-linker /lib/ld-x.so -o our a.o libb.so 2>our.err; then
        echo "FAIL $flags: not linked:"; cat our.err; fail=1; continue
    fi
    if grep -q 'unsupported option' our.err; then
        echo "FAIL $flags: the option is not taken:"; cat our.err; fail=1
    fi
    if [ "$(frames our)" != "$(frames gnu)" ]; then
        echo "FAIL $flags: unwind sections '$(frames our)', GNU ld's '$(frames gnu)'"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
