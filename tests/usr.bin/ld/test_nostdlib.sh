#!/bin/sh
# -nostdlib: only the directories -L names are searched for -l.
#
# The linker did not know the option.  It said "unsupported option
# ignored" on every kernel link, the kernel Makefiles passing it, and
# went on to search its own directories for -l all the same.
#
# GNU ld is the judge: with -nostdlib a library found only in the
# default directories is not found, and one in a -L directory is.
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
	.text
	.globl _start
_start:	ret
EOF
cat > b.s <<'EOF'
	.text
	.globl helper
helper:	ret
EOF
${AS_GNU:-as} --32 -o a.o a.s || exit 1
${AS_GNU:-as} --32 -o b.o b.s || exit 1
mkdir lib && ar rcs lib/libhelper.a b.o || exit 1

# The option is known: nothing is said about it.
if ! ./ld -m elf_i386 -nostdlib -o out a.o > said 2>&1; then
    echo "FAIL: -nostdlib: $(head -1 said)"; fail=1
elif [ -s said ]; then
    echo "FAIL: -nostdlib is warned about: $(head -1 said)"; fail=1
fi

# A -L directory is still searched.
if ! ./ld -m elf_i386 -nostdlib -L lib -o out a.o -lhelper > said 2>&1; then
    echo "FAIL: -nostdlib -L lib -lhelper: $(head -1 said)"; fail=1
fi

# A library only in a default directory is not found, as with GNU ld.
# (Only where the linker finds it without -nostdlib, and GNU ld does not
# with it.)  Any archive there will do; nothing is taken from it.
lib=
for f in /usr/lib/lib*.a; do
    if [ -f "$f" ] && [ "$(head -c 7 "$f")" = '!<arch>' ]; then
        lib=${f#/usr/lib/lib}; lib=${lib%.a}; break
    fi
done
if [ -n "$lib" ] && ./ld -m elf_i386 -Bstatic -o out a.o -l$lib > /dev/null 2>&1 &&
   ! ${GNU_LD:-ld} -m elf_i386 -nostdlib -Bstatic -o gnu.out a.o -l$lib > /dev/null 2>&1; then
    if ./ld -m elf_i386 -nostdlib -Bstatic -o out a.o -l$lib > said 2>&1; then
        echo "FAIL: -nostdlib -l$lib is found in a default directory, GNU ld does not find it"
        fail=1
    elif ! grep -q "cannot find -l$lib" said; then
        echo "FAIL: -nostdlib -l$lib: $(head -1 said)"; fail=1
    fi
else
    echo "note: no archive in /usr/lib that only the default search finds; that case is not run"
fi

[ "$fail" -eq 0 ] && echo "ok: -nostdlib"
exit "$fail"
