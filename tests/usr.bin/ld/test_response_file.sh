#!/bin/sh
# @file: the arguments a file holds, in the place of the argument naming it.
#
# The linker took @file for an input of that name and could not open it.
# Linux links each module of more than one object so, `ld -r -o m.o
# @m.mod`, and every such module failed.
#
# GNU ld is the judge: what it links from a response file -- white space
# between arguments, quotes and backslashes keeping it in one, options
# among them, an @file within one, an empty one -- the linker links the
# same; an @name naming no file is an input named so, which neither
# finds; a file naming itself is refused.
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
for n in one two three; do
    printf '\t.text\n\t.globl %s\n%s:\tret\n' "$n" "$n" > $n.s
    ${AS_GNU:-as} --32 -o $n.o $n.s || exit 1
done
cp three.o "with space.o"
cp two.o 'quo"te.o'

# the defined symbols of a relocatable link
syms() {
    nm "$1" | awk '$2 == "T" { print $3 }' | sort | tr '\n' ' '
}
# check NAME CONTENTS ARGS...: the link -r of ARGS, list holding CONTENTS, is GNU ld's (@OUT@ in
# CONTENTS is the output each is to write)
check() {
    what=$1
    content=$2
    shift 2
    printf '%s' "$content" | sed 's/@OUT@/gnu.o/g' > list
    rm -f gnu.o our.o
    if ! ${GNU_LD:-ld} -m elf_i386 -r -o gnu.o "$@" > gnu.said 2>&1; then
        echo "FAIL: $what: GNU ld did not link it: $(head -1 gnu.said)"; fail=1; return
    fi
    printf '%s' "$content" | sed 's/@OUT@/our.o/g' > list
    if ! ./ld -m elf_i386 -r -o our.o "$@" > said 2>&1; then
        echo "FAIL: $what: $(head -1 said)"; fail=1; return
    fi
    if [ "$(syms our.o)" != "$(syms gnu.o)" ]; then
        echo "FAIL: $what: defines [$(syms our.o)], GNU ld's [$(syms gnu.o)]"; fail=1
    fi
}

check "objects, a space and a newline between" 'one.o two.o
three.o
' @list
check "a quoted name, a backslash" '"with space.o" quo\"te.o' @list
check "single quotes" "'with space.o' one.o" @list
check "an escaped space" "one.o with\\ space.o" @list
check "an option in it" '-o
@OUT@ one.o' @list
printf 'two.o three.o\n' > inner
check "an @file within one" 'one.o @inner' @list
check "an empty one" '' one.o @list
check "after other arguments" 'two.o' one.o @list three.o

# an @name naming no file is an input of that name, which neither finds
if ${GNU_LD:-ld} -m elf_i386 -r -o gnu.o one.o @nosuch > /dev/null 2>&1; then
    echo "note: GNU ld linked @nosuch"
fi
if ./ld -m elf_i386 -r -o our.o one.o @nosuch > said 2>&1; then
    echo "FAIL: @nosuch was linked"; fail=1
elif ! grep -q '@nosuch' said; then
    echo "FAIL: @nosuch refused without naming it: $(head -1 said)"; fail=1
fi

# a file that names itself is refused, as GNU ld refuses it
printf '@loop\n' > loop
if ${GNU_LD:-ld} -m elf_i386 -r -o gnu.o one.o @loop > /dev/null 2>&1; then
    echo "FAIL: GNU ld linked a response file naming itself"; fail=1
fi
if ./ld -m elf_i386 -r -o our.o one.o @loop > said 2>&1; then
    echo "FAIL: a response file naming itself was linked"; fail=1
elif ! grep -q 'too many @-files' said; then
    echo "FAIL: a response file naming itself: $(head -1 said)"; fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: @file"
exit "$fail"
