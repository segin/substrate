#!/bin/sh
# What one library search or one pass of the link leaves behind is not
# taken for the next one's.
#
# A -l that finds nothing says so, and names a library of that name it
# passed over because it was for another machine.  The name was kept
# from search to search: `-La -Lb -lfoo -Bstatic -lfoo`, with a 64-bit
# a/libfoo.so, a 32-bit b/libfoo.so and no libfoo.a, found b/libfoo.so
# for the first -lfoo, passing over a/libfoo.so, and then said of the
# second, which looks for libfoo.a only, that a/libfoo.so had been passed
# over.  GNU ld, the judge, says nothing of a/libfoo.so for the search
# that failed.
#
# The link keeps, for the lookup of indirect functions, a pointer to the
# context of the pass that planned them.  That context is a local of the
# pass, and the pointer was kept after it was freed: a pass that read it
# before planning its own read a context whose lifetime had ended.  The
# linker is built with a check, run when it exits, that it holds no such
# pointer.
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

cd "$work" || exit 1

# ld_reloc.c with a check of what it still points at when the linker exits
cat > reloc_check.c <<EOF
#include "$top/usr.bin/ld/ld_reloc.c"

__attribute__((destructor)) static void ldtest_report_ifunc_ctx(void) {
    const char *path = getenv("LDTEST_IFUNC_CTX");
    FILE *f;

    if (path != NULL && (f = fopen(path, "w")) != NULL) {
        fprintf(f, "%s\n", ifunc_ctx == NULL ? "none" : "kept");
        fclose(f);
    }
}
EOF
inc="-idirafter $top/include -idirafter $top/sys -idirafter $top/sys/include -I$top/usr.lib/elfobj/src -I$top/usr.bin/ld"
for src in "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c "$work/reloc_check.c"; do
    case $src in
    */usr.bin/ld/ld_reloc.c) continue ;;
    esac
    obj=$(basename "$src" .c).o
    case $src in
    */usr.bin/ld/*) obj=ld_$obj ;;
    esac
    # shellcheck disable=SC2086
    ${CC:-cc} -O0 -w $inc -c -o "$obj" "$src" || {
        echo "FAIL: the linker does not build for the host"; exit 1; }
done
${CC:-cc} -o ld ./*.o || { echo "FAIL: the linker does not link for the host"; exit 1; }

cat > main.c <<'EOF'
int f(void);
void _start(void) { f(); for (;;) { } }
EOF
echo 'int f(void) { return 1; }' > lib.c
cc32="${CC:-cc} -m32 -c -ffreestanding -fno-asynchronous-unwind-tables -fno-stack-protector"
$cc32 -fno-pic -fno-pie -o main.o main.c && $cc32 -fPIC -o lib.o lib.c || { echo "SKIP: no 32-bit compiler"; exit 0; }
${CC:-cc} -m64 -c -ffreestanding -fPIC -o lib64.o lib.c || { echo "SKIP: no 64-bit compiler"; exit 0; }
mkdir a b
${GNU_LD:-ld} -m elf_x86_64 -shared -o a/libfoo.so lib64.o || { echo "FAIL: GNU ld: 64-bit library"; exit 1; }
${GNU_LD:-ld} -m elf_i386 -shared -o b/libfoo.so lib.o || { echo "FAIL: GNU ld: 32-bit library"; exit 1; }

# The library passed over is named for the -l that passed it over...
if ./ld -m elf_i386 -o p main.o -La -lfoo 2> err; then
    echo "FAIL: -lfoo with only a 64-bit libfoo.so: linked"; fail=1
elif grep -q 'note: a/libfoo.so is for another machine' err; then
    echo "ok   the -l that passed over a library for another machine names it"
else
    echo "FAIL: the -l that passed over a/libfoo.so does not name it:"; cat err; fail=1
fi

# ...and not for a later one that never looked at it.
if ${GNU_LD:-ld} -m elf_i386 -o gnu main.o -La -Lb -lfoo -Bstatic -lfoo 2> gnu.err; then
    echo "FAIL: GNU ld linked with no libfoo.a"; exit 1
fi
if sed -n '/cannot find -lfoo/,$p' gnu.err | grep -q 'a/libfoo.so'; then
    echo "FAIL: GNU ld names a/libfoo.so where it could not find -lfoo"; exit 1
fi
if ./ld -m elf_i386 -o p main.o -La -Lb -lfoo -Bstatic -lfoo 2> err; then
    echo "FAIL: -Bstatic -lfoo with no libfoo.a: linked"; fail=1
elif ! grep -q 'cannot find -lfoo' err; then
    echo "FAIL: -Bstatic -lfoo with no libfoo.a: $(head -1 err)"; fail=1
elif grep -q 'a/libfoo.so' err; then
    echo "FAIL: a later -l that found nothing names what an earlier one passed over:"; cat err; fail=1
else
    echo "ok   a later -l that found nothing names nothing an earlier one passed over"
fi
if ./ld -m elf_i386 -o p main.o -La -Lb -lfoo -l:libbar.a 2> err; then
    echo "FAIL: -l:libbar.a with no libbar.a: linked"; fail=1
elif grep -q 'a/libfoo.so' err; then
    echo "FAIL: a later -l:NAME that found nothing names what an earlier -l passed over:"; cat err; fail=1
else
    echo "ok   nor does a later -l:NAME"
fi

# A pass's context is not pointed at once the pass is over: a link, and
# one done over to settle where a script puts things.
cat > a.s <<'EOF'
.section .text.untrain,"ax"
.globl srso
srso: .byte 0xab, 0xac
.section .text.safe,"ax"
.globl safe
safe: .byte 0x5a
.text
.globl _start
_start: ret
EOF
printf 'SECTIONS { . = 0x400000; .text : { *(.text.untrain) . = srso | (1 << 2) | (1 << 8); *(.text.safe) *(.text) } }\n' > relayout.lds
${GNU_AS:-as} --32 -o a.o a.s || { echo "FAIL: as"; exit 1; }
for what in "a link" "a link done over"; do
    case $what in
    "a link") set -- -o q main.o -Lb -lfoo ;;
    *) set -- -T relayout.lds -o q a.o ;;
    esac
    rm -f ictx
    if ! LDTEST_IFUNC_CTX="$work/ictx" ./ld -m elf_i386 "$@" 2> err; then
        echo "FAIL: $what: $(head -1 err)"; fail=1
    elif [ ! -s ictx ]; then
        echo "FAIL: $what: the checked linker did not report"; fail=1
    elif [ "$(cat ictx)" != none ]; then
        echo "FAIL: $what: the linker still points at a freed pass's context when it exits"; fail=1
    else
        echo "ok   $what: no freed pass's context is pointed at"
    fi
done

exit $fail
