#!/bin/sh
# A copied variable's other names, when the shared object versions them.
#
# A shared object that is not stripped names a versioned definition in its
# full symbol table as "var@@V2", beside the "var" of its dynamic one.  The
# linker took every name at the copied variable's place for one of its
# names, the "@@V2" with it: the program got symbols "var@@V2" and
# "wvar@@V2" on the copy, exported them as definitions of their own, so
# that its dynamic symbols named var and wvar twice, and it defined V2
# itself instead of needing it from the library.
#
# GNU ld is the judge: a program referring to one name of a versioned
# weak/strong pair has the same copies, dynamic symbols, symbols and
# version sections as GNU ld's, and prints what GNU ld's prints, as a PIE
# and as an executable that is not one.
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
    echo "skip: gcc links the programs"
    exit 0
fi
mkdir "$work/tools" "$work/lib"
${CC:-cc} -O0 -w -o "$work/tools/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > t.c <<'EOF'
int var_new = 5;
extern int wvar_new __attribute__((weak, alias("var_new")));
__asm__(".symver var_new, var@@V2");
__asm__(".symver wvar_new, wvar@@V2");
int lib_var(void) { return var_new; }
int lib_wvar(void) { return wvar_new; }
EOF
cat > t.map <<'EOF'
V2 { global: var; wvar; lib_var; lib_wvar; local: *; };
EOF
cat > main.c <<'EOF'
#include <stdio.h>
extern int var;
int lib_var(void);
int lib_wvar(void);
int main(void)
{
    var += 10;
    printf("%d %d %d\n", var, lib_var(), lib_wvar());
    return 0;
}
EOF
gcc -shared -fPIC -Wl,--version-script=t.map -o lib/libt.so t.c || exit 1
if ! readelf -sW lib/libt.so | grep -q ' var@@V2$'; then
    echo "skip: the library's full symbol table does not name var@@V2"
    exit 0
fi

copies() {
    readelf -rW "$1" 2>/dev/null | awk '/_COPY/ { print $5 }' | sort | tr '\n' ' '
}
# The program's dynamic symbols for the variable, with the versions readelf
# shows beside them (not their indexes, which follow the order of the
# needed versions).
names() {
    readelf -W --dyn-syms "$1" 2>/dev/null | awk '$8 ~ /^w?var($|@)/ { print $8 }' | sort | tr '\n' ' '
}
# Its full symbol table's names: var's, and any still spelling a default
# version, as no output symbol's name does.
symtab() {
    readelf -W --syms "$1" 2>/dev/null | awk '/^Symbol table .\.symtab/ { t = 1 }
        t && ($8 ~ /^var($|@)/ || $8 ~ /@@/) { print $8 }' | sort | tr '\n' ' '
}
# The version sections and where V2 comes from.
versions() {
    readelf -VW "$1" 2>/dev/null | awk '/^Version (definition|needs) section/ { print $1, $2, $3 }
        /File:/ { file = $5 } /Name: V2/ { print "V2", file }' | sort | tr '\n' ' '
}
for kind in "-pie -fPIE" "-no-pie -fno-pie"; do
    rm -f gnu our
    gcc $kind -o gnu main.c -Llib -lt || { echo "FAIL: $kind: GNU ld did not link it"; fail=1; continue; }
    if ! gcc -B"$work/tools/" $kind -o our main.c -Llib -lt > said 2>&1; then
        echo "FAIL: $kind: $(grep -v collect2 said | head -1)"; fail=1; continue
    fi
    want=$(LD_LIBRARY_PATH="$work/lib" ./gnu)
    got=$(LD_LIBRARY_PATH="$work/lib" ./our 2>&1)
    if [ "$got" != "$want" ]; then
        echo "FAIL: $kind: prints [$got], GNU ld's [$want]"; fail=1
    fi
    if [ "$(copies our)" != "$(copies gnu)" ]; then
        echo "FAIL: $kind: copies [$(copies our)], GNU ld's [$(copies gnu)]"; fail=1
    fi
    if [ "$(names our)" != "$(names gnu)" ]; then
        echo "FAIL: $kind: .dynsym [$(names our)], GNU ld's [$(names gnu)]"; fail=1
    fi
    if [ "$(symtab our)" != "$(symtab gnu)" ]; then
        echo "FAIL: $kind: .symtab [$(symtab our)], GNU ld's [$(symtab gnu)]"; fail=1
    fi
    if [ "$(versions our)" != "$(versions gnu)" ]; then
        echo "FAIL: $kind: versions [$(versions our)], GNU ld's [$(versions gnu)]"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_copy_alias_versioned: ok"
fi
exit $fail
