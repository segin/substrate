#!/usr/bin/env bash
# Hard links survive copy-out and copy-in: every name of a file comes
# back as one inode holding the data, in each format, from our archives
# and from GNU cpio's (newc puts the data on the last name, odc and bin
# on every name).
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CPIO=${CPIO:-"$ROOT/usr.bin/cpio/cpio"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1

GNU=
if command -v cpio >/dev/null 2>&1 && cpio --version 2>/dev/null | grep -q GNU; then
    GNU=$(command -v cpio)
fi

fail=0
mkdir src
printf data > src/a
ln src/a src/b
ln src/a src/c
: > src/e1
ln src/e1 src/e2
LIST='src/a\nsrc/b\nsrc/c\nsrc/e1\nsrc/e2\n'

# check LABEL DIR: a, b, c one inode with "data"; e1, e2 one empty inode
check() {
    local label=$1 d=$2
    local ia ib ic ie1 ie2
    ia=$(stat -c %i "$d/src/a" 2>/dev/null); ib=$(stat -c %i "$d/src/b" 2>/dev/null)
    ic=$(stat -c %i "$d/src/c" 2>/dev/null)
    ie1=$(stat -c %i "$d/src/e1" 2>/dev/null); ie2=$(stat -c %i "$d/src/e2" 2>/dev/null)
    if [ -z "$ia" ] || [ "$ia" != "$ib" ] || [ "$ia" != "$ic" ]; then
        echo "FAIL $label: a, b, c are not one file ($ia $ib $ic)"; fail=1
    elif [ "$(cat "$d/src/a")" != data ]; then
        echo "FAIL $label: the data is \"$(cat "$d/src/a")\""; fail=1
    fi
    if [ -z "$ie1" ] || [ "$ie1" != "$ie2" ] || [ -s "$d/src/e1" ]; then
        echo "FAIL $label: e1, e2 are not one empty file ($ie1 $ie2)"; fail=1
    fi
}

for fmt in newc odc bin; do
    printf "$LIST" | "$CPIO" -o -H "$fmt" > "ours-$fmt.cpio"
    mkdir "x-$fmt"
    (cd "x-$fmt" && "$CPIO" -id < "../ours-$fmt.cpio") || { echo "FAIL ours, $fmt: status"; fail=1; }
    check "ours, $fmt" "x-$fmt"
    if [ -n "$GNU" ]; then
        printf "$LIST" | "$GNU" -o -H "$fmt" 2>/dev/null > "gnu-$fmt.cpio"
        mkdir "g-$fmt"
        (cd "g-$fmt" && "$CPIO" -id < "../gnu-$fmt.cpio") || { echo "FAIL GNU's, $fmt: status"; fail=1; }
        check "GNU's, $fmt" "g-$fmt"
    fi
done

exit $fail
