#!/bin/sh
# --icf: one frame for the place of a class of folded functions.
#
# fn1 and fn2 of one file and fn4 of another fold into one.  Each kept its
# FDE in .eh_frame, now describing the place they share, and the filter
# that leaves out the FDE of a function that is not there (one whose code
# is in no section of the output) did not see them: .eh_frame_hdr listed
# that place three times, so its binary search table had three equal keys,
# and three FDEs covered the one address -- with different LSDAs, this is
# how the unwinder came to take the wrong one.
#
# gold is the judge: it folds fn1 and fn2 (fn4 too, without unwind
# tables; with them it leaves fn4 apart, which ours need not) and gives
# their place one FDE.  Ours must too: one FDE whose initial location is
# the place, one entry for it in .eh_frame_hdr's table, no two entries
# alike, and the program runs.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GOLD:-ld.gold} --version 2>/dev/null | grep -q 'gold'; then
    echo "skip: gold is the judge"
    exit 0
fi
mkdir "$work/tools"
${CC:-cc} -O0 -w -o "$work/tools/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > a.c <<'EOF'
extern int fn4(int);
int fn1(int x) { return x * 3 + 7; }
int fn2(int x) { return x * 3 + 7; }
int fn3(int x) { return x * 5 + 7; }
int main(void) { return !(fn1(1) == 10 && fn2(2) == 13 && fn3(1) == 12 && fn4(3) == 16); }
EOF
cat > b.c <<'EOF'
int fn4(int x) { return x * 3 + 7; }
EOF
for f in a b; do
    ${GCC:-gcc} -O2 -fno-pie -ffunction-sections -fasynchronous-unwind-tables -c -o $f.o $f.c || exit 1
done

addr() {
    nm "$1" | awk -v s="$2" '$3 == s { print $1 }'
}
# The initial locations .eh_frame_hdr's search table lists, in hex, one a line.
hdr_table() {
    set -- $(readelf -SW "$1" | sed 's/\[ */[/' | awk '$2 == ".eh_frame_hdr" { print $4, $5 }') "$1"
    [ $# = 3 ] || return
    base=$(printf '%d' "0x$1")
    off=$(printf '%d' "0x$2")
    n=$(od -An -t u4 -j $((off + 8)) -N 4 "$3" | tr -d ' ')
    i=0
    while [ "$i" -lt "$n" ]; do
        v=$(od -An -t d4 -j $((off + 12 + 8 * i)) -N 4 "$3" | tr -d ' ')
        printf '%x\n' $((base + v))
        i=$((i + 1))
    done
}
# How many FDEs begin at ADDR (hex, no 0x).
fdes_at() {
    readelf -wf "$1" | grep -c "FDE .* pc=0*$2\.\."
}

check() {   # prog, who
    a1=$(addr "$1" fn1); a2=$(addr "$1" fn2)
    if [ -z "$a1" ] || [ "$a1" != "$a2" ]; then
        echo "FAIL: $2: fn1 $a1 and fn2 $a2 are not folded"; fail=1; return
    fi
    short=$(echo "$a1" | sed 's/^0*//')
    n=$(fdes_at "$1" "$short")
    if [ "$n" != 1 ]; then
        echo "FAIL: $2: $n FDEs begin at the folded place $short"; fail=1
    fi
    hdr_table "$1" > table
    if [ ! -s table ]; then
        echo "FAIL: $2: no .eh_frame_hdr search table"; fail=1
    elif [ -n "$(sort table | uniq -d)" ]; then
        echo "FAIL: $2: .eh_frame_hdr lists $(sort table | uniq -d | tr '\n' ' ')more than once"; fail=1
    elif [ "$(grep -c "^$short\$" table)" != 1 ]; then
        echo "FAIL: $2: .eh_frame_hdr does not list the folded place $short once"; fail=1
    fi
    if ! "./$1"; then
        echo "FAIL: $2: the program does not run"; fail=1
    fi
}

${GCC:-gcc} -no-pie -fuse-ld=gold -Wl,--icf=all -o gold a.o b.o || exit 1
check gold gold
[ "$fail" = 0 ] || { echo "skip: gold does not fold as this test expects"; exit 0; }

if ! ${GCC:-gcc} -no-pie -B"$work/tools/" -Wl,--icf=all -o ours a.o b.o > said 2>&1; then
    echo "FAIL: ld --icf=all: $(grep -v collect2 said | head -1)"; exit 1
fi
check ours "ld --icf=all"

[ "$fail" = 0 ] && echo "PASS: --icf leaves one frame for a folded place, as gold does"
exit "$fail"
