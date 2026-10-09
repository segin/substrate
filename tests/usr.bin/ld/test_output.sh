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
    -I"$top/usr.lib/elfobj/src" "$top/usr.bin/ld/ld.c" "$top"/usr.lib/elfobj/src/*.c || {
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

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
