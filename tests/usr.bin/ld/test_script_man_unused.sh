#!/bin/sh
# ld(1)'s list of linker-script commands read and not acted on, against
# the linker.
#
# The LINKER SCRIPTS section of ld(1) ends with the commands the linker
# reads and does nothing with, and those it refuses.  The list went
# stale: it still named VERSION, an output section's AT(lma), and the
# data statements BYTE, SHORT, LONG and QUAD, refused outright -- when
# the linker had come to version symbols from a script's VERSION { },
# load a section where its AT() says, and write the data statements, as
# the same page says further up.  A reader of the list would work around
# what is there.
#
# Each is tried here with GNU ld as the judge of what doing it looks
# like: a shared object linked through a script's VERSION has GNU ld's
# versioned dynamic symbols, an output section's AT() gives GNU ld's
# load addresses, and BYTE, SHORT, LONG, QUAD and SQUAD give GNU ld's
# bytes.  The list must then name none of them; the only AT it may name
# is a PHDRS entry's, and the AT>region form, which are read over.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
man="$top/usr.man/man1/ld.1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GNU_LD:-ld} --version 2>/dev/null | grep -q 'GNU ld'; then
    echo "skip: GNU ld is the judge"
    exit 0
fi
if ! ${GNU_AS:-as} --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as makes the objects"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1

# link NAME SCRIPT [ld options]: gnu.NAME with GNU ld, ours.NAME with ours
link() {
    n=$1 s=$2
    shift 2
    "${GNU_LD:-ld}" -m elf_x86_64 "$@" -T "$s" -o "gnu.$n" a.o > gsaid 2>&1 || {
        echo "FAIL: $n: GNU ld refused the link: $(head -1 gsaid)"; fail=1; return 1; }
    ./ld -m elf_x86_64 "$@" -T "$s" -o "ours.$n" a.o > said 2>&1 || {
        echo "FAIL: $n: ld refused the link: $(head -1 said)"; fail=1; return 1; }
}
# same NAME WHAT: WHAT (a function of a file) of both outputs is alike
same() {
    $2 "gnu.$1" > want
    $2 "ours.$1" > have
    if [ ! -s want ]; then
        echo "FAIL: $1: GNU ld's output shows no $2"; fail=1
    elif cmp -s want have; then
        echo "ok   $1: $2 is GNU ld's"
    else
        echo "FAIL: $1: $2 differs from GNU ld's:"; diff want have | sed 's/^/    /'; fail=1
    fi
}

printf '.text\n.globl _start, f, g\n_start: ret\nf: ret\ng: ret\n.data\n.byte 0x55\n' > a.s
${GNU_AS:-as} --64 -o a.o a.s || exit 1

# VERSION { } in a script versions the dynamic symbols
cat > version.lds <<'EOF'
SECTIONS { .text : { *(.text) } .data : { *(.data) } }
VERSION { V_1 { global: f; local: *; }; V_2 { global: g; } V_1; }
EOF
dynsyms() {
    readelf --dyn-syms -W "$1" | awk 'NR > 3 && $8 != "" { print $5, $8 }' | sort
}
link version version.lds -shared && same version dynsyms

# an output section's AT(lma) is where it is loaded
cat > at.lds <<'EOF'
SECTIONS { . = 0x400000; .text : AT(0x800000) { *(.text) } .data : { *(.data) } }
EOF
lmas() {
    objdump -h "$1" | awk '$2 == ".text" { print $2, $4, $5 }'
}
link at at.lds && same at lmas

# the data statements write their values
cat > data.lds <<'EOF'
SECTIONS { . = 0x400000; .text : { *(.text) } .data : { *(.data) BYTE(1) SHORT(0x203) LONG(0x4050607) QUAD(0x8090a0b0c0d0e0f) SQUAD(-2) } }
EOF
databytes() {
    objcopy -O binary --only-section=.data "$1" "$1.bin" 2>/dev/null && od -An -tx1 -v "$1.bin" | tr -d ' \n'
}
link data data.lds && same data databytes

# the man page's list of what is read and not acted on (its first
# sentence), and the rest of that paragraph
awk '/^Read and not acted on:/ { t = 1 } t && /^\.Sh / { exit } t { print }' "$man" > para
awk '{ print } / \.$/ { exit }' para > list
if [ ! -s list ]; then
    echo "FAIL: ld(1) has no list of script commands read and not acted on"; exit 1
fi
for w in VERSION BYTE SHORT LONG QUAD SQUAD; do
    if grep -q "^\.Ic $w\( \|$\)" list; then
        echo "FAIL: ld(1) lists $w as read and not acted on, or refused"; fail=1
    else
        echo "ok   ld(1) does not list $w as not acted on"
    fi
done
if grep -q '^Refused' para; then
    echo "FAIL: ld(1) still says data statements are refused:"; sed -n '/^Refused/,$p' para | sed 's/^/    /'; fail=1
fi
if grep 'Ic AT\>' list | grep -qv 'PHDRS\|AT>'; then
    echo "FAIL: ld(1) lists an output section's AT as not acted on:"
    grep 'Ic AT\>' list | sed 's/^/    /'; fail=1
else
    echo "ok   ld(1) names only a PHDRS entry's AT and AT>region as not acted on"
fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
