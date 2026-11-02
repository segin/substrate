#!/bin/sh
# A copied variable's other names: the ones GNU ld gives the copy.
#
# A shared object may define one variable under two names.  GNU ld pairs
# a weak name with a strong one at the same place (glibc's environ and
# __environ) and copies the variable once, both names on the copy; two
# strong names it takes for two variables, each copied by itself.  The
# linker put every name at the place on the one copy, so a program
# referring to two strong names saw one variable where GNU ld's sees two.
#
# GNU ld is the judge: a program writing through each name of a weak pair
# and a strong pair, and the library reading them, prints what GNU ld's
# prints, and has the same copies, as a PIE and as an executable that is
# not one.
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
int w = 1;
extern int w_weak __attribute__((weak, alias("w")));
int s = 2;
extern int s_strong __attribute__((alias("s")));
int lib_w(void) { return w; }
int lib_s(void) { return s; }
int lib_s_strong(void) { return s_strong; }
EOF
cat > main.c <<'EOF'
#include <stdio.h>
extern int w, w_weak, s, s_strong;
int lib_w(void);
int lib_s(void);
int lib_s_strong(void);
int main(void)
{
    w += 10;
    w_weak += 100;
    s += 10;
    s_strong += 100;
    printf("%d %d %d %d %d %d %d\n", w, w_weak, lib_w(), s, s_strong, lib_s(), lib_s_strong());
    return 0;
}
EOF
gcc -shared -fPIC -o lib/libv.so v.c || exit 1

copies() {
    readelf -rW "$1" 2>/dev/null | awk '/_COPY/ { sub(/@.*/, "", $5); print $5 }' | sort | tr '\n' ' '
}
for kind in "-pie -fPIE" "-no-pie -fno-pie"; do
    rm -f gnu our
    gcc $kind -o gnu main.c -Llib -lv || { echo "FAIL: $kind: GNU ld did not link it"; fail=1; continue; }
    if ! gcc -B"$work/tools/" $kind -o our main.c -Llib -lv > said 2>&1; then
        echo "FAIL: $kind: $(grep -v collect2 said | head -1)"; fail=1; continue
    fi
    want=$(LD_LIBRARY_PATH="$work/lib" ./gnu)
    got=$(LD_LIBRARY_PATH="$work/lib" ./our)
    if [ "$got" != "$want" ]; then
        echo "FAIL: $kind: prints [$got], GNU ld's [$want]"; fail=1
    fi
    if [ "$(copies our)" != "$(copies gnu)" ]; then
        echo "FAIL: $kind: copies [$(copies our)], GNU ld's [$(copies gnu)]"; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_copy_alias: ok"
fi
exit $fail
