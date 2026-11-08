#!/bin/sh
# What a PIE puts in its .dynsym.
#
# A PIE is ET_DYN, and the linker exported from it every global it
# defined, as from a shared object: our cc's .dynsym was 12360 bytes,
# GNU ld's 2112.  GNU ld exports from a program, PIE or not, only what
# it imports, what a shared object of the link refers to, and its copies
# of shared objects' variables (with the version of the definition they
# copy); everything with -E.  _GLOBAL_OFFSET_TABLE_ is never among them.
#
# GNU ld is the judge: a PIE and a program linked against a shared object
# with versioned symbols, with and without -E, have GNU ld's dynamic
# symbols, versions and all, and run.
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
int lib_var = 42;
extern int prog_callback(int);
int lib_call(int x) { return prog_callback(x) + 1; }
EOF
cat > lib.map <<'EOF'
LIB_1 { global: lib_var; lib_call; local: *; };
EOF
cat > prog.c <<'EOF'
extern int lib_var;
extern int lib_call(int);
int prog_global = 7;
int prog_helper(int x) { return x * 2; }
int prog_callback(int x) { return prog_helper(x) + prog_global; }
int main(void) { return lib_call(lib_var) == 92 ? 0 : 1; }
EOF
${GCC:-gcc} -O2 -fPIC -shared -Wl,--version-script=lib.map -o libt.so lib.c || exit 1

# The dynamic symbols of OBJ: defined or not, binding, name with version, sorted.
dynsyms() {
    readelf --dyn-syms -W "$1" | awk 'NR > 3 && $8 != "" { print ($7 == "UND" ? "UND" : "DEF"), $5, $8 }' | sort
}

# check FLAGS...: GNU ld and this one, through gcc -B
for mode in -pie -no-pie; do
    if [ $mode = -pie ]; then pic=-fPIE; else pic=-fno-pie; fi
    ${GCC:-gcc} -O2 $pic -c -o prog.o prog.c || exit 1
    for e in "" -Wl,-E; do
        ${GCC:-gcc} $mode $e -o gnu.out prog.o -L. -lt || { echo "skip: gcc $mode $e"; continue; }
        mkdir -p bin && ln -sf "$work/ld" bin/ld
        if ! ${GCC:-gcc} -B"$work/bin/" $mode $e -o ours.out prog.o -L. -lt > said 2>&1; then
            echo "FAIL: ld $mode $e: $(head -1 said)"; fail=1; continue
        fi
        dynsyms gnu.out > want
        dynsyms ours.out > have
        if ! cmp -s want have; then
            echo "FAIL: ld $mode $e: dynamic symbols differ from GNU ld's:"
            diff want have | sed 's/^/    /'
            fail=1
        fi
        if ! LD_LIBRARY_PATH=. ./ours.out; then
            echo "FAIL: ld $mode $e: the program does not run as it should"; fail=1
        fi
    done
done

# A weak reference nothing defines is in a position-dependent program's
# .dynsym only when the program reaches it through its GOT: not for an
# absolute reference or a call, though a shared object of the link wants
# it (Ubuntu's crtbegin.o refers to _ITM_registerTMCloneTable absolutely;
# libraries made with crtbeginS.o want it through their GOT, and it went
# out where GNU ld leaves it).
cat > wlib.c <<'EOF'
extern void weak_fn(void) __attribute__((weak));
void *lib_wants(void) { return (void *)weak_fn; }
EOF
cat > wprog.c <<'EOF'
extern void weak_fn(void) __attribute__((weak));
extern void *got_user(void);
void *absolute(void) { return (void *)weak_fn; }
int main(void) { if (weak_fn) weak_fn(); return got_user() != 0; }
EOF
${GCC:-gcc} -O2 -fPIC -shared -o libw.so wlib.c || exit 1
${GCC:-gcc} -O2 -fno-pie -c -o wprog.o wprog.c || exit 1
# weak_got through the GOT, as crti.o reaches __gmon_start__
printf '\t.text\n\t.globl got_user\ngot_user:\n\tmovq weak_got@GOTPCREL(%%rip), %%rax\n\tret\n\t.weak weak_got\n' > wgot.s
if ${GCC:-gcc} -c -o wgot.o wgot.s 2>/dev/null &&
   ${GCC:-gcc} -no-pie -o gnu.out wprog.o wgot.o -L. -lw 2>/dev/null; then
    ${GCC:-gcc} -B"$work/bin/" -no-pie -o ours.out wprog.o wgot.o -L. -lw > said 2>&1 ||
        { echo "FAIL: ld -no-pie, weak references: $(head -1 said)"; fail=1; }
    dynsyms gnu.out > want
    dynsyms ours.out > have
    if ! cmp -s want have; then
        echo "FAIL: ld -no-pie, weak references: dynamic symbols differ from GNU ld's:"
        diff want have | sed 's/^/    /'
        fail=1
    fi
    if ! LD_LIBRARY_PATH=. ./ours.out; then
        echo "FAIL: ld -no-pie, weak references: the program does not run as it should"; fail=1
    fi
else
    echo "skip: weak references (not an x86-64 gcc)"
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_pie_exports"
fi
exit $fail
