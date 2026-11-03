#!/bin/sh
# --icf and C++ exceptions: code with an LSDA is not folded.
#
# f1 and f2 below compile to the same bytes with the same relocations, but
# f1 catches A and f2 catches B: what each catches is in its LSDA (in
# .gcc_except_table), which its FDE in .eh_frame names.  --icf compared
# only the code and its relocations and put f1 and f2 at one address; two
# FDEs then covered that address with different LSDAs, the unwinder took
# either, and an exception thrown through one of them was caught by the
# wrong handler or not at all (the program died in std::terminate).
#
# gold is the judge: with --icf=all and --icf=safe it leaves f1 and f2
# apart and the program passes all four of its catch checks.  Ours must do
# the same.
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
cat > ex.cc <<'EOF'
struct A { int v; };
struct B { int v; };
__attribute__((noinline)) void thrower(int k)
{
    if (k == 1) throw A{1};
    if (k == 2) throw B{2};
}
__attribute__((noinline)) int f1(int k) { int r = 0; try { thrower(k); } catch (A &) { r = 1; } return r; }
__attribute__((noinline)) int f2(int k) { int r = 0; try { thrower(k); } catch (B &) { r = 1; } return r; }
int main()
{
    int bad = 0;

    if (f1(1) != 1) bad |= 1;
    try { if (f1(2) != 0) bad |= 2; bad |= 2; } catch (B &) { }
    if (f2(2) != 1) bad |= 4;
    try { if (f2(1) != 0) bad |= 8; bad |= 8; } catch (A &) { }
    return bad;
}
EOF
g++ -O2 -fno-pie -ffunction-sections -fno-reorder-blocks-and-partition -c -o ex.o ex.cc || exit 1

addr() {
    nm "$1" | awk -v s="$2" '$3 == s { print $1 }'
}

for mode in all safe; do
    g++ -no-pie -fuse-ld=gold -Wl,--icf=$mode -o gold ex.o || { echo "FAIL: gold --icf=$mode did not link"; exit 1; }
    if [ "$(addr gold _Z2f1i)" = "$(addr gold _Z2f2i)" ] || ! ./gold; then
        echo "skip: gold --icf=$mode does not keep f1 and f2 apart as this test expects"
        exit 0
    fi
    if ! g++ -no-pie -B"$work/tools/" -Wl,--icf=$mode -o ours ex.o > said 2>&1; then
        echo "FAIL: ld --icf=$mode: $(grep -v collect2 said | head -1)"; fail=1; continue
    fi
    if [ "$(addr ours _Z2f1i)" = "$(addr ours _Z2f2i)" ]; then
        echo "FAIL: ld --icf=$mode folds f1 and f2, which catch different exceptions"; fail=1
    fi
    ./ours > /dev/null 2>&1
    rc=$?
    if [ $rc != 0 ]; then
        echo "FAIL: ld --icf=$mode: the program fails its catch checks (exit $rc)"; fail=1
    fi
done

[ "$fail" = 0 ] && echo "PASS: --icf leaves code with an LSDA unfolded, as gold does"
exit "$fail"
