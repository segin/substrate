#!/bin/sh
# The specification's table of what is not implemented, against the linker.
#
# usr.bin/ld/SPEC.md §0 lists the features the specification asks for and
# the code does not do, so that nothing is built on them.  The table went
# stale: it said --build-id made no build ID, --version-script was not
# accepted, the archive index was skipped, SORT_* changed nothing and
# .gnu.hash had one bucket, when the linker had come to do all five.  A
# reader believing it would work around features that are there.
#
# Each feature is tried here, and GNU ld is the judge of what doing it
# looks like; one the linker does must not be in the table as not done.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
spec="$top/usr.bin/ld/SPEC.md"
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

# the rows of §0's table, without its header
awk '/^\| Specified/ { t = 1; next } t && !/^\|/ { exit } t && !/^\|--/ { print }' "$spec" > table
if [ ! -s table ]; then
    echo "FAIL: no table of what is not implemented in SPEC.md §0"; exit 1
fi

# done NAME ROW-PATTERN: the linker does NAME, so no row may match
done_() {
    if grep -q -- "$2" table; then
        echo "FAIL: SPEC.md §0 says $1 is not implemented: $(grep -- "$2" table | head -1)"
        fail=1
    else
        echo "ok   $1 is done and not listed as not done"
    fi
}
# not_done NAME: the probe found NAME not done; the table may say so
not_done() {
    echo "FAIL: the linker does not do $1"
    fail=1
}

# --build-id: a note, as GNU ld makes, of 20 bytes
printf 'int f(void) { return 1; }\nint g(void) { return f() + 1; }\n' > a.c
${GCC:-gcc} -O2 -fPIC -c -o a.o a.c || exit 1
"${GNU_LD:-ld}" -shared --build-id -o gnu.so a.o || exit 1
./ld -m elf_x86_64 -shared --build-id -o ours.so a.o > said 2>&1
if readelf -nW ours.so 2>/dev/null | grep -q 'NT_GNU_BUILD_ID' &&
   [ "$(readelf -nW ours.so 2>/dev/null | awk '/NT_GNU_BUILD_ID/ { print $2 }')" = \
     "$(readelf -nW gnu.so 2>/dev/null | awk '/NT_GNU_BUILD_ID/ { print $2 }')" ]; then
    done_ "--build-id" '^| `--build-id`'
else
    not_done "--build-id: $(head -1 said)"
fi

# --version-script: what it makes local leaves the dynamic symbols
printf '{ global: g; local: *; };\n' > v.map
"${GNU_LD:-ld}" -shared --version-script v.map -o gnu.so a.o || exit 1
./ld -m elf_x86_64 -shared --version-script v.map -o ours.so a.o > said 2>&1
dyn() { nm -D --defined-only "$1" 2>/dev/null | awk '{ print $3 }' | sort | tr '\n' ' '; }
if [ -f ours.so ] && [ "$(dyn ours.so)" = "$(dyn gnu.so)" ]; then
    done_ "--version-script" '^| `--version-script`'
else
    not_done "--version-script: $(head -1 said) [$(dyn ours.so 2>/dev/null)] against [$(dyn gnu.so)]"
fi

# The archive's index: an index that says the one member defines a symbol
# it does not is believed, as GNU ld believes it, so the member is not read
# and the symbol it really defines is not found.
printf 'int fa(void) { return 1; }\n' > m.c
printf 'int fa(void); int _start(void) { return fa(); }\n' > s.c
${GCC:-gcc} -O2 -fno-pic -c -o m.o m.c && ${GCC:-gcc} -O2 -fno-pic -c -o s.o s.c || exit 1
rm -f true.a stale.a
ar rcs true.a m.o || exit 1
cp true.a stale.a
at=$(grep -abo 'fa' stale.a | head -1 | cut -d: -f1)
printf 'fz' | dd of=stale.a bs=1 seek="$at" conv=notrunc 2>/dev/null
# with the true index both link, so a failure below is the index's doing
ctl=0
"${GNU_LD:-ld}" -static -o gnu.out s.o true.a > /dev/null 2>&1 &&
    ./ld -m elf_x86_64 -static -o ours.out s.o true.a > /dev/null 2>&1 && ctl=1
gnu_ok=0; ours_ok=0
"${GNU_LD:-ld}" -static -o gnu.out s.o stale.a > /dev/null 2>&1 && gnu_ok=1
./ld -m elf_x86_64 -static -o ours.out s.o stale.a > /dev/null 2>&1 && ours_ok=1
if [ "$ctl" -eq 1 ] && [ "$gnu_ok" -eq 0 ] && [ "$ours_ok" -eq 0 ]; then
    done_ "archive extraction by the symbol index" '^| Symbol-index-driven archive extraction'
else
    not_done "archive extraction by the symbol index (true index linked: $ctl; stale index linked by GNU ld: $gnu_ok, ours: $ours_ok)"
fi

# SORT_BY_NAME: input sections in the order of their names
printf '__attribute__((section(".text.b"))) int b(void) { return 2; }\n' > sb.c
printf '__attribute__((section(".text.a"))) int a(void) { return 1; }\n' > sa.c
${GCC:-gcc} -O2 -fno-pic -c -o sb.o sb.c && ${GCC:-gcc} -O2 -fno-pic -c -o sa.o sa.c || exit 1
printf 'SECTIONS { .text : { *(SORT_BY_NAME(.text.*)) } }\n' > sort.ld
order() { nm -n "$1" | awk '$3 == "a" || $3 == "b" { print $3 }' | tr -d '\n'; }
"${GNU_LD:-ld}" -T sort.ld -o gnu.out sb.o sa.o > /dev/null 2>&1 || exit 1
./ld -m elf_x86_64 -T sort.ld -o ours.out sb.o sa.o > said 2>&1
if [ -f ours.out ] && [ "$(order ours.out)" = "$(order gnu.out)" ] && [ "$(order gnu.out)" = ab ]; then
    done_ "SORT_* in scripts" '^| `SORT_\*`'
else
    not_done "SORT_BY_NAME: $(head -1 said) [$(order ours.out 2>/dev/null)] against [$(order gnu.out)]"
fi

# .gnu.hash: more than one bucket for many symbols
: > many.c
i=0
while [ "$i" -lt 64 ]; do
    printf 'int fn%d(void) { return %d; }\n' "$i" "$i" >> many.c
    i=$((i + 1))
done
${GCC:-gcc} -O2 -fPIC -c -o many.o many.c || exit 1
./ld -m elf_x86_64 -shared --hash-style=gnu -o many.so many.o > said 2>&1
buckets() {
    off=$(readelf -SW "$1" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk '$1 == ".gnu.hash" { print $4 }')
    [ -n "$off" ] || return
    od -An -tu4 -j $((0x$off)) -N4 "$1" | tr -d ' '
}
n=$(buckets many.so)
if [ -n "$n" ] && [ "$n" -gt 1 ]; then
    done_ ".gnu.hash buckets" '^| `\.gnu\.hash`'
else
    not_done ".gnu.hash with more than one bucket: $(head -1 said) [${n:-none}]"
fi

# The linker's ARCHITECTURE.md said the same of archives and .gnu.hash.
arch="$top/usr.bin/ld/ARCHITECTURE.md"
if grep -q 'reading every member\|written with one bucket' "$arch"; then
    echo "FAIL: ARCHITECTURE.md: $(grep 'reading every member\|written with one bucket' "$arch" | head -1)"
    fail=1
else
    echo "ok   ARCHITECTURE.md does not say the index is skipped or .gnu.hash has one bucket"
fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
