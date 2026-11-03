#!/bin/sh
# --icf=safe: a function whose address is taken keeps an address of its own.
#
# safe mode kept writable and read-only data out of the fold and nothing
# else: two functions with the same code were folded whether or not
# anything took their addresses, so &fn1 == &fn2 came out true and C's
# promise that two functions have two addresses was broken -- the very
# thing safe mode is for.
#
# gold is the judge.  With --icf=safe it folds fn3 and fn4, which are only
# called, and leaves fn1 apart from fn2: fn1's address is stored in a
# pointer in .data and fn2's is loaded by code, each a relocation other
# than a call's.  With --icf=all all four pairs fold, and the program,
# which compares the two pointers, says so.  Both on x86-64 (R_X86_64_64,
# R_X86_64_32) and on i386 (R_386_32, and R_386_GOTOFF in
# position-independent code).
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
#define KEEP __attribute__((noinline, noipa))
KEEP int fn1(int x) { return x * 3 + 7; }
KEEP int fn2(int x) { return x * 3 + 7; }
KEEP int fn3(int x) { return x * 5 + 1; }
KEEP int fn4(int x) { return x * 5 + 1; }
int (*p1)(int) = fn1;
KEEP int (*get2(void))(int) { return fn2; }
#ifdef __x86_64__
static void quit(long code) {
    __asm__ volatile ("syscall" : : "a"(60L), "D"(code));
}
#else
static void quit(long code) {
    __asm__ volatile ("int $0x80" : : "a"(1L), "b"(code));
}
#endif
void _start(void) {
    /* 0: the pointers differ; 3: they are the same; 1: a call is wrong */
    long ok = fn1(1) == 10 && get2()(2) == 13 && fn3(1) == 6 && fn4(2) == 11;

    quit(!ok ? 1 : p1 != get2() ? 0 : 3);
    for (;;) {}
}
EOF

# The address of SYM in program PROG.
addr() {
    nm "$1" | awk -v s="$2" '$3 == s { print $1 }'
}

# prog, who, mode (safe|all), run (1 to run it)
check() {
    a1=$(addr "$1" fn1); a2=$(addr "$1" fn2); a3=$(addr "$1" fn3); a4=$(addr "$1" fn4)
    if [ -z "$a3" ] || [ "$a3" != "$a4" ]; then
        echo "FAIL: $2: fn3 $a3 and fn4 $a4, only called, are not folded"; fail=1
    fi
    if [ "$3" = safe ] && { [ -z "$a1" ] || [ "$a1" = "$a2" ]; }; then
        echo "FAIL: $2: fn1 and fn2, whose addresses are taken, are folded at $a1"; fail=1
    fi
    if [ "$3" = all ] && { [ -z "$a1" ] || [ "$a1" != "$a2" ]; }; then
        echo "FAIL: $2: fn1 $a1 and fn2 $a2 are not folded"; fail=1
    fi
    if [ "$4" = 1 ]; then
        "./$1"
        st=$?
        want=0
        [ "$3" = all ] && want=3
        if [ "$st" != "$want" ]; then
            echo "FAIL: $2: the program exits $st, not $want"; fail=1
        fi
    fi
}

# arch (64|32), gcc flags, gold emulation, our emulation
one() {
    run=1
    if ! ${GCC:-gcc} -m"$1" $2 -O2 -fno-builtin -ffreestanding -fno-asynchronous-unwind-tables \
        -fno-stack-protector -ffunction-sections -fdata-sections -c -o "a$1.o" a.c 2> said; then
        echo "skip: gcc -m$1 $2: $(head -1 said)"; return 0
    fi
    for mode in safe all; do
        if ! "${GOLD:-ld.gold}" -m "$3" --icf=$mode -o "gold$1$mode" "a$1.o" 2> said; then
            echo "skip: gold -m $3: $(head -1 said)"; return 0
        fi
        [ "$1" = 32 ] && { "./gold$1$mode" 2>/dev/null; [ $? -le 3 ] || run=0; }
        before=$fail
        check "gold$1$mode" "gold -m $3 --icf=$mode" $mode $run
        if [ "$fail" != "$before" ]; then
            echo "skip: gold does not fold as this test expects"; exit 0
        fi
        if ! ./ld -m "$4" --icf=$mode -o "ours$1$mode" "a$1.o" > said 2>&1; then
            echo "FAIL: ld -m $4 --icf=$mode: $(head -1 said)"; fail=1; continue
        fi
        check "ours$1$mode" "ld -m $4 $2 --icf=$mode" $mode $run
    done
}

one 64 "-fno-pic" elf_x86_64 elf_x86_64
one 32 "-fno-pic" elf_i386 elf_i386
one 32 "-fpie" elf_i386 elf_i386

[ "$fail" = 0 ] && echo "PASS: --icf=safe leaves functions whose addresses are taken apart, as gold does"
exit "$fail"
