#!/bin/sh
# What the directives about a symbol make of it (AS-T-267 to 273, 276).
#
# - `.size f, e` was taken for `e - f`, and `.size f, e - g` refused
#   unless g was f.  The size is a number or the distance between two
#   labels of one section; a symbol alone is refused.
# - `.type` took a name it did not know for no type and said nothing,
#   and knew neither @gnu_indirect_function nor @gnu_unique_object.
# - A symbol the source does not define, and only names by `.type`,
#   `.size`, `.hidden` or `.local`, was written local and undefined,
#   which is nothing a linker knows.  It is global; one that is only
#   weak and never referred to is not written at all.
# - `.globl .Lx` made no symbol.
# - A name may be set and set again, and a label may take a name that
#   was set; but a name that is a label may not be set, nor one that
#   `.equiv` set, nor may `.equiv` set what is anything yet.  All were
#   taken, and whichever came last was what the name was.  `.equiv`
#   itself was not a symbol at all.
# - `.symver` made version sections of this assembler's own; a version
#   is a symbol named `name@version` or `name@@version`, which the
#   linker reads.
#
# Each case is the bytes of .data, the relocations, and the symbols as
# name=value/size/type/binding/visibility/section.  They are GNU as's,
# and GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is SOURCE BYTES RELOCATIONS SYMBOLS: `|` in SOURCE is a newline
is() {
    cases=$((cases + 1))
    printf '%s\n' "$1" | tr '|' '\n' > t.s
    rm -f t.o
    "$AS" --64 -o t.o t.s > /dev/null 2> err
    if [ ! -f t.o ]; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .data t.o /dev/stdout 2> /dev/null | od -An -v -tx1 | tr -d ' \n')
    relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
             LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
    syms=$(readelf -sSW t.o | awk '
        /^ *\[ *[0-9]+\]/ { x = $0; sub(/^ *\[ */, "", x); split(x, a, /[] ]+/); sec[a[1]] = a[2]; next }
        /^ +[0-9]+: / && $4 != "SECTION" && $4 != "FILE" && $8 != "" {
            printf "%s=%x/%s/%s/%s/%s/%s\n", $8, strtonum("0x" $2), $3, $4, $5, $6, (($7 in sec) ? sec[$7] : $7)
        }' | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
    [ "$bytes" = "$2" ] || { echo "FAIL [$1]: bytes $bytes, not $2"; fail=1; }
    [ "$relocs" = "$3" ] || { echo "FAIL [$1]: relocations [$relocs], not [$3]"; fail=1; }
    [ "$syms" = "$4" ] || { echo "FAIL [$1]: symbols [$syms], not [$4]"; fail=1; }
}

refused() {
    cases=$((cases + 1))
    printf '%s\n' "$1" | tr '|' '\n' > t.s
    rm -f t.o
    "$AS" --64 -o t.o t.s > /dev/null 2> err
    [ ! -f t.o ] || { echo "FAIL [$1]: assembled"; fail=1; }
}

# .size
is '.text|f: nop|nop|e: nop|.size f, e-f' '' '' 'e=2/0/NOTYPE/LOCAL/DEFAULT/.text f=0/2/NOTYPE/LOCAL/DEFAULT/.text'
is '.text|f: nop|nop|e: nop|.size f, e-f+4' '' '' 'e=2/0/NOTYPE/LOCAL/DEFAULT/.text f=0/6/NOTYPE/LOCAL/DEFAULT/.text'
is '.text|f: nop|nop|.size f, .-f' '' '' 'f=0/2/NOTYPE/LOCAL/DEFAULT/.text'
is '.text|f: nop|.size f, 12' '' '' 'f=0/12/NOTYPE/LOCAL/DEFAULT/.text'
is '.text|f: nop|.set fe, . - f|.size f, fe' '' '' 'f=0/1/NOTYPE/LOCAL/DEFAULT/.text fe=1/0/NOTYPE/LOCAL/DEFAULT/ABS'
is '.text|f: nop|nop|e: nop|g: nop|.size f, g-e+8' '' '' 'e=2/0/NOTYPE/LOCAL/DEFAULT/.text f=0/9/NOTYPE/LOCAL/DEFAULT/.text g=3/0/NOTYPE/LOCAL/DEFAULT/.text'
is '.text|f: nop|nop|e: nop|g: nop|.size f, e-g' '' '' 'e=2/0/NOTYPE/LOCAL/DEFAULT/.text f=0/0xffffffffffffffff/NOTYPE/LOCAL/DEFAULT/.text g=3/0/NOTYPE/LOCAL/DEFAULT/.text'
refused '.text|f: nop|nop|e: nop|.size f, e'
refused '.text|f: nop|nop|e: nop|.data|g: .long 0|.text|.size f, e-g'
refused '.text|f: nop|.size f, und'

# .type, in each spelling
is '.text|f: nop|.type f,@function' '' '' 'f=0/0/FUNC/LOCAL/DEFAULT/.text'
is '.text|f: nop|.type f,%function' '' '' 'f=0/0/FUNC/LOCAL/DEFAULT/.text'
is '.text|f: nop|.type f,"function"' '' '' 'f=0/0/FUNC/LOCAL/DEFAULT/.text'
is '.text|f: nop|.type f,STT_FUNC' '' '' 'f=0/0/FUNC/LOCAL/DEFAULT/.text'
is '.data|o: .long 0|.type o,@object' '00000000' '' 'o=0/0/OBJECT/LOCAL/DEFAULT/.data'
is '.data|o: .long 0|.type o,STT_OBJECT' '00000000' '' 'o=0/0/OBJECT/LOCAL/DEFAULT/.data'
is '.data|o: .long 0|.type o,@tls_object' '00000000' '' 'o=0/0/TLS/LOCAL/DEFAULT/.data'
is '.type o,@common|.comm o,4' '' '' 'o=4/4/OBJECT/GLOBAL/DEFAULT/COM'
is '.text|f: nop|.type f,@notype' '' '' 'f=0/0/NOTYPE/LOCAL/DEFAULT/.text'
is '.text|f: nop|.type f,@gnu_indirect_function' '' '' 'f=0/0/IFUNC/LOCAL/DEFAULT/.text'
is '.data|o: .long 0|.type o,@gnu_unique_object' '00000000' '' 'o=0/0/OBJECT/UNIQUE/DEFAULT/.data'
refused '.text|f: nop|.type f,@bogus'
refused '.text|f: nop|.type f'

# What the source names and does not define.
is '.type u1,@function' '' '' 'u1=0/0/FUNC/GLOBAL/DEFAULT/UND'
is '.size u2,4' '' '' 'u2=0/4/NOTYPE/GLOBAL/DEFAULT/UND'
is '.hidden u3' '' '' 'u3=0/0/NOTYPE/GLOBAL/HIDDEN/UND'
is '.globl u5' '' '' 'u5=0/0/NOTYPE/GLOBAL/DEFAULT/UND'
is '.protected u6' '' '' 'u6=0/0/NOTYPE/GLOBAL/PROTECTED/UND'
is '.type u7,@object|.size u7,4|.globl u7' '' '' 'u7=0/4/OBJECT/GLOBAL/DEFAULT/UND'
is '.local u8' '' '' 'u8=0/0/NOTYPE/GLOBAL/DEFAULT/UND'
is '.data|.long l1|.local l1' '00000000' 'R_X86_64_32@0:l1+0' 'l1=0/0/NOTYPE/GLOBAL/DEFAULT/UND'
is '.weak u4' '' '' ''
is '.weak w3|.hidden w3|.type w3,@function' '' '' ''
is '.weak w1|.data|.long w1' '00000000' 'R_X86_64_32@0:w1+0' 'w1=0/0/NOTYPE/WEAK/DEFAULT/UND'
is '.weak w2|.text|call w2' '' 'R_X86_64_PLT32@1:w2-4' 'w2=0/0/NOTYPE/WEAK/DEFAULT/UND'

# A compiler's label is a symbol when it is said to be seen.
is '.text|.globl .Lg|.Lg: nop' '' '' '.Lg=0/0/NOTYPE/GLOBAL/DEFAULT/.text'
is '.text|.weak .Lw|.Lw: nop' '' '' '.Lw=0/0/NOTYPE/WEAK/DEFAULT/.text'
is '.text|.Lplain: nop' '' '' ''

# Set, and set again; and what may not be.
is '.data|x = 6|.long x|x = x + 1|.long x' '0600000007000000' '' 'x=7/0/NOTYPE/LOCAL/DEFAULT/ABS'
is '.set k,5|.set k,k+1|.data|.long k' '06000000' '' 'k=6/0/NOTYPE/LOCAL/DEFAULT/ABS'
is '.equ q,3|.equiv r,q|.data|.long q,r' '0300000003000000' '' 'q=3/0/NOTYPE/LOCAL/DEFAULT/ABS r=3/0/NOTYPE/LOCAL/DEFAULT/ABS'
is '.set q,3|.text|q: nop' '' '' 'q=0/0/NOTYPE/LOCAL/DEFAULT/.text'
refused '.text|q: nop|.set q,3'
refused '.text|q: nop|q = .'
refused '.set q,3|.equiv q,4'
refused '.equiv q,4|.equiv q,4'
refused '.equiv q,4|.set q,5'
refused '.text|q: nop|q: nop'

# Versions.
is '.text|.globl foo|foo: ret|.symver foo,foo@VER1' '' '' 'foo=0/0/NOTYPE/GLOBAL/DEFAULT/.text foo@VER1=0/0/NOTYPE/GLOBAL/DEFAULT/.text'
is '.text|.globl foo|foo: ret|.symver foo,foo@@VER1' '' '' 'foo=0/0/NOTYPE/GLOBAL/DEFAULT/.text foo@@VER1=0/0/NOTYPE/GLOBAL/DEFAULT/.text'
is '.text|.globl foo|foo: ret|.symver foo,foo@@@VER1' '' '' 'foo@@VER1=0/0/NOTYPE/GLOBAL/DEFAULT/.text'
is '.text|.globl foo|foo: ret|.symver foo,foo@VER1|.symver foo,foo@VER2' '' '' 'foo=0/0/NOTYPE/GLOBAL/DEFAULT/.text foo@VER1=0/0/NOTYPE/GLOBAL/DEFAULT/.text foo@VER2=0/0/NOTYPE/GLOBAL/DEFAULT/.text'
is '.text|.globl foo|.type foo,@function|foo: ret|.size foo,.-foo|.symver foo,foo@@VER1|call foo' '' 'R_X86_64_PLT32@2:foo-4' 'foo=0/1/FUNC/GLOBAL/DEFAULT/.text foo@@VER1=0/1/FUNC/GLOBAL/DEFAULT/.text'
is '.text|.globl foo|foo: ret|.symver foo,foo@@@VER1|call foo|.data|.long foo' '00000000' 'R_X86_64_32@0:foo@@VER1+0 R_X86_64_PLT32@2:foo@@VER1-4' 'foo@@VER1=0/0/NOTYPE/GLOBAL/DEFAULT/.text'
is '.symver bar,bar@VER1|.text|call bar' '' 'R_X86_64_PLT32@1:bar@VER1-4' 'bar@VER1=0/0/NOTYPE/GLOBAL/DEFAULT/UND'
is '.symver bar,bar@@@VER1|.text|call bar|.data|.long bar' '00000000' 'R_X86_64_32@0:bar@VER1+0 R_X86_64_PLT32@1:bar@VER1-4' 'bar@VER1=0/0/NOTYPE/GLOBAL/DEFAULT/UND'

# A shared library made of such an object, with a version script, has
# the versions: an old one and the one a plain reference gets.
if command -v cc > /dev/null 2>&1; then
    cases=$((cases + 1))
    cat > lib.s <<'EOF'
	.text
	.globl	old_impl
	.type	old_impl, @function
old_impl:
	movl	$1, %eax
	ret
	.size	old_impl, .-old_impl
	.globl	new_impl
	.type	new_impl, @function
new_impl:
	movl	$2, %eax
	ret
	.size	new_impl, .-new_impl
	.symver	old_impl, thing@VER_1
	.symver	new_impl, thing@@VER_2
	.section	.note.GNU-stack,"",@progbits
EOF
    printf 'VER_1 { global: thing; local: *; };\nVER_2 { global: thing; } VER_1;\n' > lib.map
    if "$AS" --64 -o lib.o lib.s 2> err && cc -shared -nostdlib -Wl,--version-script=lib.map -o lib.so lib.o 2>> err; then
        got=$(readelf --dyn-syms -W lib.so | awk '$8 ~ /^thing@/ { print $8 }' | LC_ALL=C sort | tr '\n' ' ')
        [ "$got" = "thing@@VER_2 thing@VER_1 " ] || { echo "FAIL the library's versions are [$got]"; fail=1; }
    else
        echo "FAIL the library was not built: $(tail -1 err)"; fail=1
    fi
fi

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
