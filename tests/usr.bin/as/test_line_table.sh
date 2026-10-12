#!/bin/sh
# The line table (AS-T-294, 295, 400).
#
# `.file N` and `.loc` were taken and nothing was made of them, so what
# a compiler built with -g had no line numbers in it; `.loc ... view
# NAME` defined nothing, so the `.uleb128 NAME` a compiler writes in its
# location lists was an undefined symbol in a number; and -g on an
# assembly source did nothing at all.
#
# - The rows `readelf --debug-dump=decodedline` reads are the rows GNU
#   as 2.46 makes, for a source with every thing `.loc` can say -- and
#   with GNU's way of placing a row: at the instruction after the
#   `.loc`, or at the `.loc` after it, and nowhere if nothing follows.
# - A view's name is its number, in `.uleb128` and in `.byte`, and is no
#   symbol of the object; `view -0` begins the count again.
# - -g on a source with no `.loc` gives it a line table of itself and a
#   compile unit that a reader of debugging information takes.
# - Where there is a C compiler, what it writes with -g -O2 is
#   assembled, and where GNU as is at hand its rows and its location
#   lists are GNU's.
#
# GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# rows OBJECT: the rows of its line table, one to a line, the blanks
# between their columns made one.
rows() {
    readelf --debug-dump=decodedline "$1" 2> /dev/null | sed -e 's/[ \t][ \t]*/ /g' -e 's/ $//' | grep -v '^$'
}
# same NAME: got and want are the same.
same() {
    cases=$((cases + 1))
    if ! cmp -s got want; then
        echo "FAIL $1:"; diff want got | head -12 | sed 's/^/  /'; fail=1
    fi
}

# Everything .loc says, in two sections, with a row that waits for the
# next section and one that nothing follows.
cat > h.s << 'EOF'
	.file 1 "dir/x.c"
	.file 2 "/usr/inc/y.h"
	.text
f:	.loc 1 3 5
	nop
	.loc 1 4 1 is_stmt 0
	.loc 1 4 7 prologue_end
	nop
	nop
	.loc 2 10 0 is_stmt 1 discriminator 3
	nop
	.loc 1 9 2 epilogue_begin basic_block isa 1
	ret
	.loc 1 12 1
	.section .text.b,"ax"
g:	.loc 1 20
	nop
	.loc 1 21
EOF
cat > want << 'EOF'
Contents of the .debug_line section:
x.c:
File name Line number Starting address View Stmt
x.c 3 0 x
x.c 4 0x1
x.c 4 0x1 1
/usr/inc/y.h:
y.h 10 0x3 x
dir/x.c:
 Set ISA to 1
x.c 9 0x4 x
x.c - 0x5
 Set ISA to 1
x.c 12 0 x
x.c 20 0 1 x
x.c - 0x1
EOF
for m in 64 32; do
    "$AS" --$m -o h.o h.s 2> err || { echo "FAIL --$m h.s: $(tail -1 err)"; fail=1; }
    rows h.o > got
    same "--$m: the rows of every thing .loc says"
done

# Views.
cat > v.s << 'EOF'
	.file 0 "/src" "v.c"
	.file 1 "v.c"
	.file 2 "/usr/include/h.h"
	.text
f:	.loc 1 3 1 view -0
	.loc 1 3 5 is_stmt 0 view .LVU1
	nop
	.loc 1 4 2 is_stmt 1 view .LVU2
	.loc 2 9 7 view .LVU3
	.loc 1 4 9 is_stmt 0 view .LVU4
	.p2align 2
	nop
	.loc 1 5 1 is_stmt 1 view .LVU5
	ret
g:	.loc 1 8 1 view -0
	.loc 1 8 3 view .LVU6
	ret
	.data
	.uleb128 .LVU1
	.uleb128 .LVU2
	.uleb128 .LVU3
	.byte .LVU4, .LVU5
	.uleb128 .LVU6
	.byte 0xff
EOF
cat > want << 'EOF'
Contents of the .debug_line section:
v.c:
File name Line number Starting address View Stmt
v.c 3 0 x
v.c 3 0 1
v.c 4 0x1 x
/usr/include/h.h:
h.h 9 0x1 1 x
/src/v.c:
v.c 4 0x1 2
v.c 5 0x5 x
v.c 8 0x6 x
v.c 8 0x6 1 x
v.c - 0x7
EOF
"$AS" --64 -o v.o v.s 2> err || { echo "FAIL v.s: $(tail -1 err)"; fail=1; }
rows v.o > got
same "the rows of a source with views"
cases=$((cases + 1))
data=$(objcopy -O binary -j .data v.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
[ "$data" = 010001020001ff ] || { echo "FAIL the numbers of the views: $data, and GNU as: 010001020001ff"; fail=1; }
cases=$((cases + 1))
[ "$(nm v.o | grep -c LVU)" = 0 ] || { echo "FAIL a view is a symbol of the object"; fail=1; }

# -g, and a source that says nothing of lines.
printf '\t.text\n\t.globl f\nf:\tpush %%rbp\n\tmov %%rsp,%%rbp\n\n\tnop\n\tpop %%rbp\n\tret\n\t.data\nv:\t.long 1\n' > plain.s
cat > want << 'EOF'
Contents of the .debug_line section:
CU: plain.s:
File name Line number Starting address View Stmt
plain.s 3 0 x
plain.s 4 0x1 x
plain.s 6 0x4 x
plain.s 7 0x5 x
plain.s 8 0x6 x
plain.s - 0x7
EOF
for opt in -g --gen-debug; do
    "$AS" --64 $opt -o plain.o plain.s 2> err || { echo "FAIL $opt: $(tail -1 err)"; fail=1; }
    rows plain.o > got
    same "$opt: the rows of the source itself"
    cases=$((cases + 1))
    readelf --debug-dump=info plain.o > info 2> info.err
    if [ -s info.err ] || ! grep -q 'DW_TAG_compile_unit' info || ! grep -q 'plain\.s' info; then
        echo "FAIL $opt: the compile unit: $(head -1 info.err)"; fail=1
    fi
done
# Without it there is no table; and a source with .loc of its own keeps
# its own under -g.
cases=$((cases + 1))
"$AS" --64 -o none.o plain.s 2> err
[ "$(readelf -SW none.o | grep -c debug_)" = 0 ] || { echo "FAIL debugging sections without -g"; fail=1; }
"$AS" --64 -g -o h.o h.s 2> err
rows h.o | grep -q '^x\.c 20 0 1 x' || { echo "FAIL -g and a source with .loc of its own"; fail=1; }
cases=$((cases + 1))
# A numbered .file is of a table the source means to write, though it
# has no .loc: -g adds no rows of the assembly source to it.  (GCC's
# configure asks exactly this, and where the object named the source it
# took -g to be unusable for a compiler's output.)
printf '\t.text\nfoo:\tnop\n\t.file 1 "foo.c"\n' > numbered.s
cases=$((cases + 1))
if "$AS" --64 -g -o numbered.o numbered.s 2> err; then
    objdump -W numbered.o 2> /dev/null | grep -q 'numbered\.s' &&
        { echo "FAIL -g and a source with a numbered .file: the object names the assembly source"; fail=1; }
else
    echo "FAIL -g and a source with a numbered .file: $(head -1 err)"; fail=1
fi
# And one that writes its debugging sections out, as a compiler without
# .loc does, keeps those under -g: it was refused, as writing
# .debug_line twice, and GCC's configure took the assembler for one that
# cannot be given -g at all.
cat > own.s <<'EOF'
	.text
f:	nop
	.section .debug_line,"",@progbits
	.long 0x11223344
	.byte 0x55
	.section .debug_info,"",@progbits
	.byte 0x66
EOF
for opt in -g --gdwarf-4; do
    cases=$((cases + 1))
    if ! "$AS" --64 "$opt" -o own.o own.s 2> err; then
        echo "FAIL $opt and a source with .debug_line of its own: $(head -1 err)"; fail=1
        continue
    fi
    [ "$(objdump -s -j .debug_line own.o | tail -n +5 | cut -c7-41 | tr -d ' \n')" = 4433221155 ] ||
        { echo "FAIL $opt: .debug_line is not the source's own"; fail=1; }
    [ "$(objdump -s -j .debug_info own.o | tail -n +5 | cut -c7-41 | tr -d ' \n')" = 66 ] ||
        { echo "FAIL $opt: .debug_info is not the source's own"; fail=1; }
done

# A compiler's output.
gnu=
if as --version 2> /dev/null | grep -q '^GNU assembler'; then
    gnu=as
fi
if command -v gcc > /dev/null 2>&1; then
    cat > c.c << 'EOF'
#include <stdlib.h>
struct s { int a; long b; };
extern int g(int *, struct s *);
int f(int n, struct s *p) { int v[n > 0 ? n : 1]; int r = 0; for (int i = 0; i < n; i++) { v[i] = g(&v[0], p) + i; r += v[i] * p->a; } return r + (int)p->b; }
static int h(int x) { return x ? h(x - 1) + g(&x, 0) : 0; }
int k(int x) { if (x > 3) abort(); return h(x) + f(x, 0); }
EOF
    for m in 64 32; do
        for opt in -O0 -O2; do
            gcc -m$m -g $opt -fno-pic -S -o c.s c.c 2> /dev/null || continue
            cases=$((cases + 1))
            if ! "$AS" --$m -o c.o c.s 2> err; then
                echo "FAIL gcc -m$m -g $opt: $(tail -1 err)"; fail=1; continue
            fi
            readelf --debug-dump=decodedline c.o > /dev/null 2> rd.err
            readelf --debug-dump=info c.o > /dev/null 2>> rd.err
            [ -s rd.err ] && { echo "FAIL gcc -m$m -g $opt: readelf: $(head -1 rd.err)"; fail=1; }
            if [ -n "$gnu" ]; then
                "$gnu" --$m -o g.o c.s
                rows c.o > got; rows g.o > want
                same "gcc -m$m -g $opt: the rows"
                if [ "$m" = 64 ] && [ "$opt" = -O2 ]; then
                    cases=$((cases + 1))
                    a=$(objdump -Z -s -j .debug_loclists g.o | tail -n +5 | cut -c7-41 | tr -d ' \n')
                    b=$(objdump -Z -s -j .debug_loclists c.o | tail -n +5 | cut -c7-41 | tr -d ' \n')
                    [ "$a" = "$b" ] || { echo "FAIL gcc -g -O2: the location lists are not GNU as's"; fail=1; }
                fi
            fi
        done
    done
fi

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
