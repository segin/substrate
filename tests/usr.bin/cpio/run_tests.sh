#!/bin/sh
# Run every test_*.sh here against one cpio binary (the first argument,
# or usr.bin/cpio/cpio), each in its own shell.  Every script runs even
# after a failure; the exit status is 1 if any of them failed.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
CPIO=${1:-"$ROOT/usr.bin/cpio/cpio"}
case $CPIO in
/*) ;;
*) CPIO=$(pwd)/$CPIO ;;
esac
export CPIO

failed=
n=0
for t in "$HERE"/test_*.sh; do
    n=$((n + 1))
    name=$(basename "$t")
    if out=$(bash "$t" 2>&1); then
        echo "PASS $name"
    else
        echo "FAIL $name"
        printf '%s\n' "$out" | sed 's/^/    /'
        failed="$failed $name"
    fi
done

if [ -n "$failed" ]; then
    echo "cpio: failed:$failed"
    exit 1
fi
echo "cpio: ok ($n scripts)"
