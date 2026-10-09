#!/bin/sh
# Thread-local storage in a program: the extent a thread gets a copy of,
# what the symbols in it are worth, and how far from the thread pointer
# the code is told each variable is.  The linker is built for the host out
# of the tree and links freestanding objects; that a thread then finds its
# variables is checked on the target, not here.
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
cat > t.c <<'EOF'
__thread int first = 5;
__thread char pad = 1;
__thread long long wide __attribute__((aligned(32))) = 7;
__thread int zeroed;
int plain = 3;
int big[64];
void _start(void) { zeroed = first + (int)wide + plain + big[1] + pad; for (;;) { } }
EOF
base="-c -ffreestanding -fno-asynchronous-unwind-tables -fno-stack-protector"
${CC:-cc} -m32 $base -fno-pic -fno-pie -o le32.o t.c || { echo "SKIP: no 32-bit compiler"; exit 0; }
${CC:-cc} -m32 $base -fPIC -ftls-model=initial-exec -o ie32.o t.c
${CC:-cc} -m64 $base -fno-pic -fno-pie -o le64.o t.c
${CC:-cc} -m64 $base -fPIC -ftls-model=initial-exec -o ie64.o t.c
# The two that call __tls_get_addr, which in a program are rewritten not to.
for a in 32 64; do
    ${CC:-cc} -m$a $base -fPIC -fplt -ftls-model=global-dynamic -o gd$a.o t.c
    ${CC:-cc} -m$a $base -fPIC -fplt -ftls-model=local-dynamic -o ld$a.o t.c
done

is() {    # WHAT GOT WANT
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi
}
sec() {   # FILE SECTION FIELD(addr|size)
    readelf -SW "$1" | sed 's/^ *\[ *[0-9]*\] *//' | awk -v s="$2" -v f="$3" '$1 == s { print "0x" (f == "addr" ? $3 : $5) }'
}
symval() { readelf -sW "$1" | awk -v s="$2" '$8 == s { print "0x" $2; exit }'; }

for arch in 32 64; do
    m=elf_i386; [ $arch = 64 ] && m=elf_x86_64
    for model in le ie gd ld; do
        out=$model$arch
        if ! ./ld -m $m -o $out $out.o 2> err; then
            echo "FAIL $out: does not link: $(head -1 err)"; fail=1; continue
        fi
        td=$(sec $out .tdata addr); tds=$(sec $out .tdata size)
        tb=$(sec $out .tbss addr); tbs=$(sec $out .tbss size)
        set -- $(readelf -lW $out | awk '$1 == "TLS" { print $3, $5, $6, $NF }')
        is "$out: PT_TLS begins at .tdata"      "$(( $1 ))" "$(( td ))"
        is "$out: has .tdata for initial values" "$(( $2 ))" "$(( tds ))"
        is "$out: and ends where .tbss does"    "$(( $1 + $3 ))" "$(( tb + tbs ))"
        is "$out: .tbss directly after .tdata"  "$(( tb >= td + tds && tb < td + tds + 32 ))" 1
        is "$out: aligned as its strictest member" "$(( $4 ))" 32
        is "$out: and begins on that alignment" "$(( td % 32 ))" 0
        is "$out: .tbss takes no room in the image" \
           "$(( $(sec $out .data addr) < tb + tbs || $(sec $out .bss addr) < tb + 4096 ))" 1
        is "$out: a symbol's value is its place in the copy" "$(( $(symval $out first) ))" 0
        is "$out: the aligned one on its alignment" "$(( $(symval $out wide) % 32 ))" 0
        is "$out: the zeroed one after the initial values" "$(( $(symval $out zeroed) >= tds ))" 1
        # The copy ends at the thread pointer, rounded up to the alignment:
        # the distances the code has are those back from there.
        blk=$(( ($3 + 31) / 32 * 32 ))
        v=$(( (1 << 32) - blk + $(symval $out first) ))
        # (general-dynamic on i386 becomes a subtraction of the distance)
        [ $out = gd32 ] && v=$(( blk - $(symval $out first) ))
        bytes=$(printf '%02x %02x %02x %02x' $(( v & 255 )) $(( (v >> 8) & 255 )) $(( (v >> 16) & 255 )) $(( v >> 24 )))
        is "$out: the code has 'first' at -$blk from the thread pointer" \
           "$(( $(objdump -d --insn-width=16 -j .text $out | grep -c "$bytes") > 0 ))" 1
        is "$out: nothing calls __tls_get_addr" "$(objdump -d -j .text $out | grep 'call' | grep -c -v 'get_pc_thunk')" 0
        is "$out: which is not left undefined" "$(readelf -sW $out | grep -c 'GLOBAL.*UND .*tls_get_addr')" 0
        is "$out: no dynamic relocations"       "$(readelf -rW $out | grep -c 'R_')" 0
    done
done

# A shared object does not know which module it will be or how far its
# storage is from the thread pointer: the dynamic linker says, in GOT
# entries the code reads, and each of those has a relocation.
for arch in 32 64; do
    m=elf_i386; mod=R_386_TLS_DTPMOD32; tp=R_386_TLS_TPOFF
    [ $arch = 64 ] && { m=elf_x86_64; mod=R_X86_64_DTPMOD64; tp=R_X86_64_TPOFF64; }
    for model in gd ld ie; do
        out=lib$model$arch.so
        if ! ./ld -m $m -shared -o $out $model$arch.o 2> err; then
            echo "FAIL $out: does not link: $(head -1 err)"; fail=1; continue
        fi
        nmod=$(readelf -rW $out | grep -c "$mod")
        ntp=$(readelf -rW $out | grep -c "$tp")
        case $model in
        gd) is "$out: a module entry for each variable" "$nmod $ntp" "4 0" ;;
        ld) is "$out: one module entry for them all"    "$nmod $ntp" "1 0" ;;
        ie) is "$out: a distance for each variable"     "$nmod $ntp" "0 4"
            is "$out: marked as needing its storage laid out at the start" \
               "$(readelf -d $out | grep -c 'STATIC_TLS')" 1 ;;
        esac
        is "$out: PT_TLS"                       "$(readelf -lW $out | grep -c '^ *TLS ')" 1
        is "$out: a symbol's value is its place in the copy" "$(( $(symval $out first) ))" 0
    done
    if ./ld -m $m -shared -o bad$arch.so le$arch.o 2> err; then
        echo "FAIL local-exec code in a shared object: linked"; fail=1
    else
        is "local-exec code in a shared object ($arch) is an error that says so" \
           "$(grep -c 'cannot be used in a shared object' err)" 1
    fi
done

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
