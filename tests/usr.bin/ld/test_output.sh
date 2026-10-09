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
