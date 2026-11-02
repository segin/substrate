#!/bin/sh
# The symbols GNU ld's default script defines: __bss_start, _edata and
# _end always; etext, _etext, __etext, edata, end and __executable_start
# where something refers to them and no input defines them.
#
# The linker defined none of them: a reference to etext or end was
# undefined, and -E did not export __bss_start, _edata or _end as GNU
# ld's link does.
#
# GNU ld is the judge: for programs that refer to them and that do not,
# with zeros and without, x86-64 and i386, a PIE and a shared object, and
# an input that defines one itself, each such symbol is there or not as
# in GNU ld's output, with GNU ld's value, binding and section.
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
# refers: etext, edata, end, __executable_start; and zeros
cat > refers.s <<'EOF'
	.text
	.globl _start
_start:	lea etext(%rip), %rax
	lea edata(%rip), %rax
	lea end(%rip), %rax
	lea __executable_start(%rip), %rax
	ret
	.section .rodata
	.long 1
	.data
	.long 1
	.bss
	.zero 64
EOF
# refers to none, has no zeros
cat > plain.s <<'EOF'
	.text
	.globl _start
_start:	ret
	.data
	.long 1
EOF
# defines _end itself
cat > own.s <<'EOF'
	.text
	.globl _start
_start:	ret
	.data
	.globl _end
_end:	.long 1
EOF
for f in refers plain own; do
    ${AS_GNU:-as} --64 -o $f.o $f.s || exit 1
done
sed -e 's/(%rip)//' -e 's/%rax/%eax/' refers.s > refers32.s
${AS_GNU:-as} --32 -o refers32.o refers32.s || exit 1

# Each of the symbols OBJ has: name, binding, the name of its section and
# where in it (the value less the section's address: the two linkers put
# the sections at different addresses), or its value if it is absolute.
syms() {
    readelf -SW "$1" | sed -n 's/^ *\[ *\([0-9]*\)\] \([^ ]*\) *[^ ]* *\([0-9a-f]*\).*/\1 \2 \3/p' > secnames
    readelf -sW "$1" | awk 'NR > 3 && $8 ~ /^(__bss_start|_edata|_end|edata|end|etext|_etext|__etext|__executable_start)$/ {
        print $8, $2, $5, $7 }' | sort -u | while read -r n v b s; do
        # (__executable_start is the image's first address, below the first section by the headers,
        # whose size is the linker's own: its value is what is compared)
        case $n:$s in
            *:ABS|*:UND|__executable_start:*) echo "$n $b $v" ;;
            *) awk -v i="$s" -v n="$n" -v b="$b" -v v="$v" '$1 == i {
                   printf "%s %s %s %d\n", n, b, $2, strtonum("0x" v) - strtonum("0x" $3) }' secnames ;;
        esac
    done
}

check() {
    m=$1; shift
    "${GNU_LD:-ld}" -m "$m" -o gnu.out "$@" 2>/dev/null || { echo "skip: GNU ld $*"; return; }
    if ! ./ld -m "$m" -o ours.out "$@" > said 2>&1; then
        echo "FAIL: ld -m $m $*: $(head -1 said)"; fail=1; return
    fi
    syms gnu.out > want
    syms ours.out > have
    if ! cmp -s want have; then
        echo "FAIL: ld -m $m $*:"
        diff want have | sed 's/^/    /'
        fail=1
    fi
}

check elf_x86_64 refers.o
if [ "$(wc -l < want)" -ne 7 ]; then
    echo "FAIL: GNU ld's program that refers to them has $(wc -l < want) of the symbols, not 7"; fail=1
fi
check elf_x86_64 plain.o
check elf_x86_64 own.o
check elf_x86_64 -pie -dynamic-linker /lib64/ld-linux-x86-64.so.2 refers.o
check elf_x86_64 -shared plain.o
check elf_i386 refers32.o

# a script's link: the script says what it defines, and these are not made
printf 'SECTIONS { . = 0x400000; .text : { *(.text) } .data : { *(.data) } }\n' > s.lds
if ./ld -m elf_x86_64 -T s.lds -o script.out plain.o > said 2>&1; then
    n=$(readelf -sW script.out | grep -c -E ' (__bss_start|_edata|_end)$')
    if [ "$n" -ne 0 ]; then
        echo "FAIL: a script's link has $n of the default script's symbols"; fail=1
    fi
else
    echo "FAIL: ld -T s.lds: $(head -1 said)"; fail=1
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_linker_symbols"
fi
exit $fail
