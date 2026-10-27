#!/bin/sh
# Which section a symbol a script sets between output sections is in.
#
# GNU ld works it out from the expression: a symbol or ADDR() is an
# address in its section, a literal or "." an absolute one, SIZEOF() a
# plain number.  Of two operands that differ, a number takes the other's
# section; otherwise the result is absolute, so `sym = other + 1;` is
# SHN_ABS while `sym = other + SIZEOF(.text);` is in other's section.
# What came from "." alone goes to the section by the location counter:
# the one before it, or the next one if "." was set since (but not past
# "_end").  The linker put every such symbol in the section its value
# fell in, or made it absolute if none: `seg = 0;` went in .text, and the
# Linux kernel's real-mode trampoline says `real_mode_seg = 0;`, which
# its relocs tool records a 16-bit relocation against only if it is
# SHN_ABS -- with none, a second CPU never came up.
#
# GNU ld is the judge of each symbol's value and section.  The objects
# carry no .note.gnu.property, which the two linkers place differently
# when no script names it.
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
cat > a.s <<'EOF'
	.text
	.globl _start
_start:	nop
	nop
	.word seg_a
	.data
	.globl dvar
dvar:	.long 1
	.bss
	.zero 8
EOF
cat > a.lds <<'EOF'
SECTIONS {
  . = 0x1000;
  p_first = .;
  seg_a = 0;
  num_b = 0x100;
  sum_c = 2 + 3 * 4;
  .text : { *(.text) }
  d_text = .;
  a_sym = _start;
  a_paren = (_start);
  b_sym1 = _start + 1;
  b_sym2 = 1 + _start;
  b_sym3 = _start & ~0xf;
  b_sym4 = _start + SIZEOF(.text);
  c_dot1 = . + 1;
  c_dot2 = . - 1;
  c_dot3 = . & ~0xf;
  c_dot4 = . + SIZEOF(.text);
  c_dot5 = . + _start;
  e_addr = ADDR(.text);
  e_addr1 = ADDR(.text) + 1;
  e_addr2 = ADDR(.text) + SIZEOF(.text) - 1;
  e_load = LOADADDR(.text) + 1;
  f_absdot = ABSOLUTE(.);
  f_abssym = ABSOLUTE(_start);
  g_size = SIZEOF(.text);
  g_cmp = _start > 0;
  h_max = MAX(_start, 0);
  h_align = ALIGN(_start, 4);
  h_align1 = ALIGN(4);
  h_align2 = ALIGN(., 4);
  k_cond = 1 ? _start : 0;
  k_cond1 = DEFINED(_start) ? _start + 1 : 0;
  r_dotsym = d_text;
  r_dotsym2 = d_text + SIZEOF(.text);
  . = ALIGN(0x100);
  n_next = .;
  n_next2 = . + 4;
  .data 0x3000 : { *(.data) }
  m_dvar = dvar + 4;
  m_dvar2 = dvar;
  _end = .;
  . = . + 0x10;
  e_after = .;
  .bss : { *(.bss) }
  z_last = .;
}
EOF

# name value section-or-ABS, for the symbols the script sets.
syms() {
    readelf -SW "$1" | awk '{ if (match($0, /\] *[^ ]+/)) { n = substr($0, RSTART + 1, RLENGTH - 1);
        gsub(/ /, "", n); print substr($0, index($0, "[") + 1, 2) + 0, n } }' > "$1.secs"
    readelf -sW "$1" | awk 'NR == FNR { sec[$1] = $2; next }
        $5 == "GLOBAL" && $8 != "_start" && $8 != "dvar" {
        print $8, $2, ($7 == "ABS" ? "ABS" : sec[$7]) }' "$1.secs" - | sort
}

for mode in 32 64; do
    emul=elf_i386
    [ "$mode" = 64 ] && emul=elf_x86_64
    if ! as --$mode -mx86-used-note=no -o a.o a.s 2> err || ! ld -m $emul -T a.lds -o want.elf a.o 2> err; then
        echo "skip: the host's as or ld does not handle --$mode"; continue
    fi
    if ! ./ld -m $emul -T a.lds -o got.elf a.o > out 2>&1; then
        echo "FAIL --$mode: refused: $(head -1 out)"; fail=1; continue
    fi
    syms want.elf > want
    syms got.elf > got
    if [ "$(wc -l < want)" -ne 39 ]; then
        echo "FAIL --$mode: GNU ld's output has $(wc -l < want) of the 39 symbols"; fail=1
    elif ! cmp -s got want; then
        echo "FAIL --$mode: the symbols differ from GNU ld's:"
        diff got want | head -12
        fail=1
    fi
done

[ "$fail" -eq 0 ] && echo "ok: a script's symbols between sections are in GNU ld's sections"
exit "$fail"
