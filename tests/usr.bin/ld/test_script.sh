#!/bin/sh
# Linker scripts: what the parser accepts and refuses, and what a script
# does to the layout.  The linker is built for the host out of the tree,
# so nothing of a target build is disturbed, and links a freestanding
# 32-bit object; the results are read with the host's nm and readelf.
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

cat > "$work/t.c" <<'EOF'
extern char etext[], bss_begin[], bss_finish[];
int counter = 5;
static int zeroes[64];
const char msg[] = "hi";
int helper(int x) { return x + msg[0]; }
void _start(void) { zeroes[0] = helper(counter) + (int)(etext - bss_begin) + (int)(bss_finish - etext); for (;;) { } }
EOF
${CC:-cc} -m32 -c -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables -ffunction-sections \
    -o "$work/t.o" "$work/t.c" || { echo "SKIP: no 32-bit compiler"; exit 0; }

# link NAME SCRIPT: 0 if the link succeeds; its messages in $work/err
link() {
    printf '%s\n' "$2" > "$work/$1.lds"
    (cd "$work" && ./ld -m elf_i386 -T "$1.lds" -o "$1.out" t.o) > "$work/err" 2>&1
}
ok() {    # NAME SCRIPT
    if link "$1" "$2"; then echo "ok   $1"; else echo "FAIL $1: $(head -1 "$work/err")"; fail=1; fi
}
bad() {   # NAME SCRIPT MESSAGE
    if link "$1" "$2"; then echo "FAIL $1: linked"; fail=1
    elif grep -q -- "$3" "$work/err"; then echo "ok   $1"
    else echo "FAIL $1: wanted '$3', got: $(head -1 "$work/err")"; fail=1; fi
}
sym() {   # NAME SYMBOL -> its value, in hex without leading zeros
    nm "$work/$1.out" | awk -v s="$2" '$3 == s { sub(/^0+/, "", $1); print $1 == "" ? "0" : $1 }'
}
is() {    # WHAT GOT WANT
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi
}

std='etext = 0; bss_begin = 0; bss_finish = 0;'

# Expressions: an operator is an operator with or without spaces round it.
ok  minus      "$std x = 10; y = 3; z = x-y; ASSERT(z == 7, \"minus\")"
ok  divide     "$std x = 12; z = x/4; ASSERT(z == 3, \"divide\")"
ok  compound   "$std x = 1; x += 4; x <<= 1; x-=2; ASSERT(x == 8, \"compound\");"
ok  nosemi     "$std ASSERT(1 == 1, \"fine\") x = 1;"
bad assert     "$std ASSERT(1 == 2, \"one is not two\")" "one is not two"
bad undefined  "$std x = nowhere + 1;" "undefined symbol 'nowhere'"
ok  forward    "$std a = b + 1; b = 2; ASSERT(a == 3, \"forward\")"
ok  unprovided "$std PROVIDE(lo = 0x10); PROVIDE(hi = 0x30); span = hi - lo; ASSERT(span == 0x20, \"span\")"
ok  program    "$std ASSERT(_start != 0 && helper != _start && counter != 0, \"addresses\")"
ok  sections   "$std ASSERT(SIZEOF(.data) == 4 && ADDR(.data) > 0 && SIZEOF(.nothing) == 0, \"sections\")"
ok  defined    "$std mine = 1; ASSERT(DEFINED(helper) && DEFINED(mine) && !DEFINED(nowhere), \"defined\")"
ok  suffixes   "$std ASSERT(64K == 0x10000 && 2M == 0x200000 && 1k == 1024, \"suffixes\")"
ok  cond       "$std x = 1 ? 10 : 20; y = 0 ? 10 : 1 ? 30 : 40; ASSERT(x == 10 && y == 30, \"conditional\")"
ok  cond_lazy  "$std x = DEFINED(nowhere) ? nowhere : 7; y = 1 ? 5 : 1 / 0; ASSERT(x == 7 && y == 5, \"lazy\")"
bad cond_colon "$std x = 1 ? 2;" "expected ':'"
ok  functions  "$std ASSERT(MAX(3, 9) == 9 && MIN(3, 9) == 3 && ABSOLUTE(5) == 5 && CONSTANT(MAXPAGESIZE) == 4096 &&
                            SIZEOF_HEADERS > 52 && ALIGNOF(.data) == 4, \"functions\")"
ok  regions    "$std MEMORY { rom : ORIGIN = 0x800000, LENGTH = 256K ram : org = ORIGIN(rom) + LENGTH(rom), len = 64K }
                ASSERT(ORIGIN(ram) == 0x840000 && LENGTH(ram) == 0x10000, \"regions\")"
bad no_region  "$std x = ORIGIN(nowhere);" "which MEMORY does not define"
bad no_func    "$std x = FROBNICATE(1);" "FROBNICATE(): not a function"
bad dot_top    "$std x = .;" "no value outside SECTIONS"
bad align_top  "$std x = ALIGN(16);" "no value outside SECTIONS"
ok  align_two  "$std x = ALIGN(17, 16); ASSERT(x == 32, \"align\")"

# What cannot be read is said to be so, with the place.
bad comment    "$std /* never closed" "comment is not closed"
bad string     "$std ASSERT(0, \"never closed)" "string is not closed"
bad semicolon  "x = 1 y = 2;" "expected ';'"
bad sections   "SECTIONS { .text : { *(.text) }" "SECTIONS is not closed"
bad phdrs      "PHDRS { text PT_LOAD" "expected ';' after the program header"
bad brace      "}" "unexpected '}'"
bad backwards  "SECTIONS { .text : { *(.text*) } . = 0x10; .data : { *(.data) } }" "may not move back"
bad no_phdr    "$std PHDRS { a PT_LOAD; } SECTIONS { .text : { *(.text*) } :nope }" "does not declare"
bad region     "$std MEMORY { ram : ORIGIN = 0x900000, LENGTH = 0x10 } SECTIONS { .text : { *(.text*) } >ram }" \
               "does not fit in memory region ram"

# The location counter, and the symbols a script defines from it.
ok layout 'ENTRY(_start)
SECTIONS
{
    . = 0x00400000 + 0x1000;
    image = .;
    .text : { *(.text .text.*) }
    PROVIDE(etext = .);
    PROVIDE(helper = 0x1234);
    . = ALIGN(0x1000);
    rodata_begin = .;
    .rodata : { *(.rodata .rodata.*) }
    . = 0x00500000;
    .data : { data_begin = .; *(.data .data.*) data_finish = .; }
    .bss : { bss_begin = .; *(.bss .bss.*) *(COMMON) . = ALIGN(., 0x400); bss_finish = .; }
    . += 0x100;
    end = .;
    ASSERT(bss_finish > bss_begin && (bss_finish & 0x3ff) == 0, "bss is not padded to its alignment")
    /DISCARD/ : { *(.comment) *(.note*) }
}
size = end - image;'
is "image"            "$(sym layout image)" 401000
is "text begins it"   "$(readelf -SW "$work/layout.out" | sed -n 's/.* \.text  *PROGBITS  *\([0-9a-f]*\) .*/\1/p')" 00401000
is "function sections folded into .text" "$(readelf -SW "$work/layout.out" | grep -c ' \.text\.')" 0
is "ALIGN(n) aligns the counter" "$(sym layout rodata_begin)" 402000
is "data where told"  "$(sym layout data_begin)" 500000
is "data's extent"    "$(sym layout data_finish)" 500004
is "bss padded"       "$(sym layout bss_finish)" 500400
is "end"              "$(sym layout end)" 500500
is "size is absolute" "$(nm "$work/layout.out" | awk '$3 == "size" { print $2 }')" A
is "size"             "$(sym layout size)" ff500
is "PROVIDE leaves a definition alone" "$(nm "$work/layout.out" | awk '$3 == "helper" { print $2 }')" T
is "discarded"        "$(readelf -SW "$work/layout.out" | grep -c '\.comment')" 0
is "a segment for what is placed apart" "$(readelf -lW "$work/layout.out" | grep -c '^  LOAD')" 3

# PHDRS: a section goes where the one before it went, and a PT_LOAD
# without FLAGS has the permissions of what is in it.
ok phdrs "$std"'
PHDRS { text PT_LOAD FILEHDR PHDRS; data PT_LOAD; ro PT_LOAD FLAGS(4); }
SECTIONS
{
    . = 0x00800000 + 0x1000;
    .text : { *(.text .text.*) } :text
    . = 0x00810000;
    .rodata : { *(.rodata*) } :ro
    . = 0x00900000;
    .data : { *(.data*) } :data
    .bss : { *(.bss*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}'
is "text is R E" "$(readelf -lW "$work/phdrs.out" | awk '$1 == "LOAD" && $3 ~ /0080[01]000/ { print $7 $8 }')" RE
is "data is RW"  "$(readelf -lW "$work/phdrs.out" | awk '$1 == "LOAD" && $3 == "0x00900000" { print $7 }')" RW
is "bss follows data into its header" \
   "$(readelf -lW "$work/phdrs.out" | sed -n '/Section to Segment/,$p' | grep -c '\.data \.bss')" 1

# A 32-bit address space wraps: a call across more than half of it is in
# range, being the same call the other way round.
printf 'int helper(int);\nvoid far(void) { helper(1); }\n' > "$work/far.c"
${CC:-cc} -m32 -c -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables -o "$work/far.o" "$work/far.c"
printf '%s SECTIONS { . = 0x08048000 + SIZEOF_HEADERS; .text : { t.o(.text*) } . = 0xc0100000; .hi : { far.o(.text*) } }\n' \
    "$std" > "$work/wrap.lds"
if (cd "$work" && ./ld -m elf_i386 -T wrap.lds -o wrap.out t.o far.o) > "$work/err" 2>&1; then
    is "a call from the top of the address space to the bottom" \
       "$(objdump -d "$work/wrap.out" | sed -n '/<far>:/,/ret/p' | grep -c 'call .*<helper>')" 1
else
    echo "FAIL a call across the address space: $(head -1 "$work/err")"; fail=1
fi

# Statements between two input descriptions see the location counter
# where they stand, and the sections go in in the descriptions' order, not
# the inputs': Linux's scripts bracket tables with `__start_x = .;
# KEEP(*(x)) __stop_x = .;` inside one output section, and realmode's has
# `. = ALIGN(16); video_cards = .;` between two.  They were refused.
ok  between "$std SECTIONS { .text : { *(.text) mid = .; *(.text.helper) . = ALIGN(64); al = .; *(.text._start)
                after = .; } }
             ASSERT(helper == mid && al % 64 == 0 && al > mid && _start == al && after > _start, \"between\")"
ok  between_order "$std SECTIONS { .text : { *(.text._start) first_end = .; *(.text.helper) } }
             ASSERT(_start < helper && helper == first_end, \"order\")"

# The gap before a description's sections is every statement since the last
# description that had any: an ALIGN before one with none still counts
# (vmlinux's `. = ALIGN(4); *(.text.asan.* .text.tsan.*)`, empty), and a
# symbol set there is an offset a later `. = sym + n` is worked out from
# (vmlinux's init stack).  Both were refused: "the location counter moved
# past where these sections were put".
ok  gap_empty "$std SECTIONS { .text : { *(.text._start) . = ALIGN(64); *(.nothing) a = .; *(.text.helper) } }
             ASSERT(helper == a && a % 64 == 0, \"gap_empty\")"
ok  gap_symbol "$std SECTIONS { .text : { *(.text*) } .data : { s = .; *(.data*) . = s + 0x100; e = .; *(.bss*) } }
             ASSERT(e - s == 0x100 && SIZEOF(.data) > 0x100, \"gap_symbol\")"
# An output section nothing goes into, given its own address (vmlinux's
# `.stab 0 : { *(.stab) }`), leaves the location counter where it was:
# "may not move back over what is placed".
ok  empty_at_zero "$std SECTIONS { .text : { *(.text*) } .stab 0 : { *(.stab) } .data : { *(.data*) } }
             ASSERT(ADDR(.data) > ADDR(.text), \"empty_at_zero\")"
# `. = ASSERT(cond, msg)` is the assertion, the counter staying put, as GNU
# ld reads it (vmlinux checks its size so); it was "not a function".
ok  dot_assert "$std SECTIONS { .text : { *(.text*) } . = ASSERT(1, \"fine\"); .data : { *(.data*) } }"
bad dot_assert_fails "$std SECTIONS { .text : { *(.text*) } . = ASSERT(0, \"too big\"); }" "too big"
# A PROVIDE nothing needs is not evaluated where it cannot be: vmlinux's
# PROVIDE(__ref_stack_chk_guard = __stack_chk_guard), without the symbol.
ok  provide_unneeded "$std PROVIDE(unneeded = nowhere_at_all); SECTIONS { .text : { *(.text*) } }"

# An image linked at 0, read-only data before the code, as Linux's realmode
# blob is: the script's order and addresses hold, though the default
# policy puts code first; no section the linker makes itself (.dynstr, its
# own note, which the script's /DISCARD/ of .note* covers) lands among
# them; no PT_PHDR that nothing loads; and the segments are runs of one
# kind of section, none spanning another.  The output was refused when
# read back, or not written at all.
ok  at_zero "$std SECTIONS { . = 0; .rodata : { r0 = .; *(.rodata*) } . = ALIGN(4096); tstart = .;
                .text : { *(.text*) } . = ALIGN(4096); .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) }
                /DISCARD/ : { *(.note*) } }
             ASSERT(r0 == 0 && tstart == 0x1000 && ADDR(.text) == 0x1000 && ADDR(.data) == 0x2000, \"placed\")"
if [ -f "$work/at_zero.out" ]; then
    is  at_zero_sections "$(readelf -SW "$work/at_zero.out" | grep -cE ' \.(dynstr|note\.substrate_ld) ')" 0
    is  at_zero_phdr "$(readelf -lW "$work/at_zero.out" 2>&1 | grep -cE '^ *PHDR|Error')" 0
    is  at_zero_loads "$(readelf -lW "$work/at_zero.out" | awk '$1 == "LOAD" { print $3, $6 }' |
        awk '{ lo = strtonum($1); hi = lo + strtonum($2); if (n && lo < last) bad = 1; last = hi; n++ }
             END { print bad ? "overlap" : n }')" 3
fi

# Data statements put their values where they stand, after a section's
# input sections or in a section of nothing else, which is made for them
# (Linux's setup.ld: `.signature : { setup_sig = .; LONG(0x5a5aaa55) }`);
# a value may use a symbol defined after it.  They were refused.
ok  data_stmts "$std SECTIONS { .text : { *(.text*) } .data : { *(.data*) BYTE(0x11) SHORT(0x2233) LONG(s_end - s_start) }
                .signature : { s_start = .; LONG(0x5a5aaa55) QUAD(0x0102030405060708) s_end = .; } }
             ASSERT(SIZEOF(.signature) == 12 && s_end - s_start == 12, \"data_stmts\")"
if [ -f "$work/data_stmts.out" ]; then
    is  data_stmts_signature "$(readelf -x .signature "$work/data_stmts.out" | awk '/0x/ { print $2, $3, $4 }')" \
        "55aa5a5a 08070605 04030201"
    is  data_stmts_after_inputs "$(readelf -x .data "$work/data_stmts.out" | grep -o '1133220c')" 1133220c
fi

# Zeros (.bss) gathered into a section with contents (.data) are written
# out as zeros, as GNU ld does: Linux's vDSO script puts *(.bss*) in
# .data, and the merge stopped at "section type mismatch".
ok  bss_in_data "$std SECTIONS { .text : { *(.text*) } .data : { *(.data*) *(.bss*) *(COMMON) } }
             ASSERT(SIZEOF(.data) >= 4 + 64 * 4, \"bss_in_data\")"
if [ -f "$work/bss_in_data.out" ]; then
    is  bss_in_data_type "$(readelf -SW "$work/bss_in_data.out" |
        awk '{ for (i = 1; i < NF; i++) if ($i == ".data") print $(i + 1) }')" PROGBITS
fi

# AT() says where a section is loaded, and a segment's p_paddr is its first
# section's; a section after one with AT() loads as far from its address.
# The i386 kernel is linked at 0xc1000000 and loaded at 0x1000000, and its
# decompressor puts each segment at its p_paddr; AT() was read and ignored.
ok  at_lma "$std SECTIONS { . = 0xc1000000; .text : AT(ADDR(.text) - 0xc0000000) { *(.text*) }
             . = ALIGN(4096); .data : { *(.data*) } }"
if [ -f "$work/at_lma.out" ]; then
    is  at_lma_paddr "$(readelf -lW "$work/at_lma.out" | awk '$1 == "LOAD" { printf "%s-%s ", $3, $4 }')" \
        "$(readelf -lW "$work/at_lma.out" | awk '$1 == "LOAD" { v = strtonum($3); printf "%s-0x%08x ", $3, v - 0xc0000000 }')"
fi

# PHDRS without FILEHDR: the headers are in no segment, and there is no
# PT_PHDR the script did not ask for.  vmlinux's text segment, so made,
# took the headers in a page below its code -- at p_paddr 0xfff000, where
# the decompressor would have put them -- and had a PT_PHDR nothing loads.
ok  phdrs_nofilehdr "$std PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
             SECTIONS { . = 0xc1000000; .text : AT(ADDR(.text) - 0xc0000000) { *(.text*) } :text
             . = ALIGN(4096); .data : AT(ADDR(.data) - 0xc0000000) { *(.data*) *(.bss*) *(COMMON) } :data }"
if [ -f "$work/phdrs_nofilehdr.out" ]; then
    is  phdrs_nofilehdr_text "$(readelf -lW "$work/phdrs_nofilehdr.out" | awk '$1 == "LOAD" { print $3, $4; exit }')" \
        "0xc1000000 0x01000000"
    is  phdrs_nofilehdr_phdr "$(readelf -lW "$work/phdrs_nofilehdr.out" 2>&1 | grep -cE '^ *PHDR|Error')" 0
fi

# INCLUDE: where a statement may stand, its statements standing there.
mkdir -p "$work/inc"
echo '*(.text .text.*)' > "$work/inc/body.lds"
echo '.rodata : { *(.rodata*) }' > "$work/inc/sec.lds"
echo 'included = 42;' > "$work/inc/top.lds"
ok  include "$std INCLUDE inc/top.lds
SECTIONS { .text : { INCLUDE inc/body.lds } INCLUDE inc/sec.lds } ASSERT(included == 42, \"include\")"
bad include_missing "INCLUDE nowhere.lds" "file not found"

# Nesting is bounded.
deep=$(awk 'BEGIN { for (i = 0; i < 1000; i++) printf "("; printf "1"; for (i = 0; i < 1000; i++) printf ")" }')
bad deep "$std x = $deep;" "nested too deeply"
long=$(awk 'BEGIN { printf "1"; for (i = 0; i < 5000; i++) printf "+1" }')
bad long "$std x = $long;" "expression is too long"

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
