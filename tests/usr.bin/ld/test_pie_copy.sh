#!/bin/sh
# Data a PIE reaches pc-relatively in a shared object: copied into it.
#
# gcc compiles a PIE on x86-64 to reach data it does not define with
# `movl v(%rip)`, R_X86_64_PC32, and GNU ld makes a copy of that data in
# the program with an R_X86_64_COPY relocation.  The linker made copies
# only in executables that are not PIEs, and refused the PIE ("stderr is
# data in a shared object and is referred to pc-relatively ... a copy
# relocation, which is not supported"); gcc makes PIEs by default, so a
# program using stderr, optind or environ did not link.  Linux's modpost
# is one.
#
# GNU ld is the judge: the copies a PIE gets (by name), and what the
# program does -- the library and the program see one object, written by
# either, its alias too; an object reached only by an absolute address in
# data is not copied, as GNU ld does not copy it.
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
cat > v.c <<'EOF'
int v = 5;
extern int v_alias __attribute__((weak, alias("v")));
long big[4] = { 1, 2, 3, 4 };
int only_abs = 7;
int *lib_v(void) { return &v; }
long lib_big(int i) { return big[i]; }
EOF
cat > main.c <<'EOF'
#include <stdio.h>
#include <unistd.h>
extern int v, v_alias, only_abs;
extern long big[4];
int *lib_v(void);
long lib_big(int i);
int *abs_ref = &only_abs;
int main(int argc, char **argv)
{
    int ok = 1;

    v += 1;
    ok &= *lib_v() == 6 && v_alias == 6;
    *lib_v() = 9;
    ok &= v == 9;
    big[2] = 30;
    ok &= lib_big(2) == 30 && big[3] == 4;
    ok &= *abs_ref == 7;
    ok &= getopt(argc, argv, "x") == 'x' && optind == 2;
    fprintf(stderr, "%s", "");
    return ok ? 42 : 1;
}
EOF
gcc -shared -fPIC -o lib/libv.so v.c || exit 1

copies() {
    readelf -rW "$1" 2>/dev/null | awk '/R_X86_64_COPY/ { sub(/@.*/, "", $5); print $5 }' | sort | tr '\n' ' '
}
gcc -pie -fPIE -o gnu main.c -Llib -lv || { echo "FAIL: GNU ld did not link it"; exit 1; }
if ! gcc -B"$work/tools/" -pie -fPIE -o our main.c -Llib -lv > said 2>&1; then
    echo "FAIL: $(grep -v collect2 said | head -1)"; exit 1
fi
if [ "$(copies our)" != "$(copies gnu)" ]; then
    echo "FAIL: copies [$(copies our)], GNU ld's [$(copies gnu)]"; fail=1
fi
LD_LIBRARY_PATH="$work/lib" ./gnu -x
want=$?
LD_LIBRARY_PATH="$work/lib" ./our -x
rc=$?
if [ $want != 42 ]; then
    echo "FAIL: GNU ld's program exits $want, not 42"; fail=1
elif [ $rc != $want ]; then
    echo "FAIL: the program exits $rc, GNU ld's $want"; fail=1
fi

if [ $fail = 0 ]; then
    echo "test_pie_copy: ok"
fi
exit $fail
