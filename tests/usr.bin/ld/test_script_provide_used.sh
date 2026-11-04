#!/bin/sh
# A PROVIDE whose symbol the script itself uses defines it.
#
# GNU ld takes a symbol that an expression of the script names as one
# referred to, so a PROVIDE of it that no input defines defines it, in an
# output section's body or outside one:
#     .sig : { PROVIDE(p = ABSOLUTE(.)); PROVIDE(q = 7); x = p + q; }
#     PROVIDE(r = x - 1); s = r;
# defines p, q and r as well as x and s.  So does a use in an ASSERT or
# a data statement, and a use by another PROVIDE that is itself used;
# DEFINED(sym) is not a use, nor is a PROVIDE that provides nothing.  The
# linker defined a PROVIDE's symbol only for an input's reference, so the
# symbols were missing from the output.
#
# GNU ld is the judge: each symbol's value and section, or that it is
# absolute.
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
if ! ${GNU_AS:-as} --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as makes the objects"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
# the symbols of $1: name, value, and the section's name or ABS
symbols() {
    readelf -SW "$1" | awk '/^ *\[ *[0-9]+\]/ { sub(/^ *\[ */, ""); sub(/\]/, ""); print $1, $2 }' > "$1.secs"
    readelf -sW "$1" | awk 'NR > 3 && $5 == "GLOBAL" { print $8, $2, $7 }' |
        awk 'NR == FNR { sec[$1] = $2; next } { print $1, $2, ($3 == "ABS" ? "ABS" : sec[$3]) }' "$1.secs" - |
        grep -v '^_start ' | sort
}

# what stands before .sig, its body, what stands after it
cat > cases <<'EOF'
|PROVIDE(p = ABSOLUTE(.)); PROVIDE(q = 7); x = p + q;|PROVIDE(r = x - 1); s = r;
|x = p2;|PROVIDE(p2 = 0x40);
|PROVIDE(c1 = c2); PROVIDE(c2 = 5);|
|PROVIDE(c1 = c2); PROVIDE(c2 = 5); y = c1;|
|PROVIDE(d1 = 5); y = DEFINED(d1) ? 1 : 2;|
|PROVIDE(e1 = 5); ASSERT(e1 == 5, "e");|
|PROVIDE(f1 = 5); LONG(f1)|
PROVIDE(g1 = 0x10);|x = g1;|PROVIDE(h1 = 3); y = h1 + 1;
|setup_size = ALIGN(ABSOLUTE(.), 4096);|PROVIDE(ext = setup_size - 0x1000); PROVIDE(unused = 1);
EOF

printf '.section .data\n.fill 5000,1,0\n.long ext\n.bss\n.zero 4\n.globl _start\n.text\n_start: ret\n' > a.s
${GNU_AS:-as} --32 -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }
n=0
while IFS='|' read -r pre body post; do
    n=$((n + 1))
    printf 'SECTIONS { . = 0; .text : { *(.text) } .data : { *(.data) } %s .sig : { LONG(0x5a5aaa55) %s } %s }\n' \
        "$pre" "$body" "$post" > s.lds
    # every case but the last leaves ext to the command line
    def="--defsym=ext=0"
    case "$post" in *"PROVIDE(ext"*) def= ;; esac
    rm -f gnu our
    ${GNU_LD:-ld} -m elf_i386 --no-warn-rwx-segments $def -T s.lds -o gnu a.o 2>gnu.err ||
        { echo "FAIL: GNU ld did not link case $n:"; cat gnu.err; cat s.lds; exit 1; }
    if ! ./ld -m elf_i386 --no-warn-rwx-segments $def -T s.lds -o our a.o 2>our.err; then
        echo "FAIL: case $n ($body): not linked:"; cat our.err; fail=1; continue
    fi
    symbols gnu > gnu.sym
    symbols our > our.sym
    if ! cmp -s gnu.sym our.sym; then
        echo "FAIL: case $n ($pre | $body | $post): the symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
    fi
done < cases

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
