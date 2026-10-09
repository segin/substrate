#!/bin/sh
# Archives as input: one that is not as an archive should be is refused,
# with the reason, and nothing is read past the end of the file.  The
# linker is built for the host with the address sanitizer where the
# compiler has one, so that reading past the end is a failure and not luck.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0
export ASAN_OPTIONS=detect_leaks=0

build() {
    ${CC:-cc} -O0 -w "$@" -o "$work/ld" \
        -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
        -I"$top/usr.lib/elfobj/src" "$top/usr.bin/ld/ld.c" "$top"/usr.lib/elfobj/src/*.c 2>/dev/null
}
build -fsanitize=address || build || { echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
printf 'int helper(int x) { return x + 1; }\n' > h.c
printf 'int helper(int); void _start(void) { helper(1); for (;;) { } }\n' > s.c
${CC:-cc} -m32 -c -ffreestanding -fno-pic -fno-pie -o h.o h.c &&
${CC:-cc} -m32 -c -ffreestanding -fno-pic -fno-pie -o s.o s.c || { echo "SKIP: no 32-bit compiler"; exit 0; }

# member NAME SIZE-FIELD DATA: one archive member, header and all
member() {
    printf '%-16s%-12s%-6s%-6s%-8s%-10s`\n%s' "$1" 0 0 0 644 "$2" "$3"
}

ok() {    # WHAT ARCHIVE
    if ./ld -m elf_i386 -o out s.o "$2" > err 2>&1; then echo "ok   $1"
    else echo "FAIL $1: $(head -1 err)"; fail=1; fi
}
bad() {   # WHAT ARCHIVE MESSAGE
    if ./ld -m elf_i386 -o out s.o "$2" > err 2>&1; then echo "FAIL $1: linked"; fail=1
    elif grep -q "AddressSanitizer" err; then echo "FAIL $1: read outside the file"; fail=1
    elif grep -q -- "$3" err; then echo "ok   $1"
    else echo "FAIL $1: wanted '$3', got: $(head -1 err)"; fail=1; fi
}

ar rc good.a h.o
ok  "an archive"                       good.a
cp good.a libgood.bin
ok  "an archive by any other name"     libgood.bin
mkdir thin && cp h.o thin/h.o && (cd thin && ar rcT t.a h.o)
ok  "a thin archive"                   thin/t.a

head -c "$(( $(wc -c < good.a) - 40 ))" good.a > short.a
bad "a member cut short"               short.a   "longer than what is left of the file"
head -c 40 good.a > header.a
bad "a header cut short"               header.a  "ends in the middle of a member header"
{ printf '!<arch>\n'; member '#1/9000' 9100 short; } > bsdname.a
bad "a name longer than the file"      bsdname.a "name cannot be read"
{ printf '!<arch>\n'; member 'x.o/' lots ''; } > badsize.a
bad "a size that is not a number"      badsize.a "size is not a number"
{ printf '!<arch>\n'; member 'x.o/' 4294967304 "$(printf '\177ELF1234')"; } > wrap.a
bad "a size past four gigabytes"       wrap.a    "longer than what is left of the file"
{ printf '!<arch>\n'; head -c 100 /dev/zero | tr '\0' x; } > garbage.a
bad "no member header"                 garbage.a "does not end as one does"

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
