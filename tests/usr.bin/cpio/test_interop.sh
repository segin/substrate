#!/usr/bin/env bash
# Interoperability with GNU cpio, both ways, in every format both write:
# GNU extracts our archives and we extract GNU's, the data, a symlink and
# a directory each coming out the same.  Skipped without GNU cpio.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CPIO=${CPIO:-"$ROOT/usr.bin/cpio/cpio"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1

if ! command -v cpio >/dev/null 2>&1 || ! cpio --version 2>/dev/null | grep -q GNU; then
    echo "no GNU cpio: skipped"
    exit 0
fi
GNU=$(command -v cpio)
fail=0

mkdir -p src/d
printf 'interop data\n' > src/f
head -c 70000 /dev/urandom > src/big
ln -s f src/s
LIST='src\nsrc/d\nsrc/f\nsrc/big\nsrc/s\n'

# same LABEL DIR: DIR holds what src does
same() {
    if ! cmp -s src/f "$2/src/f" || ! cmp -s src/big "$2/src/big" ||
       [ "$(readlink "$2/src/s")" != f ] || [ ! -d "$2/src/d" ]; then
        echo "FAIL $1"
        fail=1
    fi
}

for fmt in newc crc odc bin ustar tar; do
    printf "$LIST" | "$CPIO" -o -H "$fmt" --quiet > "ours.$fmt"
    mkdir "g.$fmt"
    (cd "g.$fmt" && "$GNU" -id -H "$fmt" --quiet < "../ours.$fmt" 2>/dev/null)
    same "GNU extracting ours, $fmt" "g.$fmt"

    printf "$LIST" | "$GNU" -o -H "$fmt" --quiet > "gnu.$fmt" 2>/dev/null
    mkdir "o.$fmt"
    (cd "o.$fmt" && "$CPIO" -id --quiet < "../gnu.$fmt")
    same "ours extracting GNU's, $fmt" "o.$fmt"
done
exit $fail
