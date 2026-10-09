#!/bin/sh
# Where the output goes and what it is left as: its mode, what happens to
# a name that is already something other than a plain file, and where a
# program with no _start begins.  The linker is built for the host out of
# the tree and links a freestanding 32-bit object.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
printf 'int second(void) { return 2; }\nvoid _start(void) { second(); for (;;) { } }\n' > s.c
printf 'int zebra(void) { return 1; }\nint apple(void) { return zebra(); }\n' > n.c
cc32="${CC:-cc} -m32 -c -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables"
$cc32 -o s.o s.c && $cc32 -o n.o n.c || { echo "SKIP: no 32-bit compiler"; exit 0; }

is() {    # WHAT GOT WANT
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi
}

# The mode is a new file's, less the umask.
(umask 027; ./ld -m elf_i386 -o p027 s.o)
(umask 022; ./ld -m elf_i386 -o p022 s.o; ./ld -m elf_i386 -r -o r022.o s.o)
is "a program under umask 027"        "$(stat -c %a p027)" 750
is "a program under umask 022"        "$(stat -c %a p022)" 755
is "a relocatable file is not executable" "$(stat -c %a r022.o)" 644

# A name that is not a plain file is written into, not replaced.
echo old > target; ln -s target link
./ld -m elf_i386 -o link s.o
is "a link stays a link"              "$(stat -c %F link)" "symbolic link"
is "and what it points at is the program" "$(cmp -s target p022 && echo same)" same
is "which is executable"              "$(stat -c %a target)" 755
mkfifo pipe
(timeout 20 cat pipe > got &)
timeout 20 ./ld -m elf_i386 -o pipe s.o
is "a pipe: the link finishes"        "$?" 0
sleep 1
is "it stays a pipe"                  "$(stat -c %F pipe)" fifo
is "and the reader has the program"   "$(cmp -s got p022 && echo same)" same
./ld -m elf_i386 -o /dev/null s.o
is "/dev/null stays the null device"  "$(stat -c %F /dev/null)" "character special file"

# With no _start the program begins where its text does, and says so.
./ld -m elf_i386 -o noentry n.o 2> err
is "no _start: a warning"             "$(grep -c 'cannot find entry symbol _start' err)" 1
is "and the entry is the start of .text" \
   "$(readelf -h noentry | sed -n 's/.*Entry point address: *0x//p')" \
   "$(readelf -SW noentry | sed -n 's/.* \.text  *PROGBITS  *0*\([0-9a-f]*\) .*/\1/p')"
if ./ld -m elf_i386 -e nowhere -o bad n.o 2> err; then echo "FAIL an entry that is not defined: linked"; fail=1
else is "an entry that is not defined is an error" "$(grep -c "entry symbol 'nowhere' not found" err)" 1; fi

# Options that are words of their own, or joined to their value.
./ld -m elf_i386 -ojoined s.o
is "-oFILE"                           "$(cmp -s joined p022 && echo same)" same
./ld -m elf_i386 -Ttext-segment=0x400000 -o based s.o
is "-Ttext-segment is where the image begins" "$(readelf -lW based | awk '$1 == "LOAD" { print $3; exit }')" 0x00400000
if ./ld -m elf_i386 -Ttext 0x100000 -o no s.o 2> err; then echo "FAIL -Ttext: linked"; fail=1
else is "-Ttext is not -T and a script called text" "$(grep -c -- '-Ttext is not supported' err)" 1; fi
$cc32 -g -o g.o s.c
./ld -m elf_i386 -o withdebug g.o; ./ld -m elf_i386 -S -o nodebug g.o; ./ld -m elf_i386 -q -o withrel g.o
is "debugging information by default" "$(readelf -SW withdebug | grep -c '\.debug_info')" 1
is "-S leaves it out"                 "$(readelf -SW nodebug | grep -c '\.debug')" 0
is "no relocation sections in a program" "$(readelf -SW withdebug | grep -c ' \.rel\.')" 0
is "-q keeps them"                    "$(readelf -SW withrel | grep -c ' \.rel\.text')" 1

# With no script, a function's own section is part of .text, and the same
# for the data, the constants and the exception tables; strings and
# constants gathered into .rodata with other things make plain bytes.  A
# relocatable output keeps every section as it came.
printf 'const char *s(void) { return "a string"; }\ndouble d(void) { return 2.5; }\nint v = 3; static int z;\nint f(void) { return v + z; }\nvoid _start(void) { f(); s(); d(); for (;;) { } }\n' > fold.c
$cc32 -O1 -ffunction-sections -fdata-sections -o fold.o fold.c
./ld -m elf_i386 -o folded fold.o; ./ld -m elf_i386 -r -o unfolded.o fold.o
is "no .text.NAME left"               "$(readelf -SW folded | grep -c ' \.text\.')" 0
is "nor .rodata.NAME, .data.NAME, .bss.NAME" "$(readelf -SW folded | grep -c -E ' \.(rodata|data|bss)\.')" 0
is "the functions are in .text"       "$(nm folded | awk '$3 == "f" || $3 == "s" || $3 == "d" { print $2 }' | sort -u)" T
is ".rodata is plain bytes"           "$(readelf -SW folded | sed -n 's/.* \.rodata  *PROGBITS  *[0-9a-f]* [0-9a-f]* [0-9a-f]* \([0-9a-f]*\)  *\([A-Z]*\) .*/\1 \2/p')" "00 A"
is "no section is in a group"         "$(readelf -SW folded | grep -c -E ' [WAXMSILOTCE]*G[WAXMSILOTCE]* +[0-9]+ +[0-9]+ +[0-9]+$')" 0
is "-r keeps them apart"              "$(( $(readelf -SW unfolded.o | grep -c ' \.text\.') > 0 ))" 1

# --gc-sections leaves out what nothing uses: decided of the inputs'
# sections, from the entry, what is exported, what runs unasked and what a
# script says to keep.
cat > gc.c <<'EOF'
int used_value = 3;
int unused_value = 99;
static int __attribute__((noinline)) helper(int x) { return x * 2; }
int __attribute__((noinline)) used(int x) { return helper(x) + used_value; }
int unused_one(int x) { return x + unused_value; }
int unused_two(int x) { return unused_one(x) + 1; }
static void at_start(void) __attribute__((constructor, used));
static void at_start(void) { used_value++; }
int walked __attribute__((section("walked_set"))) = 1;
int kept_by_script __attribute__((section(".keepme"))) = 2;
volatile int sink;
void _start(void) { sink = used(1); for (;;) { } }
EOF
$cc32 -O1 -ffunction-sections -fdata-sections -o gc.o gc.c
./ld -m elf_i386 -o nogc gc.o
./ld -m elf_i386 --gc-sections --print-gc-sections -o gced gc.o 2> gc.err
has() { nm "$1" | awk -v s="$2" '$3 == s { n = 1 } END { print n + 0 }'; }
is "--gc-sections: what is called stays"   "$(has gced used)$(has gced helper)$(has gced used_value)" 111
is "what nothing uses goes"                "$(has gced unused_one)$(has gced unused_two)$(has gced unused_value)" 000
is "it was there without the option"       "$(has nogc unused_one)$(has nogc unused_value)" 11
is "a constructor stays"                   "$(has gced at_start)" 1
is "a section that can be walked by name stays" "$(has gced walked)" 1
is "nothing asked for .keepme"             "$(has gced kept_by_script)" 0
is "--print-gc-sections names what went"   "$(grep -c 'removing \.text\.unused_one in gc\.o' gc.err)" 1
printf 'SECTIONS { .text : { *(.text .text.*) } .data : { *(.data .data.*) KEEP(*(.keepme)) *(walked_set) } }\n' > gc.lds
./ld -m elf_i386 --gc-sections -T gc.lds -o gcscript gc.o
is "a script's KEEP keeps"                 "$(has gcscript kept_by_script)$(has gcscript unused_one)" 10
$cc32 -fPIC -O1 -ffunction-sections -fdata-sections -o gcpic.o gc.c
./ld -m elf_i386 -shared --gc-sections -o gc.so gcpic.o
is "a shared object keeps what it exports" "$(has gc.so unused_one)$(has gc.so unused_value)" 11
./ld -m elf_i386 -r --gc-sections -o gcrel.o gc.o
is "a relocatable output keeps everything" "$(has gcrel.o unused_one)" 1

# Which definition a name gets does not depend on the order things are
# met in, beyond first-come among equals: a weak definition serves a
# reference in a later object as well as in an earlier one, a strong one
# displaces a weak one whichever comes first, and two strong ones in
# different objects are an error that names both.
printf '__attribute__((weak)) int pick(void) { return 1; }\n' > weakdef.c
printf 'int pick(void) { return 2; }\n' > strongdef.c
printf 'int pick(void);\nvolatile int sunk;\nvoid _start(void) { sunk = pick(); for (;;) { } }\n' > caller.c
$cc32 -o weakdef.o weakdef.c && $cc32 -o strongdef.o strongdef.c && $cc32 -o caller.o caller.c
retval() {    # the constant the linked pick() returns: b8 NN 00 00 00
    objdump -d "$1" | sed -n '/<pick>:/,/ret/p' | sed -n 's/.*mov  *\$0x\([0-9a-f]*\),%eax.*/\1/p' | head -1
}
./ld -m elf_i386 -o wd1 weakdef.o caller.o 2> err
is "a weak definition before the reference to it" "$? $(retval wd1)" "0 1"
./ld -m elf_i386 -o wd2 caller.o weakdef.o 2> err
is "and after it"                     "$? $(retval wd2)" "0 1"
./ld -m elf_i386 -o wd3 weakdef.o caller.o strongdef.o 2> err
is "a strong definition after a weak one wins" "$? $(retval wd3)" "0 2"
./ld -m elf_i386 -o wd4 strongdef.o caller.o weakdef.o 2> err
is "and before it"                    "$? $(retval wd4)" "0 2"
cp strongdef.o strongdef2.o
if ./ld -m elf_i386 -o wd5 strongdef.o caller.o strongdef2.o 2> err; then echo "FAIL two strong definitions: linked"; fail=1
else is "two strong definitions are an error naming both" \
        "$(grep -c 'duplicate strong definition of .pick.: strongdef.o and strongdef2.o' err)" 1; fi
if ./ld -m elf_i386 -o wd6 caller.o 2> err; then echo "FAIL no definition: linked"; fail=1
else is "no definition names who wanted it" "$(grep -c 'undefined reference to .pick. (referenced by caller.o)' err)" 1; fi

# The map and the --reproduce bundle are for later: a map that could not
# be written is an error, and the bundle's script makes the same output.
${CC:-cc} -m32 -c -ffreestanding -fPIE -fno-asynchronous-unwind-tables -o spie_early.o s.c
./ld -m elf_i386 -e second -Map s.map --reproduce bundle -o mapped s.o
is "a map file is written"            "$(grep -c '^Symbols:' s.map)" 1
if [ -c /dev/full ]; then
    if ./ld -m elf_i386 -Map /dev/full -o nomap s.o 2> err; then echo "FAIL a map that cannot be written: linked"; fail=1
    else is "a map that cannot be written is an error" "$(grep -c 'failed to write the map file /dev/full' err)" 1; fi
fi
is "the bundle's script quotes what it was given" "$(grep -c -- "-e 'second' " bundle/repro.sh)" 1
(LD_TOOL="$work/ld" sh bundle/repro.sh) > /dev/null 2>&1
is "and run, it makes the same output" "$(cmp -s bundle/repro.out mapped && echo same)" same
./ld -m elf_i386 -pie -e second --reproduce bundlepie -o mappedpie spie_early.o 2> /dev/null
is "a PIE is reproduced as a PIE"     "$(grep -c -- ' -pie ' bundlepie/repro.sh)" 1

# sym@SIZE is how big the thing is, and not where it is.
cat > size.s <<'EOF'
        .data
        .globl table, table_size
table:  .long 1, 2, 3, 4, 5
        .size table, .-table
table_size:
        .long table@SIZE
        .long table@SIZE-4
        .text
        .globl _start
_start: jmp _start
EOF
${CC:-cc} -m32 -c -o size.o size.s && ./ld -m elf_i386 -o sized size.o
soff=$(( 0x$(readelf -SW sized | sed -n 's/.* \.data  *PROGBITS  *[0-9a-f]* \([0-9a-f]*\) .*/\1/p') + 20 ))
is "sym@SIZE is the symbol's size"    "$(od -An -tu4 -j"$soff" -N8 sized | tr -s ' ' | sed 's/^ //')" "20 16"

# Nothing to load is no segment; and a section aligned to more than a page
# is at an address so aligned and at the place in the file that its
# segment maps there.
printf 'void _start(void) { for (;;) { } }\n' > nodata.c
printf 'int wide __attribute__((aligned(65536))) = 7;\nchar narrow = 1;\nvoid _start(void) { wide += narrow; for (;;) { } }\n' > wide.c
$cc32 -o nodata.o nodata.c && $cc32 -o wide.o wide.c
./ld -m elf_i386 -o nodata nodata.o; ./ld -m elf_i386 -o wideout wide.o
is "no PT_LOAD of nothing"            "$(readelf -lW nodata | awk '$1 == "LOAD" && $5 == "0x00000" && $6 == "0x00000"' | wc -l)" 0
wa=0x$(nm wideout | awk '$3 == "wide" { print $1 }')
is "aligned to 64K, as asked"         "$(( wa % 65536 ))" 0
set -- $(readelf -lW wideout | awk -v a="$wa" '$1 == "LOAD" { print $2, $3, $5 }' | while read o v f; do
    [ $(( wa >= v && wa < v + f )) -eq 1 ] && echo "$o $v"; done)
is "and its initial value is where its segment maps it" \
   "$(od -An -tu4 -j"$(( $1 + wa - $2 ))" -N4 wideout | tr -d ' ')" 7

# --eh-frame-hdr: a table of every function that has an unwinding record,
# in order of address, and a program header that points to it.
printf 'int a(int x) { return x + 1; }\nint b(int x) { return a(x) * 2; }\nint c(int x) { return b(x) - 3; }\nvoid _start(void) { c(1); for (;;) { } }\n' > e.c
${CC:-cc} -m32 -c -ffreestanding -fno-pic -fno-pie -fasynchronous-unwind-tables -o e.o e.c
./ld -m elf_i386 --eh-frame-hdr -o eh e.o
./ld -m elf_i386 -o noeh e.o
is "PT_GNU_EH_FRAME"                  "$(readelf -lW eh | grep -c GNU_EH_FRAME)" 1
is "not without the option"           "$(readelf -lW noeh | grep -c GNU_EH_FRAME)" 0
hoff=0x$(readelf -SW eh | sed -n 's/.* \.eh_frame_hdr  *PROGBITS  *[0-9a-f]* \([0-9a-f]*\) .*/\1/p')
hadr=0x$(readelf -SW eh | sed -n 's/.* \.eh_frame_hdr  *PROGBITS  *\([0-9a-f]*\) .*/\1/p')
set -- $(od -An -v -tu4 -j"$(( hoff ))" -N44 eh)
is "version 1, addresses relative, a table" "$(od -An -v -tx1 -j"$(( hoff ))" -N4 eh | tr -d ' ')" 011b033b
is "one entry for each function"      "$3" 4
is "the same as the unwinding records there are" "$3" "$(readelf -wf eh 2>/dev/null | grep -c ' FDE ')"
first=$(( (hadr + $4) & 0xffffffff )); second=$(( (hadr + $6) & 0xffffffff ))
third=$(( (hadr + $8) & 0xffffffff )); fourth=$(( (hadr + ${10}) & 0xffffffff ))
is "in order of address"              "$(( first < second && second < third && third < fourth ))" 1
is "the first is the first function"  "$(printf '%08x' "$first")" "$(nm eh | awk '$3 == "a" { print $1 }')"
is "the last is the last"             "$(printf '%08x' "$fourth")" "$(nm eh | awk '$3 == "_start" { print $1 }')"

# A PIE is a program that happens to be ET_DYN; a shared object is a
# library; -static says which libraries to use and not which to make.
# Everything made is branded for substrate.
printf 'int missing(void);\nvoid _start(void) { missing(); for (;;) { } }\n' > u.c
${CC:-cc} -m32 -c -ffreestanding -fPIE -fno-asynchronous-unwind-tables -o spie.o s.c
${CC:-cc} -m32 -c -ffreestanding -fPIE -fno-asynchronous-unwind-tables -o u.o u.c
./ld -m elf_i386 -pie -o pie spie.o; ./ld -m elf_i386 -shared -o lib.so spie.o
is "a PIE is ET_DYN"                  "$(readelf -h pie | sed -n 's/.*Type: *\([A-Z]*\).*/\1/p')" DYN
is "marked as a program"              "$(readelf -d pie | grep -c 'PIE')" 1
is "executable"                       "$(stat -c %a pie)" 755
is "with an entry"                    "$(readelf -h pie | sed -n 's/.*Entry point address: *//p' | grep -c -v '^0x0$')" 1
is "a shared object is not executable" "$(stat -c %a lib.so)" 644
is "nor marked as a program"          "$(readelf -d lib.so | grep -c 'PIE')" 0
if ./ld -m elf_i386 -pie -o upie u.o 2> err; then echo "FAIL a PIE with something undefined: linked"; fail=1
else is "a PIE with something undefined is an error" "$(grep -c 'undefined reference to .missing' err)" 1; fi
./ld -m elf_i386 -shared -o ulib.so u.o
is "a shared object with something undefined is not" "$?" 0
./ld -m elf_i386 -static -shared -o slib.so spie.o
is "-static -shared is still a shared object" "$(readelf -h slib.so | sed -n 's/.*Type: *\([A-Z]*\).*/\1/p')" DYN
for f in p022 pie lib.so; do
    is "$f is branded for substrate" "$(od -An -tu1 -j7 -N1 "$f" | tr -d ' ')" 64
done
is "a relocatable file is not branded" "$(od -An -tu1 -j7 -N1 r022.o | tr -d ' ')" 0

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
