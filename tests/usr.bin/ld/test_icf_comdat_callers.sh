#!/bin/sh
# --icf folds template instances (COMDAT groups) and the callers of what it
# folds.
#
# A section of a group was never a candidate, and every template instance
# and inline function of C++ is in one: Box<int>::get() and
# Box<unsigned>::get(), the same code, stayed apart.  And the classes were
# found in one pass, comparing what relocations refer to by name: g1 and
# g2, calling h1 and h2 (identical), stayed apart though they become
# identical once h1 and h2 are folded; so did f1 and f2, calling the two
# get()s.  The output was right, only larger, but C++ template bloat is
# what --icf is for.
#
# gold is the judge, with --icf=all and --icf=safe: the two get()s, f1 and
# f2, h1 and h2, g1 and g2 and r1 and r2 (each calling itself) are each
# folded; k1 and k2, calling h1 and h3 (whose code differs), are not; and
# the program runs.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GOLD:-ld.gold} --version 2>/dev/null | grep -q 'gold'; then
    echo "skip: gold is the judge"
    exit 0
fi
if ! command -v g++ >/dev/null 2>&1; then
    echo "skip: g++ compiles the program"
    exit 0
fi
mkdir "$work/tools"
${CC:-cc} -O0 -w -o "$work/tools/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > t.cc <<'EOF'
template <class T> struct Box {
    T v;
    __attribute__((noipa)) T get() const { return v; }
};
__attribute__((noinline)) int f1(const Box<int> &b) { return b.get() + 1; }
__attribute__((noinline)) unsigned f2(const Box<unsigned> &b) { return b.get() + 1; }
extern "C" {
__attribute__((noinline)) int h1(int x) { return x * 7 + 3; }
__attribute__((noinline)) int h2(int x) { return x * 7 + 3; }
__attribute__((noinline)) int h3(int x) { return x * 9 + 3; }
__attribute__((noinline)) int g1(int x) { return h1(x) + 5; }
__attribute__((noinline)) int g2(int x) { return h2(x) + 5; }
__attribute__((noinline)) int k1(int x) { return h1(x) + 6; }
__attribute__((noinline)) int k2(int x) { return h3(x) + 6; }
__attribute__((noipa)) int r1(int x) { return x <= 0 ? 0 : r1(x - 1) + 2; }
__attribute__((noipa)) int r2(int x) { return x <= 0 ? 0 : r2(x - 1) + 2; }
}
int main()
{
    Box<int> a{1};
    Box<unsigned> b{2};

    return !(f1(a) == 2 && f2(b) == 3 && g1(1) == 15 && g2(2) == 22 && k1(1) == 16 && k2(1) == 18 &&
             r1(3) == 6 && r2(4) == 8);
}
EOF
g++ -O2 -fno-pie -ffunction-sections -fno-asynchronous-unwind-tables -fno-exceptions -c -o t.o t.cc || exit 1
if [ "$(readelf -SW t.o | grep -c ' GROUP ')" -lt 2 ]; then
    echo "skip: g++ did not put the two get()s in groups"
    exit 0
fi

addr() {
    nm "$1" | awk -v s="$2" '$3 == s { print $1 }'
}
# "same" or "apart" for each pair, as PROG has them.
pairs() {
    for p in _ZNK3BoxIiE3getEv:_ZNK3BoxIjE3getEv _Z2f1RK3BoxIiE:_Z2f2RK3BoxIjE h1:h2 g1:g2 r1:r2 k1:k2; do
        x=$(addr "$1" "${p%%:*}"); y=$(addr "$1" "${p##*:}")
        if [ -z "$x" ] || [ -z "$y" ]; then
            echo "$p missing"
        elif [ "$x" = "$y" ]; then
            echo "$p same"
        else
            echo "$p apart"
        fi
    done
}
want="_ZNK3BoxIiE3getEv:_ZNK3BoxIjE3getEv same
_Z2f1RK3BoxIiE:_Z2f2RK3BoxIjE same
h1:h2 same
g1:g2 same
r1:r2 same
k1:k2 apart"

for mode in all safe; do
    g++ -no-pie -fuse-ld=gold -Wl,--icf=$mode -o gold t.o || { echo "FAIL: gold --icf=$mode did not link"; exit 1; }
    if [ "$(pairs gold)" != "$want" ] || ! ./gold; then
        echo "skip: gold --icf=$mode does not fold as this test expects:"; pairs gold | sed 's/^/    /'
        exit 0
    fi
    if ! g++ -no-pie -B"$work/tools/" -Wl,--icf=$mode -o ours t.o > said 2>&1; then
        echo "FAIL: ld --icf=$mode: $(grep -v collect2 said | head -1)"; fail=1; continue
    fi
    if [ "$(pairs ours)" != "$want" ]; then
        echo "FAIL: ld --icf=$mode folds otherwise than gold:"
        pairs ours | sed 's/^/    /'; fail=1
    fi
    if ! ./ours; then
        echo "FAIL: ld --icf=$mode: the program does not run"; fail=1
    fi
done

[ "$fail" = 0 ] && echo "PASS: --icf folds template instances and their callers, as gold does"
exit "$fail"
