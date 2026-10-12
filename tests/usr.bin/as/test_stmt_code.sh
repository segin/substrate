#!/bin/sh
# An instruction's bytes are worked out once and kept (AS-T-165).
#
# The passes after the first -- the places of labels, the text, the
# relocations -- each encoded every statement again.  What is kept has
# to be what would have been worked out: so every source of the corpus,
# and the cases where it might not be, are assembled with the bytes
# kept and with AS_NO_STMT_CODE in the environment, which has them
# worked out every time, and the two objects are to be the same file.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

both() {
    cases=$((cases + 1))
    "$AS" --"$1" -o kept.o "$2" 2> kept.err
    r1=$?
    AS_NO_STMT_CODE=1 "$AS" --"$1" -o again.o "$2" 2> again.err
    r2=$?
    if [ "$r1" != "$r2" ] || ! cmp -s kept.err again.err || { [ "$r1" = 0 ] && ! cmp -s kept.o again.o; }; then
        echo "FAIL --$1 $3: not the same with the bytes kept as without"
        fail=1
    fi
    rm -f kept.o again.o
}

for f in "$here"/corpus/*.s; do
    for m in 32 64; do
        both "$m" "$f" "$(basename "$f")"
    done
done

# A name set again is another number, written the same way.
printf '.set K, 7\n\t.text\n\tmov $K, %%eax\n.set K, 300\n\tmov $K, %%eax\n\tadd $K, %%ebx\n.set K, 1\n\tadd $K, %%ebx\n' > t.s
both 32 t.s 'a name set twice'
"$AS" --32 -o t.o t.s 2> /dev/null && objcopy -O binary --only-section=.text t.o t.bin
got=$(od -An -v -tx1 t.bin 2> /dev/null | tr -d ' \n')
cases=$((cases + 1))
[ "$got" = "b807000000b82c01000081c32c01000083c301" ] || { echo "FAIL a name set twice: $got"; fail=1; }

# The same text in another code is other bytes; a register given another
# name is another register when the name is given again.
printf '\t.code32\n\tpush %%eax\n\tinc %%eax\n\t.code16\n\tpush %%eax\n\tinc %%eax\n\t.code32\n\tinc %%eax\n' > m.s
both 32 m.s 'one text in two codes'
printf '\tjmp 1f\n\t.skip 200\n1:\tjmp 1b\n\tmov $1, %%eax\n2:\tjne 2b\n\tmov $1, %%eax\n' > b.s
both 32 b.s 'branches of both sizes'
both 64 b.s 'branches of both sizes'

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
