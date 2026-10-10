#!/bin/sh
# reloc-table-check.sh MODULE MACHINE...
#
# The body of test_x86_reloc_core.sh, test_arm_reloc_core.sh and
# test_a64_reloc_core.sh: build test_MODULE_reloc_core.c with
# usr.bin/as/as_MODULE_reloc.c, run it once for each MACHINE, and hold the
# object it writes to what readelf says is in it.
#
# The program checks the table's numbers and its refusals, and prints the
# relocations it wrote as readelf -rW should show them: offset, name,
# symbol, addend.  The names are the ones the table's enumerators carry,
# and readelf has them from binutils, not from this tree's headers -- a
# number that is another relocation's reads back under the other's name.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
module=$1
shift
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if ! cc -Wall -Wextra -Werror -I"$top/usr.bin/as" -I"$here" -iquote "$top/include" \
        "$top/usr.bin/as/as_${module}_reloc.c" "$here/test_${module}_reloc_core.c" \
        "${ELFOBJ_A:-$top/usr.lib/elfobj/libelfobj.a}" \
        -o "$work/check" 2> "$work/build.err"; then
    echo "FAIL: the test of as_${module}_reloc.c does not build"
    head -5 "$work/build.err"
    exit 1
fi

fail=0
total=0
for machine in "$@"; do
    rm -f "$work/t.o"
    if ! "$work/check" "$machine" "$work/t.o" > "$work/want" 2> "$work/err"; then
        sed "s/^/$machine: /" "$work/err"
        fail=1
    fi
    if [ ! -s "$work/want" ] || [ ! -s "$work/t.o" ]; then
        echo "FAIL $machine: no relocations were written"
        fail=1
        continue
    fi
    # A relocation's line begins with its offset; the fields after the
    # symbol's name are the addend's sign and the addend.
    readelf -rW "$work/t.o" | awk '/^[0-9a-f]+ +[0-9a-f]+ +R_/ {
        line = $1 " " $3 " " $5
        if (NF >= 7) line = line " " $6 " " $7
        print line
    }' > "$work/got"
    if ! cmp -s "$work/want" "$work/got"; then
        echo "FAIL $machine: readelf does not find what was written (< written, > found)"
        diff "$work/want" "$work/got" | grep '^[<>]' | head -8
        fail=1
    fi
    total=$((total + $(wc -l < "$work/want")))
done

[ "$fail" -eq 0 ] && echo "ok: as_${module}_reloc.c ($total relocations read back by readelf)"
exit "$fail"
