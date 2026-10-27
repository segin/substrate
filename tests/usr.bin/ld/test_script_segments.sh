#!/bin/sh
# Where a script's sections stop being one run of memory, the next
# PT_LOAD begins, as GNU ld begins it.
#
# With no PHDRS the linker made one segment of each run of sections of
# a kind (code, read-only, writable), whatever their addresses.  The
# Substrate kernel puts its boot code at 1 MiB and the rest of its code
# at 3 GiB + 1 MiB, loaded at 1 MiB + 4 KiB: one PT_LOAD of 3 GiB.  A
# multiboot loader puts the command line after the segments it loads,
# and with that one it was inside .bss, which the kernel clears -- the
# kernel had no command line, and no root.
#
# GNU ld begins a segment where a section's load address is another
# distance from its address than the one before's, and where it is more
# than a page past where that one ends; contents after a .bss stay in
# its segment.  GNU ld is the judge: each PT_LOAD's flags, where it
# ends and how far its load address is from its address.  (Where the
# first begins is left out: GNU ld does not load the headers below a
# section that begins a page, and this linker does.)
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
cat > a.s <<'EOF'
	.section .setup, "ax"
	.globl _start
_start:	jmp _start
	.text
	.fill 300, 1, 0x90
	.section .text.far, "ax"
	ret
	.section .rodata, "a"
	.long 1, 2, 3
	.data
	.long 4
	.bss
	.zero 0x3000
	.section .data.after, "aw"
	.long 5
EOF
${AS_GNU:-as} --32 -mx86-used-note=no -o a.o a.s || exit 1

# segs FILE: each PT_LOAD as its flags, its end, and its load address less its address
segs() {
    readelf -lW "$1" | awk '$1 == "LOAD" {
        v = strtonum($3); p = strtonum($4); m = strtonum($6)
        printf "%s end=%#x lma-vma=%#x\n", $7 $8, v + m, (p - v) % 4294967296 }'
}

# same NAME SCRIPT: both linkers link a.o with SCRIPT, to the same segments
same() {
    printf '%s\n' "$2" > "$1.lds"
    if ! ./ld -m elf_i386 -T "$1.lds" -o "$1.ours" a.o > "$1.err" 2>&1; then
        echo "FAIL $1: $(head -1 "$1.err")"; fail=1; return
    fi
    ${GNU_LD:-ld} -m elf_i386 -T "$1.lds" -o "$1.gnu" a.o || { echo "FAIL $1: GNU ld refuses it"; fail=1; return; }
    if [ "$(segs "$1.ours")" != "$(segs "$1.gnu")" ]; then
        echo "FAIL $1: the segments are not GNU ld's"
        echo "  ours:"; segs "$1.ours" | sed 's/^/    /'
        echo "  GNU ld:"; segs "$1.gnu" | sed 's/^/    /'
        fail=1
    fi
}

# The kernel's: boot code at 1 MiB, the rest at 3 GiB + 1 MiB loaded after it.
same higher_half 'ENTRY(_start)
SECTIONS {
  . = 1M;
  _setup_phys_start = .;
  .setup : { *(.setup) }
  _setup_phys_end = .;
  . = 0xC0100000 + (_setup_phys_end - _setup_phys_start);
  .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000) { *(.text) *(.text.far) }
  .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000) { *(.rodata) }
  .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000) { *(.data) }
  .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000) { *(.bss) }
  /DISCARD/ : { *(.data.after) }
}'

# (The cases below keep code and data on pages of their own: where they
# share one GNU ld makes a single segment of all of them, and this
# linker one of each kind, which is another matter.)

# Code sections more than a page apart, at the distance they are loaded.
same gap 'ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .setup : { *(.setup) }
  .text : { *(.text) }
  . = 0x500000;
  .text.far : { *(.text.far) }
  /DISCARD/ : { *(.rodata) *(.data) *(.bss) *(.data.after) }
}'

# Writable contents after .bss: not in the segment .bss ends.
same after_bss 'ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .setup : { *(.setup) }
  .text : { *(.text) *(.text.far) }
  .data ALIGN(4K) : { *(.data) }
  .bss : { *(.bss) }
  .data.after : { *(.data.after) }
  /DISCARD/ : { *(.rodata) }
}'

# Moved, but within the page the one before ends in: one segment still.
same near 'ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .setup : { *(.setup) }
  . = ALIGN(256);
  .text : { *(.text) *(.text.far) }
  /DISCARD/ : { *(.rodata) *(.data) *(.bss) *(.data.after) }
}'

[ "$fail" -eq 0 ] && echo "ok: script segments"
exit "$fail"
