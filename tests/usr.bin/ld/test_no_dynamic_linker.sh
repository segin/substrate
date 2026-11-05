#!/bin/sh
# --no-dynamic-linker asks for no interpreter, as in GNU ld: the last of it
# and -dynamic-linker says.
#
# The option was taken as an unknown one and ignored ("unsupported option
# ignored"); Linux's x86-64 decompressor is linked -pie with it.
#
# GNU ld is the judge: with -dynamic-linker before it, after it, and
# without one, the program has a PT_INTERP and an .interp, of the path,
# when GNU ld's does; and nothing is said of the option.
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

# the interpreter of $1, or "none"
interp() {
    readelf -lW "$1" | sed -n 's/.*Requesting program interpreter: \(.*\)\]/\1/p' | grep . || echo none
}

while IFS='|' read -r flags; do
    for kind in -pie ""; do
        ${GNU_LD:-ld} $kind $flags -o gnu a.o libb.so 2>gnu.err || { echo "FAIL: GNU ld: $flags"; cat gnu.err; exit 1; }
        if ! ./ld $kind $flags -o our a.o libb.so 2>our.err; then
            echo "FAIL $kind $flags: not linked:"; cat our.err; fail=1; continue
        fi
        if grep -q 'unsupported option' our.err; then
            echo "FAIL $kind $flags: the option is not taken:"; cat our.err; fail=1
        fi
        if [ "$(interp our)" != "$(interp gnu)" ]; then
            echo "FAIL $kind $flags: interpreter $(interp our), GNU ld's $(interp gnu)"; fail=1
        fi
        if [ "$(readelf -SW our | grep -c ' \.interp ')" != "$(readelf -SW gnu | grep -c ' \.interp ')" ]; then
            echo "FAIL $kind $flags: .interp is not as GNU ld has it"; fail=1
        fi
    done
done <<'EOF'
-dynamic-linker /lib/ld-x.so --no-dynamic-linker
--no-dynamic-linker -dynamic-linker /lib/ld-x.so
--no-dynamic-linker
-dynamic-linker /lib/ld-x.so -no-dynamic-linker
EOF

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
