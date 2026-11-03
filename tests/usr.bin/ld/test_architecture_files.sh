#!/bin/sh
# The linker's ARCHITECTURE.md, against the files the linker is made of.
#
# usr.bin/ld/ARCHITECTURE.md lists each source file in its project tree
# (§1) and says in §6 how many translation units there are.  Files were
# added (ld_buildid.c, ld_version.c) without being listed, and the count
# stayed at sixteen; a reader looking for where build IDs or version
# scripts are made was not told.
#
# Every usr.bin/ld/*.c must be named in the tree, and the count in §6
# must be the number of them.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
arch="$top/usr.bin/ld/ARCHITECTURE.md"
fail=0

# The tree is the first fenced block of §1.
tree=$(sed -n '/^## 1\./,/^## 2\./p' "$arch" | sed -n '/^```/,/^```/p')

n=0
for f in "$top"/usr.bin/ld/*.c; do
    b=$(basename "$f")
    n=$((n + 1))
    if printf '%s\n' "$tree" | grep -q "── $b "; then
        echo "ok   $b is in the project tree"
    else
        echo "FAIL: $b is not in ARCHITECTURE.md's project tree"
        fail=1
    fi
done

# The tree must not name a file that is not there.
for b in $(printf '%s\n' "$tree" | sed -n 's/^.*── \(ld[a-z_]*\.c\) .*$/\1/p'); do
    if [ ! -f "$top/usr.bin/ld/$b" ]; then
        echo "FAIL: the project tree names $b, which does not exist"
        fail=1
    fi
done

words="zero one two three four five six seven eight nine ten eleven twelve
thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty
twenty-one twenty-two twenty-three twenty-four twenty-five"
want=$(echo $words | tr ' ' '\n' | sed -n "$((n + 1))p")
said=$(sed -n 's/^.*one program in \([a-z-]*\) translation units.*$/\1/p' "$arch")
if [ -n "$want" ] && [ "$said" = "$want" ]; then
    echo "ok   §6 says $said translation units"
else
    echo "FAIL: §6 says '${said:-nothing}' translation units, there are $n"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
