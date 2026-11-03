#!/bin/sh
# --icf: identical functions are folded as the input sections they are.
#
# The fold ran on the merged output, comparing one whole output section
# with another; by then every .text.NAME a compiler given
# -ffunction-sections made was one .text, so two functions with the same
# code were never compared and --icf folded nothing.
#
# gold is the judge (GNU ld has no --icf): with --icf=all it puts fn1 and
# fn2 of one file, and fn4 of another, at one address; fn3, whose code
# differs, stays apart, and so do geta and getb, the same bytes reading
# two different static variables.  The program still runs, and under
# --icf=none nothing is folded.
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
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > a.c <<'EOF'
extern int fn4(int);
static int va = 1;
static int vb = 1;
int fn1(int x) { return x * 3 + 7; }
int fn2(int x) { return x * 3 + 7; }
int fn3(int x) { return x * 5 + 7; }
int geta(void) { return va; }
int getb(void) { return vb; }
void seta(int v) { va = v; }
void _start(void) {
    long ok;

    seta(5);
    ok = fn1(1) == 10 && fn2(2) == 13 && fn3(1) == 12 && fn4(3) == 16 && geta() == 5 && getb() == 1;
    __asm__ volatile ("syscall" : : "a"(60L), "D"(ok ? 0L : 1L));
    for (;;) {}
}
EOF
cat > b.c <<'EOF'
int fn4(int x) { return x * 3 + 7; }
EOF
for f in a b; do
    ${GCC:-gcc} -O2 -fno-pic -fno-builtin -ffreestanding -fno-asynchronous-unwind-tables \
        -fno-stack-protector -ffunction-sections -fdata-sections -c -o $f.o $f.c || exit 1
done

# The address of SYM in program PROG.
addr() {
    nm "$1" | awk -v s="$2" '$3 == s { print $1 }'
}

check() {   # prog, who
    a1=$(addr "$1" fn1); a2=$(addr "$1" fn2); a3=$(addr "$1" fn3); a4=$(addr "$1" fn4)
    ga=$(addr "$1" geta); gb=$(addr "$1" getb)
    if [ -z "$a1" ] || [ "$a1" != "$a2" ] || [ "$a1" != "$a4" ]; then
        echo "FAIL: $2: fn1 $a1, fn2 $a2 and fn4 $a4 are not folded"; fail=1
    fi
    if [ "$a1" = "$a3" ]; then
        echo "FAIL: $2: fn3, whose code differs, is folded with fn1"; fail=1
    fi
    if [ -z "$ga" ] || [ "$ga" = "$gb" ]; then
        echo "FAIL: $2: geta and getb, which read different variables, are folded"; fail=1
    fi
    if ! "./$1"; then
        echo "FAIL: $2: the program does not run"; fail=1
    fi
}

"${GOLD:-ld.gold}" --icf=all -o gold a.o b.o || exit 1
check gold gold
[ "$fail" = 0 ] || { echo "skip: gold does not fold as this test expects"; exit 0; }

if ! ./ld -m elf_x86_64 --icf=all -o ours a.o b.o > said 2>&1; then
    echo "FAIL: ld --icf=all: $(head -1 said)"; exit 1
fi
check ours "ld --icf=all"

if ! ./ld -m elf_x86_64 --icf=none -o none a.o b.o > said 2>&1; then
    echo "FAIL: ld --icf=none: $(head -1 said)"; exit 1
fi
if [ "$(addr none fn1)" = "$(addr none fn2)" ]; then
    echo "FAIL: ld --icf=none folds fn1 and fn2"; fail=1
fi

[ "$fail" = 0 ] && echo "PASS: --icf folds identical input sections as gold does"
exit "$fail"
