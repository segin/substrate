#!/bin/sh
# The output's own symbols reached through the GOT: code compiled to go
# anywhere calls, compares and loads through a slot whether or not the
# thing turns out to be in the same output, so the slot has to be there,
# hold the address, and in a shared object or a PIE have the relocation
# that corrects it for where the output is loaded.  The linker is built
# for the host out of the tree; that such programs run is checked on the
# target, not here.
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

cd "$work" || exit 1
cat > g.c <<'EOF'
extern int absent(void) __attribute__((weak));
int value = 7;
int target(int x) { return x + value; }
int through(int (*p)(int), int v) { return p(v); }
void _start(void) {
    int (*q)(int) = target;
    int r = through(q, 1) + target(2);
    if (absent) r += absent();
    for (;;) { }
}
EOF
flags="-c -O2 -ffreestanding -fPIC -fno-plt -fno-asynchronous-unwind-tables -fno-stack-protector"
${CC:-cc} -m32 $flags -o g32.o g.c || { echo "SKIP: no 32-bit compiler"; exit 0; }
${CC:-cc} -m64 $flags -o g64.o g.c

is() {    # WHAT GOT WANT
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi
}
sec() {   # FILE SECTION FIELD(addr|off|size)
    readelf -SW "$1" | sed 's/^ *\[ *[0-9]*\] *//' |
        awk -v s="$2" -v f="$3" '$1 == s { print "0x" (f == "addr" ? $3 : f == "off" ? $4 : $5) }'
}
symval() { readelf -sW "$1" | awk -v s="$2" '$8 == s { print "0x" $2; exit }'; }
# Whether some slot of FILE's .got holds VALUE.
slot_holds() {
    off=$(sec "$1" .got off); size=$(sec "$1" .got size); w=$2
    od -An -v -tx$w -j"$(( off ))" -N"$(( size ))" "$1" | tr -s ' ' '\n' | grep -c -i "^0*$(printf '%x' "$(( $3 ))")\$"
}

for arch in 32 64; do
    m=elf_i386; w=4; [ $arch = 64 ] && { m=elf_x86_64; w=8; }
    rel=R_386_RELATIVE; [ $arch = 64 ] && rel=R_X86_64_RELATIVE

    # A program with nothing imported: the table is made for the slots.
    if ./ld -m $m -o st$arch g$arch.o 2> err; then
        is "static $arch: a slot holds the function's address" "$(( $(slot_holds st$arch $w "$(symval st$arch target)") > 0 ))" 1
        is "static $arch: a weak reference nothing defines has a slot that says 0" \
           "$(( $(slot_holds st$arch $w 0) > 0 ))" 1
        is "static $arch: nothing for a dynamic linker to do" "$(readelf -rW st$arch | grep -c 'R_')" 0
    else
        echo "FAIL static $arch: does not link: $(head -1 err)"; fail=1
    fi

    # A shared object and a PIE: the slots, and a relocation for each.
    for kind in shared pie; do
        out=$kind$arch
        if ! ./ld -m $m -$kind -o $out g$arch.o 2> err; then
            echo "FAIL $kind $arch: does not link: $(head -1 err)"; fail=1; continue
        fi
        got=$(sec $out .got addr); gsize=$(sec $out .got size)
        is "$kind $arch: a slot holds the function's address" "$(( $(slot_holds $out $w "$(symval $out target)") > 0 ))" 1
        n=0
        for a in $(readelf -rW $out | awk -v r=$rel '$3 == r { print $1 }'); do
            [ $(( 0x$a >= got && 0x$a < got + gsize )) -eq 1 ] && n=$(( n + 1 ))
        done
        is "$kind $arch: its slots are corrected for where it is loaded" "$(( n >= 1 ))" 1
        is "$kind $arch: no relocation is left without a type" "$(readelf -rW $out | grep -c 'R_.*NONE')" 0
    done
done

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
