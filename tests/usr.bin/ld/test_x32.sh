#!/bin/sh
# -m elf32_x86_64: x32, x86-64 code in ELFCLASS32 objects.
#
# Linux, with CONFIG_X86_X32_ABI, builds an x32 vDSO: it compiles the
# vDSO as 64-bit, converts each object with `objcopy -O elf32-x86-64`,
# and links them with `ld -m elf32_x86_64 -shared` through a script of
# PHDRS.  The linker refused the emulation ("unsupported emulation
# 'elf32_x86_64' for -m"), and the kernel did not build.
#
# GNU ld is the judge.  Objects of our own converted so are linked as the
# kernel links its vDSO (its options, a script like its own) into a
# shared object whose program headers (type, flags, alignment), sections
# (name, type, flags, in order) are GNU ld's, which has no relocations,
# and which exports what GNU ld's does; under -r and into a program they
# are ELFCLASS32 x86-64 objects with RELA relocations, as GNU ld's.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GNU_LD:-ld} --version 2>/dev/null | grep -q 'GNU ld' ||
   ! ${GNU_LD:-ld} -V 2>/dev/null | grep -q elf32_x86_64; then
    echo "skip: GNU ld with elf32_x86_64 is the judge"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
# a vDSO's shape: functions reading data the kernel maps before the image,
# a note, rodata, and an alternative-instructions table
cat > clock.c <<'EOF'
extern const volatile long vdso_data[];
static const char names[][8] = { "mono", "real" };
long __vdso_clock(int which) { return vdso_data[which] + names[which & 1][0]; }
long __vdso_time(long *t) { long v = vdso_data[2]; if (t) *t = v; return v; }
int __vdso_getcpu(unsigned *cpu) { if (cpu) *cpu = (unsigned)vdso_data[3]; return 0; }
EOF
cat > note.S <<'EOF'
	.section .note.Linux, "a", @note
	.balign 4
	.long 2f - 1f
	.long 4f - 3f
	.long 0
1:	.asciz "Linux"
2:	.balign 4
3:	.long 0x60000
4:	.balign 4
	.section .altinstructions, "a"
	.long 0
EOF
cat > vdso.lds <<'EOF'
SECTIONS
{
	PROVIDE(vdso_data = . - 4 * 4096);
	. = SIZEOF_HEADERS;
	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }
	.dynamic	: { *(.dynamic) }		:text	:dynamic
	.rodata		: { *(.rodata*) *(.data*) *(.got.plt) *(.got) *(.bss*) }	:text
	.note.gnu.property : { *(.note.gnu.property) }	:text	:note	:gnu_property
	.note		: { *(.note*) }			:text	:note
	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text
	.text		: { *(.text*) }			:text	=0x90909090
	.altinstructions : { *(.altinstructions) }	:text
	/DISCARD/ : { *(.discard) *(.discard.*) }
}
PHDRS
{
	text		PT_LOAD		FLAGS(5) FILEHDR PHDRS;
	dynamic		PT_DYNAMIC	FLAGS(4);
	note		PT_NOTE		FLAGS(4);
	eh_frame_hdr	PT_GNU_EH_FRAME;
	gnu_property	PT_GNU_PROPERTY;
}
EOF
${GCC:-gcc} -O2 -fPIC -fno-stack-protector -c -o clock.o clock.c || exit 1
${GCC:-gcc} -c -o note.o note.S || exit 1
for o in clock note; do
    objcopy -O elf32-x86-64 $o.o $o-x32.o || exit 1
done

flags="-shared --hash-style=both --build-id=sha1 --no-undefined --eh-frame-hdr -Bsymbolic -z noexecstack
       -m elf32_x86_64 -soname linux-vdso.so.1 -z max-page-size=4096 -T vdso.lds"
# shellcheck disable=SC2086
"${GNU_LD:-ld}" -o gnu.so $flags note-x32.o clock-x32.o || exit 1
# shellcheck disable=SC2086
if ! ./ld -o ours.so $flags note-x32.o clock-x32.o > said 2>&1; then
    echo "FAIL: ld -m elf32_x86_64 -shared: $(head -1 said)"; exit 1
fi

phdrs() {
    readelf -lW "$1" | awk '$1 ~ /^(LOAD|DYNAMIC|NOTE|GNU_EH_FRAME|GNU_PROPERTY|PHDR|GNU_STACK|INTERP)$/ { print $1, $7 $8, $NF }'
}
sections() {
    readelf -SW "$1" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk '$1 != "" { f = ""; for (i = 8; i < NF - 2; i++) f = f $i; print $1, $2, f }' |
        grep -v -E '^\.(comment|symtab|strtab|shstrtab) '
}
dynsyms() {
    readelf --dyn-syms -W "$1" | awk 'NR > 3 && $8 != "" { sub(/@.*/, "", $8); print $8, $4, $5, ($7 == "UND" ? "UND" : "DEF") }' | sort
}

if [ "$(readelf -hW ours.so | awk '/Class:/ { print $2 } /Machine:/ { print $2, $3, $4 }' | tr '\n' ' ')" != \
     "$(readelf -hW gnu.so | awk '/Class:/ { print $2 } /Machine:/ { print $2, $3, $4 }' | tr '\n' ' ')" ]; then
    echo "FAIL: the shared object is not GNU ld's class and machine"; fail=1
fi
phdrs gnu.so > want; phdrs ours.so > have
if ! cmp -s want have; then
    echo "FAIL: the program headers (type, flags, alignment) differ from GNU ld's:"; diff want have | sed 's/^/    /'
    fail=1
fi
# (.gnu.version_d is the script's VERSION, which this linker does not read; see below)
sections gnu.so | grep -v '^\.gnu\.version_d ' > want; sections ours.so > have
if ! cmp -s want have; then
    echo "FAIL: the sections differ from GNU ld's:"; diff want have | sed 's/^/    /'; fail=1
fi
if [ "$(readelf -rW ours.so | grep -c ' R_')" -ne 0 ]; then
    echo "FAIL: the vDSO has relocations: $(readelf -rW ours.so | grep ' R_' | head -2)"; fail=1
fi
# Where the inputs' property notes differ, the merged note is aligned as the strictest of them (objcopy's
# 8); and where no input has one (the kernel's x32 vDSO), PT_GNU_PROPERTY is empty and aligned to the
# ELF32 word, 4.
cat > prop.s <<'EOF'
	.section .note.gnu.property, "a", @note
	.p2align 3
	.long 4, 1f - 0f, 5
	.asciz "GNU"
0:	.long 0xc0008002, 4, 2
	.p2align 3
1:
	.text
	.globl __vdso_other
__vdso_other:	ret
EOF
${AS_GNU:-as} --64 -o prop.o prop.s && objcopy -O elf32-x86-64 prop.o prop-x32.o || exit 1
# shellcheck disable=SC2086
"${GNU_LD:-ld}" -o gnu2.so $flags note-x32.o clock-x32.o prop-x32.o || exit 1
# shellcheck disable=SC2086
if ! ./ld -o ours2.so $flags note-x32.o clock-x32.o prop-x32.o > said 2>&1; then
    echo "FAIL: ld -m elf32_x86_64 -shared, notes that differ: $(head -1 said)"; fail=1
elif [ "$(phdrs gnu2.so)" != "$(phdrs ours2.so)" ]; then
    echo "FAIL: notes that differ: the program headers differ from GNU ld's:"
    phdrs gnu2.so > want; phdrs ours2.so > have; diff want have | sed 's/^/    /'
    fail=1
fi
${GCC:-gcc} -O2 -fPIC -fno-stack-protector -fcf-protection=none -Wa,-mx86-used-note=no -c -o bare.o clock.c &&
    ${GCC:-gcc} -Wa,-mx86-used-note=no -c -o barenote.o note.S || exit 1
objcopy -O elf32-x86-64 bare.o bare-x32.o && objcopy -O elf32-x86-64 barenote.o barenote-x32.o || exit 1
if readelf -SW bare-x32.o barenote-x32.o | grep -q note.gnu.property; then
    echo "skip: the compiler makes property notes regardless"
else
    # shellcheck disable=SC2086
    "${GNU_LD:-ld}" -o gnu3.so $flags barenote-x32.o bare-x32.o || exit 1
    # shellcheck disable=SC2086
    if ! ./ld -o ours3.so $flags barenote-x32.o bare-x32.o > said 2>&1; then
        echo "FAIL: ld -m elf32_x86_64 -shared, no property notes: $(head -1 said)"; fail=1
    elif [ "$(phdrs gnu3.so)" != "$(phdrs ours3.so)" ]; then
        echo "FAIL: no property notes: the program headers differ from GNU ld's:"
        phdrs gnu3.so > want; phdrs ours3.so > have; diff want have | sed 's/^/    /'
        fail=1
    fi
fi

# what GNU ld's exports, ours exports alike (it exports more: without a VERSION, every global)
dynsyms gnu.so > want; dynsyms ours.so > have
if [ -n "$(comm -23 want have)" ]; then
    echo "FAIL: GNU ld's exports these and ours does not: $(comm -23 want have | tr '\n' ' ')"; fail=1
fi

# -r, and a program: ELFCLASS32 x86-64 with RELA relocations
"${GNU_LD:-ld}" -m elf32_x86_64 -r -o gnu-r.o clock-x32.o note-x32.o || exit 1
if ! ./ld -m elf32_x86_64 -r -o ours-r.o clock-x32.o note-x32.o > said 2>&1; then
    echo "FAIL: ld -m elf32_x86_64 -r: $(head -1 said)"; fail=1
else
    kinds() { readelf -SW "$1" | awk '$3 == "REL" || $3 == "RELA" || $4 == "REL" || $4 == "RELA" { print }' |
              sed -n 's/^ *\[ *[0-9]*\] //p' | awk '{ print $1, $2 }' | sort; }
    if [ "$(kinds gnu-r.o)" != "$(kinds ours-r.o)" ] || [ "$(readelf -hW ours-r.o | awk '/Class:/ { print $2 }')" != ELF32 ]; then
        echo "FAIL: ld -r: relocation sections [$(kinds ours-r.o | tr '\n' ' ')], GNU ld's [$(kinds gnu-r.o | tr '\n' ' ')]"
        fail=1
    fi
fi
printf '\t.text\n\t.globl _start\n_start:\tlea data(%%rip), %%eax\n\tret\n\t.data\ndata:\t.long 1\n' > start.s
${AS_GNU:-as} --x32 -o start.o start.s || exit 1
"${GNU_LD:-ld}" -m elf32_x86_64 -o gnu.out start.o || exit 1
if ! ./ld -m elf32_x86_64 -o ours.out start.o > said 2>&1; then
    echo "FAIL: ld -m elf32_x86_64 (a program): $(head -1 said)"; fail=1
else
    for f in gnu.out ours.out; do
        objdump -d "$f" | awk '/lea/ { sub(/.*# */, ""); print }' > "$f.lea"
    done
    if [ "$(readelf -hW ours.out | awk '/Class:|Type:/ { print $2 }' | tr '\n' ' ')" != "ELF32 EXEC " ] ||
       [ $(( 0x$(nm ours.out | awk '$3 == "data" { print $1 }') )) -ne $(( 0x$(cut -d' ' -f1 < ours.out.lea) )) ]; then
        echo "FAIL: ld -m elf32_x86_64: the program is not an ELF32 executable reaching its data"; fail=1
    fi
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_x32"
fi
exit $fail
