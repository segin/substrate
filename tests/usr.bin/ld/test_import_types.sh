#!/bin/sh
# What a program's .symtab says of what it imports.
#
# An undefined symbol kept the type of the reference, which is none: our
# cc's .symtab had its 79 libc functions as NOTYPE, and without the
# version each is bound to.  GNU ld gives an import the type of the
# shared object's definition and names it with its version
# (__errno_location@GLIBC_2.2.5), and a copy of a shared object's
# variable the same way (stdout@GLIBC_2.2.5).
#
# GNU ld is the judge, through gcc -B: a program and a PIE that call libc
# and a versioned shared object of their own and use stdout have GNU
# ld's undefined and copied symbols in .symtab, by type, binding, size
# and name with version, and run.
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
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > lib.c <<'EOF'
int lib_var = 1;
int lib_func(int x) { return x + lib_var; }
EOF
printf 'LIB_2 { global: lib_var; lib_func; local: *; };\n' > lib.map
cat > prog.c <<'EOF'
#include <stdio.h>
#include <string.h>
extern int lib_func(int);
int main(void) { fputs(strchr("imports\n", 'p'), stdout); return lib_func(1) == 2 ? 0 : 1; }
EOF
${GCC:-gcc} -O2 -fPIC -shared -Wl,--version-script=lib.map -o libt.so lib.c || exit 1
mkdir -p bin && ln -sf "$work/ld" bin/ld

# The undefined and copied symbols of OBJ's .symtab: type, binding, size, name.
imports() {
    readelf -sW "$1" | sed -n '/Symbol table .\.symtab/,$p' |
        awk 'NR > 2 && $8 != "" && ($7 == "UND" || $8 ~ /@/) && $8 !~ /^_GLOBAL_OFFSET_TABLE_$/ {
            print $4, $5, $3, ($7 == "UND" ? "UND" : "DEF"), $8 }' | sort
}

for mode in -no-pie -pie; do
    if [ $mode = -pie ]; then pic=-fPIE; else pic=-fno-pie; fi
    ${GCC:-gcc} -O2 $pic -c -o prog.o prog.c || exit 1
    ${GCC:-gcc} $mode -o gnu.out prog.o -L. -lt || exit 1
    if ! ${GCC:-gcc} -B"$work/bin/" $mode -o ours.out prog.o -L. -lt > said 2>&1; then
        echo "FAIL: ld $mode: $(head -1 said)"; fail=1; continue
    fi
    imports gnu.out > want
    imports ours.out > have
    if [ ! -s want ] || ! cmp -s want have; then
        echo "FAIL: ld $mode: the imports in .symtab differ from GNU ld's:"
        diff want have | sed 's/^/    /'
        fail=1
    fi
    if [ "$(LD_LIBRARY_PATH=. ./ours.out)" != "ports" ]; then
        echo "FAIL: ld $mode: the program does not run as it should"; fail=1
    fi
done

if [ $fail -eq 0 ]; then
    echo "ok   test_import_types"
fi
exit $fail
