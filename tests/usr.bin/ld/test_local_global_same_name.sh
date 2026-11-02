#!/bin/sh
# One input's static f and another's global f are two symbols.
#
# A link's output keeps each input's locals and the globals beside them,
# and a static f in one object is not the global f another defines.  The
# linker looked a global up by name among all the output's symbols, so a
# global f met an earlier input's static f, was taken to be it, and its
# relocations went to the static one: `int (*pb)(void) = f` in b.o
# pointed at a.o's static f after `ld -r`, and the global f was gone.
# Linux's industrialio.ko, a static iio_read_channel_label in
# industrialio-core.c and the exported one in inkern.c, was refused by
# modpost ("local symbol 'iio_read_channel_label' was exported").  Adding
# the global was also refused, a local of the name taken for a duplicate.
#
# GNU ld is the judge: after `ld -r` both f are there, the relocations
# name the same ones as GNU ld's, and a program linked from it prints
# what GNU ld's prints; so does one we link finally; x86-64 and i386.
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
if ! command -v gcc >/dev/null 2>&1; then
    echo "skip: gcc makes the objects"
    exit 0
fi
mkdir "$work/tools"
${CC:-cc} -O0 -w -o "$work/tools/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > a.c <<'EOF2'
static int f(void) { return 1; }
int (*pa)(void) = f;
int ga(void) { return f(); }
EOF2
cat > b.c <<'EOF2'
int f(void) { return 2; }
int (*pb)(void) = f;
int gb(void) { return f() * 10; }
EOF2
cat > m.c <<'EOF2'
#include <stdio.h>
extern int (*pa)(void), (*pb)(void);
int f(void);
int ga(void);
int gb(void);
int main(void)
{
    printf("%d %d %d %d %d\n", pa(), pb(), f(), ga(), gb());
    return 0;
}
EOF2
# the symbols named f, with their binding and value, and each relocation's symbol, in order
fsyms() {
    readelf -sW "$1" | awk '$8 == "f" { print $5, $2 }' | tr '\n' ';'
}
relsyms() {
    readelf -rW "$1" | awk '/R_(X86_64|386)_/ { print $1, $5 }' | tr '\n' ';'
}
for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    gcc -m$m -O0 -fno-pie -c a.c b.c m.c 2>/dev/null || continue
    ${GNU_LD:-ld} -m $emu -r -o gnu.o a.o b.o || { echo "FAIL: GNU ld did not link it"; exit 1; }
    if ! ./tools/ld -m $emu -r -o our.o a.o b.o 2> said; then
        echo "FAIL: $emu -r: $(head -1 said)"; fail=1; continue
    fi
    if [ "$(fsyms our.o)" != "$(fsyms gnu.o)" ]; then
        echo "FAIL: $emu -r: f is [$(fsyms our.o)], GNU ld's [$(fsyms gnu.o)]"; fail=1
    fi
    if [ "$(relsyms our.o)" != "$(relsyms gnu.o)" ]; then
        echo "FAIL: $emu -r: relocations [$(relsyms our.o)], GNU ld's [$(relsyms gnu.o)]"; fail=1
    fi
    gcc -m$m -no-pie -o gnu m.o gnu.o && want=$(./gnu)
    gcc -m$m -no-pie -o our-r m.o our.o && got=$(./our-r)
    if [ "$got" != "$want" ]; then
        echo "FAIL: $emu -r: the program prints [$got], GNU ld's [$want]"; fail=1
    fi
    if ! gcc -m$m -B"$work/tools/" -no-pie -o our m.o a.o b.o 2> said; then
        echo "FAIL: $emu: $(grep -v collect2 said | head -1)"; fail=1; continue
    fi
    got=$(./our)
    if [ "$got" != "$want" ]; then
        echo "FAIL: $emu: the program prints [$got], GNU ld's [$want]"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_local_global_same_name: ok"
fi
exit $fail
