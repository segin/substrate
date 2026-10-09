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
    -I"$top/usr.lib/elfobj/src" "$top/usr.bin/ld/ld.c" "$top"/usr.lib/elfobj/src/*.c || {
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
bad between    "SECTIONS { .text : { *(.text) mid = .; *(.text.*) } }" "between two input section descriptions"
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
