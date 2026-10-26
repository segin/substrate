#!/bin/sh
# A symbol a script sets to a plain number between output sections.
#
# GNU ld takes a number there as an absolute address: `seg = 0;` is
# SHN_ABS.  The linker put any value that fell inside a section into that
# section, and 0 is where .text starts.  The Linux kernel's real-mode
# trampoline says `real_mode_seg = 0;` and its relocs tool records a
# 16-bit relocation against the symbol only if it is SHN_ABS: with it in
# .text the table listed none, the segment of the trampoline's far jump
# stayed 0, and a second CPU never came up.
#
# GNU ld is the judge of which symbols are absolute; one set to the
# location counter stays with its section.
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
printf '\t.text\n\t.globl _start\n_start:\tnop\n\t.word seg_a\n\t.data\n\t.long 1\n' > a.s
cat > a.lds <<'EOF'
SECTIONS {
  . = 0;
  seg_a = 0;
  num_b = 0x100;
  sum_c = 2 + 3 * 4;
  .text : { *(.text) }
  dot_d = .;
  .data : { *(.data) }
}
EOF

# name value ABS-or-section, for the symbols the script sets.
syms() {
    readelf -sW "$1" | awk '$8 ~ /^(seg_a|num_b|sum_c|dot_d)$/ {
        print $8, $2, ($7 == "ABS" ? "ABS" : "section") }' | sort
}

for mode in 32 64; do
    emul=elf_i386
    [ "$mode" = 64 ] && emul=elf_x86_64
    if ! as --$mode -o a.o a.s 2> err || ! ld -m $emul -T a.lds -o want.elf a.o 2> err; then
        echo "skip: the host's as or ld does not handle --$mode"; continue
    fi
    if ! ./ld -m $emul -T a.lds -o got.elf a.o > out 2>&1; then
        echo "FAIL --$mode: refused: $(head -1 out)"; fail=1; continue
    fi
    syms want.elf > want
    syms got.elf > got
    if ! cmp -s got want; then
        echo "FAIL --$mode: the symbols differ from GNU ld's:"
        diff got want | head -8
        fail=1
    fi
done

[ "$fail" -eq 0 ] && echo "ok: a script's numbers between sections are absolute"
exit "$fail"
