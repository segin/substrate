#!/bin/sh
# Linking against shared objects: what the output says it needs and how
# its calls reach them.  The linker is built for the host out of the tree
# and links freestanding 32-bit objects; the results are read with the
# host's readelf and objdump.  That such a program runs is checked on the
# target, not here.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top/usr.bin/ld/ld.c" "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > lib.c <<'EOF'
int one(int x) { return x + 1; }
int two(int x) { return x + 2; }
int three(int x) { return x + 3; }
EOF
cat > main.c <<'EOF'
int one(int), two(int), three(int);
extern int absent(int) __attribute__((weak));
void _start(void) { int r = one(1) + two(2) + three(3); if (absent) r += absent(r); for (;;) { } }
EOF
cc32="${CC:-cc} -m32 -c -ffreestanding -fno-asynchronous-unwind-tables -fno-stack-protector"
$cc32 -fPIC -o lib.o lib.c && $cc32 -fno-pic -fno-pie -o main.o main.c || { echo "SKIP: no 32-bit compiler"; exit 0; }

is() {    # WHAT GOT WANT
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi
}
run() {   # WHAT COMMAND...
    what=$1; shift
    if "$@" > err 2>&1; then echo "ok   $what"; else echo "FAIL $what: $(head -1 err)"; fail=1; fi
}

# A library is known by the name it gives itself.
run "a shared object with a soname" ./ld -m elf_i386 -shared -soname libnum.so.3 -rpath /opt/a --rpath=/opt/b -o libnum.so lib.o
is  "DT_SONAME"  "$(readelf -d libnum.so | sed -n 's/.*soname: \[\(.*\)\]/\1/p')" libnum.so.3
is  "DT_RUNPATH" "$(readelf -d libnum.so | sed -n 's/.*runpath: \[\(.*\)\]/\1/p')" /opt/a:/opt/b
run "-h is -soname" ./ld -m elf_i386 -shared -h libh.so.1 -o libh.so lib.o
is  "DT_SONAME from -h" "$(readelf -d libh.so | sed -n 's/.*soname: \[\(.*\)\]/\1/p')" libh.so.1

run "a program against it" ./ld -m elf_i386 --dynamic-linker=/sbin/ld.so -o prog main.o libnum.so
is  "needed by its soname" "$(readelf -d prog | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')" libnum.so.3
is  "the interpreter"      "$(readelf -lW prog | sed -n 's/.*interpreter: \(.*\)\]/\1/p')" /sbin/ld.so

# Calls from code that is not position-independent go through the PLT,
# and each entry pushes where its relocation is, in bytes.
is  "a PLT relocation for each function called" "$(readelf -rW prog | grep -c R_386_JUMP_SLOT)" 3
is  "what the entries push" "$(objdump -d -j .plt prog | sed -n 's/.*push  *\$\(0x[0-9a-f]*\)$/\1/p' | tr '\n' ' ')" "0x0 0x8 0x10 "

# A weak reference to what nothing defines is nothing, now.
is  "no relocation left for the dynamic linker in the text" "$(readelf -rW prog | grep -c 'R_386_32 ')" 0
is  "absent is not asked of the dynamic linker" "$(readelf -W --dyn-syms prog | grep -c ' absent$')" 0

# An option that needs a value says so.
if ./ld -m elf_i386 -shared -o x.so lib.o -soname > err 2>&1; then echo "FAIL -soname with nothing after it: linked"; fail=1
elif grep -q "needs a value" err; then echo "ok   -soname with nothing after it"
else echo "FAIL -soname with nothing after it: $(head -1 err)"; fail=1; fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
