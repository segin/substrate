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

# What code that is not position-independent refers to directly has to be
# somewhere the linker knows: a variable of the library gets a copy in the
# program, under all the library's names for it, and a function whose
# address is taken has its PLT entry for an address.
cat > lib2.c <<'EOF'
int shared_var = 7;
extern int shared_alias __attribute__((alias("shared_var")));
int shared_fn(int x) { return x + shared_var; }
EOF
cat > main2.c <<'EOF'
extern int shared_var;
int shared_fn(int);
int (*pointer)(int) = shared_fn;
void _start(void) { shared_var = pointer(shared_var) + (shared_fn == pointer); for (;;) { } }
EOF
$cc32 -fPIC -o lib2.o lib2.c && $cc32 -fno-pic -fno-pie -o main2.o main2.c
run "a library with a variable" ./ld -m elf_i386 -shared -soname lib2.so.1 -o lib2.so lib2.o
run "a program that uses it directly" ./ld -m elf_i386 -z text --dynamic-linker=/sbin/ld.so -o prog2 main2.o lib2.so
is  "a copy relocation for the variable" "$(readelf -rW prog2 | grep -c 'R_386_COPY .* shared_var')" 1
is  "the program defines the variable"   "$(readelf -W --dyn-syms prog2 | awk '$8 == "shared_var" { print $4, ($7 != "UND") }')" "OBJECT 1"
is  "and its other name, at the same place" \
    "$(readelf -W --dyn-syms prog2 | awk '$8 == "shared_var" || $8 == "shared_alias" { print $2 }' | sort -u | wc -l)" 1
is  "the function's address is its PLT entry" \
    "$(readelf -W --dyn-syms prog2 | awk '$8 == "shared_fn" { print ($2 != "00000000"), $7 }')" "1 UND"
is  "nothing for the dynamic linker to write in the text" "$(readelf -d prog2 | grep -c TEXTREL)" 0

# -z text is about what the dynamic linker would have to write on, not
# about there being relocations: an ordinary link passes, a shared object
# made of code that is not position-independent does not, and without -z
# text it is marked.
$cc32 -fno-pic -o lib2np.o lib2.c
run "-z text and position-independent code" ./ld -m elf_i386 -shared -z text -o pic.so lib.o
if ./ld -m elf_i386 -shared -z text -o np.so lib2np.o > err 2>&1; then echo "FAIL -z text and code that is not: linked"; fail=1
elif grep -q "read-only and has relocations" err; then echo "ok   -z text and code that is not"
else echo "FAIL -z text and code that is not: $(head -1 err)"; fail=1; fi
run "the same without -z text" ./ld -m elf_i386 -shared -z notext -o np.so lib2np.o
is  "DT_TEXTREL" "$(readelf -d np.so | grep -c '(TEXTREL)')" 1

# What a library refers to and the program defines is in the program's
# dynamic symbol table, where the library can find it; what nothing asks
# for is not.
cat > lib3.c <<'EOF'
int program_hook(int);
int through(int x) { return program_hook(x); }
EOF
cat > main3.c <<'EOF'
int through(int);
int program_hook(int x) { return x + 1; }
int program_private(int x) { return x + 2; }
void _start(void) { through(program_private(1)); for (;;) { } }
EOF
$cc32 -fPIC -o lib3.o lib3.c && $cc32 -fno-pic -fno-pie -o main3.o main3.c
run "a library that calls back" ./ld -m elf_i386 -shared -o lib3.so lib3.o
run "a program that it calls back into" ./ld -m elf_i386 --dynamic-linker=/sbin/ld.so -o prog3 main3.o lib3.so
is  "the hook is exported, defined" "$(readelf -W --dyn-syms prog3 | awk '$8 == "program_hook" { print ($7 != "UND") }')" 1
is  "what nothing asks for is not"  "$(readelf -W --dyn-syms prog3 | grep -c ' program_private$')" 0

# What only the dynamic linker writes is together on pages of its own and
# PT_GNU_RELRO covers those pages whole; what the program writes comes
# after.  And a library's reference to its own variable through the GOT
# is a direct one: the table has no slot to hold an address not yet known.
cat > lib4.c <<'EOF'
static const char *const names[] = { "a", "b" };
int lib_counter = 1;
const char *lib_name(int i) { return names[i]; }
int lib_bump(void) { return ++lib_counter; }
EOF
$cc32 -fPIC -o lib4.o lib4.c
run "a library with data that is read-only once relocated" ./ld -m elf_i386 -shared -o lib4.so lib4.o
start=$(readelf -lW lib4.so | awk '$1 == "GNU_RELRO" { print $3 }')
size=$(readelf -lW lib4.so | awk '$1 == "GNU_RELRO" { print $6 }')
data=0x$(readelf -SW lib4.so | sed -n 's/.* \.data  *PROGBITS  *\([0-9a-f]*\) .*/\1/p')
ro=0x$(readelf -SW lib4.so | sed -n 's/.* \.data\.rel\.ro[^ ]*  *PROGBITS  *\([0-9a-f]*\) .*/\1/p' | head -1)
is  "PT_GNU_RELRO begins and ends on a page" "$(( start % 4096 )) $(( size % 4096 )) $(( size > 0 ))" "0 0 1"
is  ".data.rel.ro is inside it"       "$(( ro >= start && ro < start + size ))" 1
is  ".data is after it"               "$(( data >= start + size ))" 1
is  "the library reaches its own variable directly" \
    "$(objdump -d lib4.so | sed -n '/<lib_bump>:/,/ret/p' | grep -c 'mov  *0x0(%e[a-z]x),')" 0

# -l: the directories in order, each for the shared library and then the
# archive, and a library for another machine is passed over.  The first
# input says which machine the link is for, and looking into a library
# does not change it.
${CC:-cc} -m64 -c -fPIC -ffreestanding -o lib64.o lib.c
mkdir -p d64 d32 darch
if ./ld -m elf_x86_64 -shared -o d64/libnum.so lib64.o 2> err; then
    cp libnum.so d32/libnum.so
    ar rc darch/libnum.a lib.o
    run "a library for another machine earlier in the path" ./ld -o p_mix main.o -Ld64 -Ld32 -lnum
    is  "the link is still for the first input's machine" "$(readelf -h p_mix | sed -n 's/.*Class: *//p')" ELF32
    is  "and uses the library that suits it" "$(readelf -d p_mix | grep -c 'libnum.so.3')" 1
    run "an archive in an earlier directory" ./ld -o p_arch main.o -Ldarch -Ld32 -lnum
    is  "is found before a shared library in a later one" "$(readelf -d p_arch 2>/dev/null | grep -c NEEDED)" 0
    run "a shared library in an earlier directory" ./ld -o p_so main.o -Ld32 -Ldarch -lnum
    is  "is found before an archive in a later one" "$(readelf -d p_so | grep -c NEEDED)" 1
else
    echo "FAIL a 64-bit shared object: $(head -1 err)"; fail=1
fi

# An option that needs a value says so.
if ./ld -m elf_i386 -shared -o x.so lib.o -soname > err 2>&1; then echo "FAIL -soname with nothing after it: linked"; fail=1
elif grep -q "needs a value" err; then echo "ok   -soname with nothing after it"
else echo "FAIL -soname with nothing after it: $(head -1 err)"; fail=1; fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
