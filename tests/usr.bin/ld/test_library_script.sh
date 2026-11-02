#!/bin/sh
# -lNAME inside a library that is a linker script: searched for on the
# library path, as on the command line.
#
# The linker read a script's INPUT and GROUP for files only and passed
# over -lNAME, then, having loaded nothing, took the script for a shared
# library it could not use.  gcc 16 names libgcc_s_asneeded.so, which is
# `INPUT ( AS_NEEDED ( -lgcc_s ) )`, in every link, so no program linked
# with gcc linked ("cannot use shared library ... libgcc_s_asneeded.so").
#
# GNU ld is the judge: a program gcc links, a script naming -lNAME in
# AS_NEEDED that the program needs and one it does not, -lNAME in a GROUP
# finding an archive -- each links with the same DT_NEEDED as GNU ld's and
# runs; a script naming itself is refused.
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
printf 'int foo(void) { return 40; }\n' > foo.c
printf 'int bar(void) { return 1; }\n' > bar.c
printf 'int baz(void) { return 2; }\n' > baz.c
gcc -shared -fPIC -o lib/libfoo.so foo.c || exit 1
gcc -shared -fPIC -o lib/libbar.so bar.c || exit 1
gcc -c -o baz.o baz.c && ar rc lib/libbaz.a baz.o || exit 1
printf '/* GNU ld script */\nINPUT ( AS_NEEDED ( -lfoo ) )\n' > lib/libfoo_asneeded.so
printf '/* GNU ld script */\nINPUT ( AS_NEEDED ( -lbar ) )\n' > lib/libbar_asneeded.so
printf 'GROUP ( -lbaz )\n' > lib/libbazgroup.so
printf 'INPUT ( -lself )\n' > lib/libself.so
printf 'int foo(void);\nint baz(void);\nint main(void) { return foo() + baz(); }\n' > main.c
printf 'int main(void) { return 42; }\n' > plain.c

needed() {
    readelf -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort | tr '\n' ' '
}
# check NAME SOURCE WANT ARGS...: gcc links SOURCE with ARGS, by GNU ld and by ours, alike, exiting WANT
check() {
    what=$1
    src=$2
    want=$3
    shift 3
    rm -f gnu our
    if ! gcc -o gnu "$src" "$@" > gnu.said 2>&1; then
        echo "FAIL: $what: GNU ld did not link it: $(head -1 gnu.said)"; fail=1; return
    fi
    if ! gcc -B"$work/tools/" -o our "$src" "$@" > said 2>&1; then
        echo "FAIL: $what: $(grep -v collect2 said | head -1)"; fail=1; return
    fi
    if [ "$(needed our)" != "$(needed gnu)" ]; then
        echo "FAIL: $what: needs [$(needed our)], GNU ld's [$(needed gnu)]"; fail=1
    fi
    LD_LIBRARY_PATH="$work/lib" ./our
    rc=$?
    if [ $rc != "$want" ]; then
        echo "FAIL: $what: exits $rc, not $want"; fail=1
    fi
}

check "gcc's own link" plain.c 42
check "AS_NEEDED -l, needed and not" main.c 42 -Llib -lfoo_asneeded -lbar_asneeded -lbazgroup
check "-Wl,--as-needed around them" main.c 42 -Llib -Wl,--as-needed -lfoo_asneeded -lbar_asneeded \
    -lbazgroup -Wl,--no-as-needed

if gcc -B"$work/tools/" -o self plain.c -Llib -lself > said 2>&1; then
    echo "FAIL: a script naming itself was linked"; fail=1
fi

if [ $fail = 0 ]; then
    echo "test_library_script: ok"
fi
exit $fail
