#!/bin/sh
# Call frame information (AS-T-290, 291, 293).
#
# The `.cfi_*` directives were taken and nothing was made of them: the
# object had an `.eh_frame` section with no bytes in it.  A debugger
# could not walk such code's stack, and a C++ exception thrown through
# it ended the program.  They are the CIEs and FDEs GNU as 2.46 makes
# of them, byte for byte, in `.eh_frame` and in `.debug_frame`.
#
# - A source with every directive in it, in 64-bit and in 32-bit code:
#   the bytes of the two sections and their relocations are GNU's.
# - The directives misused, and those this assembler has not, are
#   refused on their line.
# - Where there is a C compiler: what it writes at -O0 and -O2 for both
#   modes has frames that `readelf` reads without complaint, one to a
#   procedure; and where GNU as is at hand as well, they are GNU's.
# - Where there is a C++ compiler: a program assembled here throws
#   through three frames with destructors in them and is caught.
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

cat > all64.s << 'EOF'
	.cfi_sections .eh_frame, .debug_frame
	.text
f:	.cfi_startproc
	.cfi_personality 0x9b,DW.ref.pers
	.cfi_lsda 0x1b,.Lsda
	push %rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	mov %rsp,%rbp
	.cfi_def_cfa_register 6
	.fill 300
	.cfi_remember_state
	.cfi_def_cfa 7, 8
	ret
	.cfi_restore_state
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rbx, 8
	.cfi_register %r12, %r13
	.cfi_restore %rbx
	.cfi_undefined %r14
	.cfi_same_value %r15
	.cfi_escape 0x2e, 0x10
	.cfi_offset %xmm20, -32
	.cfi_offset %rbx, 16
	.cfi_def_cfa_offset -8
	.cfi_window_save
	.cfi_val_offset %rbx, -16
	.cfi_val_offset %rbx, 16
	nop
	.cfi_endproc
g:	.cfi_startproc simple
	.cfi_signal_frame
	.cfi_return_column %rax
	.cfi_def_cfa %rsp, 0
	nop
	.cfi_endproc
h:	.cfi_startproc
	nop
	.cfi_endproc
	.section .text.cold,"ax"
k:	.cfi_startproc
	.cfi_def_cfa_offset 16
	.fill 70000
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
	.section .gcc_except_table,"a"
.Lsda:	.long 0
EOF
# The same for 32-bit code: its registers, its words of four bytes, and
# pointers written whole.
sed -e 's/%rbp/%ebp/g; s/%rsp/%esp/g; s/%rbx/%ebx/g; s/%r12, %r13/%esi, %edi/; s/%r14/%ecx/' \
    -e 's/%r15/%edx/; s/%xmm20/%xmm5/; s/%rax/%eax/; s/0x9b,DW.ref.pers/0x0,pers/; s/0x1b,.Lsda/0x0,.Lsda/' \
    -e 's/-16$/-8/; s/, -32/, -16/; s/, 16$/, 8/; s/offset -8/offset -4/' all64.s > all32.s

# frames MODE SECTION BYTES MD5: the section's bytes, in hexadecimal.
frames() {
    cases=$((cases + 1))
    got=$(objdump -Z -s -j "$2" "all$1.o" 2> /dev/null | tail -n +5 | cut -c7-41 | tr -d ' \n')
    sum="${#got} $(printf '%s' "$got" | md5sum | cut -c1-32)"
    if [ "$sum" != "$3 $4" ]; then
        echo "FAIL --$1 $2: its bytes are not GNU as's ($sum, not $3 $4):"
        readelf --debug-dump=frames "all$1.o" 2>&1 | head -40 | sed 's/^/  /'
        fail=1
    fi
}
# relocs MODE WANT: the relocations of the two sections, offset:type:symbol.
relocs() {
    cases=$((cases + 1))
    got=$(readelf -rW "all$1.o" | awk '/^Relocation/ { s = $3 }
        /^0/ && s ~ /frame/ { sub(/R_X86_64_|R_386_/, "", $3); printf "%s%x:%s:%s", sep, strtonum("0x" $1), $3, $5; sep = "," }')
    [ "$got" = "$2" ] || { echo "FAIL --$1: relocations $got, and GNU as: $2"; fail=1; }
}
for m in 64 32; do
    if ! "$AS" --$m -o all$m.o all$m.s 2> err; then
        echo "FAIL --$m: every directive: $(tail -1 err)"; fail=1
    fi
done
frames 64 .eh_frame 432 0f343a153a81ee253d11ef6e675cc488
frames 64 .debug_frame 416 de4ed4aa21efe2694f98c636f914627e
frames 32 .eh_frame 432 32770fcf4a1eb3aa027f2cdc4a0623ad
frames 32 .debug_frame 328 9e10b20e00f1c91305c59a5c586b9191
relocs 64 '13:PC32:DW.ref.pers,28:PC32:.text,31:PC32:.gcc_except_table,84:PC32:.text,b0:PC32:.text,c4:PC32:.text.cold,1c:32:.debug_frame,20:64:.text,7c:32:.debug_frame,80:64:.text,94:32:.debug_frame,98:64:.text,ac:32:.debug_frame,b0:64:.text.cold'
relocs 32 '13:32:pers,28:PC32:.text,31:32:.gcc_except_table,84:PC32:.text,b0:PC32:.text,c4:PC32:.text.cold,18:32:.debug_frame,1c:32:.text,6c:32:.debug_frame,70:32:.text,7c:32:.debug_frame,80:32:.text,8c:32:.debug_frame,90:32:.text.cold'

# With no .cfi_sections there is .eh_frame and no .debug_frame.
printf 'f:\t.cfi_startproc\n\tnop\n\t.cfi_endproc\n' > one.s
"$AS" --64 -o one.o one.s 2> err
cases=$((cases + 1))
[ "$(readelf -SW one.o | grep -c ' \.eh_frame ')" = 1 ] && [ "$(readelf -SW one.o | grep -c debug_frame)" = 0 ] ||
    { echo "FAIL the sections of a source that names none"; fail=1; }

# refused SOURCE: on its line, a `|` for each new one.
refused() {
    cases=$((cases + 1))
    printf '%s\n' "$1" | tr '|' '\n' > bad.s
    if "$AS" --64 -o bad.o bad.s > /dev/null 2> err; then
        echo "FAIL [$1]: assembled"; fail=1
    elif ! grep -q 'bad\.s:[0-9]' err && ! grep -q 'bad\.s: ' err; then
        echo "FAIL [$1]: refused without its file: $(tail -1 err)"; fail=1
    fi
}
refused '.cfi_endproc'
refused '.cfi_startproc|.cfi_startproc|.cfi_endproc'
refused '.cfi_startproc|nop'
refused '.cfi_def_cfa_offset 8'
refused '.cfi_startproc|.cfi_frobnicate 1|.cfi_endproc'
refused '.cfi_startproc|.cfi_offset %foo, 8|.cfi_endproc'
refused '.cfi_startproc|.cfi_restore_state|.cfi_endproc'
refused '.cfi_startproc|.cfi_offset 6, -3|.cfi_endproc'
refused '.cfi_startproc|.cfi_def_cfa %rsp|.cfi_endproc'
refused '.cfi_startproc|.cfi_inline_lsda|.cfi_endproc'

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
            gcc -m$m $opt -fno-pic -S -o c.s c.c 2> /dev/null || continue
            cases=$((cases + 1))
            if ! "$AS" --$m -o c.o c.s 2> err; then
                echo "FAIL gcc -m$m $opt: $(tail -1 err)"; fail=1; continue
            fi
            readelf --debug-dump=frames c.o > ours.fr 2> ours.err
            if [ -s ours.err ] || [ "$(grep -c ' FDE ' ours.fr)" != "$(grep -c 'cfi_startproc' c.s)" ]; then
                echo "FAIL gcc -m$m $opt: the frames are not one to a procedure, or are not read: $(head -1 ours.err)"
                fail=1
            fi
            if [ -n "$gnu" ]; then
                "$gnu" --$m -o g.o c.s && readelf --debug-dump=frames g.o > gnu.fr 2> /dev/null
                cmp -s gnu.fr ours.fr || { echo "FAIL gcc -m$m $opt: the frames are not GNU as's"; fail=1; }
            fi
        done
    done
fi
if command -v g++ > /dev/null 2>&1; then
    cat > x.cc << 'EOF'
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
struct G { const char *n; G(const char *s) : n(s) {} ~G() { std::printf("~%s ", n); } };
__attribute__((noinline)) int deep(int n) { G g("deep"); std::vector<int> v(n, 1); if (n > 3) throw std::runtime_error("too deep " + std::to_string(n)); return deep(n + 1) + v[0]; }
__attribute__((noinline)) int mid(int n) { G g("mid"); try { return deep(n); } catch (const std::logic_error &) { return -1; } }
int main() { try { mid(0); } catch (const std::exception &e) { std::printf("caught: %s\n", e.what()); return 0; } return 1; }
EOF
    for flags in "-O0" "-O2" "-O2 -fPIC"; do
        g++ $flags -S -o x.s x.cc 2> /dev/null || continue
        cases=$((cases + 1))
        if ! "$AS" --64 -o x.o x.s 2> err; then
            echo "FAIL g++ $flags: $(tail -1 err)"; fail=1; continue
        fi
        g++ $flags -o x.out x.o 2> /dev/null || continue
        got=$(./x.out 2>&1)
        [ "$got" = "~deep ~deep ~deep ~deep ~deep ~mid caught: too deep 4" ] ||
            { echo "FAIL g++ $flags: the program said: $got"; fail=1; }
    done
fi

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
