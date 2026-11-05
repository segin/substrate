#!/bin/sh
# ld -r makes one section symbol for each output section, and copies none
# of the inputs'.
#
# The in-tree assembler gives a section symbol its section's name, as
# GNU as does not, and the link plan took such a symbol for any local of
# a name and copied it, at its input section's place: Linux's vmlinux.o
# had 3071 section symbols for .text.  objtool, adding relocations to
# vmlinux.o against the section symbol of .text, took the last of them
# for it, at 0x14c65e0, with addends from the start of .text, and every
# entry of the kernel's static call table pointed 20 MiB past its call:
# the kernel stopped on a BUG() before its console was up.  GNU ld makes
# its own section symbols and drops the inputs', however named.
#
# GNU ld is the judge: ld -r of objects of the in-tree assembler, x86-64
# and i386, has GNU ld's section symbols (one a section, each at 0), and
# the -r output, linked into a program by GNU ld, is GNU ld's -r
# output's, byte for byte.
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
${CC:-cc} -O0 -w -o "$work/as" -I"$top/usr.bin/as" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c -lm || {
    echo "FAIL: the assembler does not build for the host"; exit 1; }

cd "$work" || exit 1
# (a temporary label is reached through its section's symbol)
cat > a.s <<'EOF'
	.text
	.globl _start
_start:	call .La
	ret
.La:	ret
	.data
	.long 0
	.long .La
	.section .sites,"a"
	.long .La - .
EOF
cat > b.s <<'EOF'
	.text
	nop
	.globl bfunc
bfunc:	call .Lb
	ret
.Lb:	ret
	.data
	.long 0, 0, 0
	.long .Lb
	.section .sites,"a"
	.long .Lb - .
EOF

# the section symbols of $1: each one's section's name and value
section_symbols() {
    readelf -SW "$1" | awk '/^ *\[ *[0-9]+\]/ { sub(/^ *\[ */, ""); sub(/\]/, ""); print $1, $2 }' > "$1.secs"
    readelf -sW "$1" | awk 'NR > 3 && $4 == "SECTION" { print $7, $2 }' |
        awk 'NR == FNR { sec[$1] = $2; next } { print sec[$1], $2 }' "$1.secs" - | sort
}
bytes_of() { objcopy -O binary -j "$2" "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }

for m in 64 32; do
    emu=elf_x86_64
    [ "$m" = 32 ] && emu=elf_i386
    ./as "--$m" -o a.o a.s && ./as "--$m" -o b.o b.s || { echo "FAIL --$m: the in-tree as"; fail=1; continue; }
    ${GNU_LD:-ld} -m $emu -r -o gnu.o a.o b.o 2>gnu.err || { echo "FAIL: GNU ld -r:"; cat gnu.err; exit 1; }
    if ! ./ld -m $emu -r -o our.o a.o b.o 2>our.err; then
        echo "FAIL --$m: not linked:"; cat our.err; fail=1; continue
    fi
    # (GNU ld makes one for every section, the linker one for each a
    # relocation names: what each has is to be one of GNU ld's)
    section_symbols gnu.o > gnu.ss
    section_symbols our.o > our.ss
    if [ -n "$(uniq -d our.ss)" ] || [ -n "$(comm -23 our.ss gnu.ss)" ]; then
        echo "FAIL --$m: section symbols GNU ld does not make:"; comm -23 our.ss gnu.ss | head -12; fail=1
    fi
    if ! grep -q '^\.text ' our.ss; then
        echo "FAIL --$m: no section symbol of .text"; fail=1
    fi
    ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -o gnu gnu.o 2>/dev/null &&
        ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -o our our.o 2>/dev/null ||
        { echo "FAIL --$m: GNU ld does not link the -r outputs"; fail=1; continue; }
    for s in .text .data .sites; do
        if [ "$(bytes_of our $s)" != "$(bytes_of gnu $s)" ]; then
            echo "FAIL --$m: $s is not GNU ld's: $(bytes_of our $s), GNU ld $(bytes_of gnu $s)"; fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
