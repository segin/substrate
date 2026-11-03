#!/bin/sh
# Run clang's dead-store checker over every libelfobj source file.  A
# value that is stored and then overwritten before anything reads it is
# at best leftover code and at worst a computation whose result was meant
# to be used; either way the library should have none.  Skips (exit 0)
# when no clang is installed.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
SRC="$TOP/usr.lib/elfobj/src"
CLANG=${CLANG:-clang}

if ! command -v "$CLANG" >/dev/null 2>&1; then
    echo "SKIP: $CLANG not found; dead-store check not run"
    exit 0
fi

status=0
for f in "$SRC"/*.c; do
    out=$("$CLANG" --analyze \
        -Xanalyzer -analyzer-checker=deadcode.DeadStores \
        -I"$SRC" -idirafter "$TOP/include" -idirafter "$TOP/sys" \
        -idirafter "$TOP/sys/include" "$f" -o /dev/null 2>&1) || {
        printf '%s\n' "$out" >&2
        echo "FAIL: clang could not analyze $f" >&2
        status=1
        continue
    }
    if printf '%s\n' "$out" | grep -q 'deadcode.DeadStores'; then
        printf '%s\n' "$out" | grep 'deadcode.DeadStores' >&2
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "FAIL: libelfobj has dead stores" >&2
    exit 1
fi
echo "PASS: no dead stores in libelfobj"
exit 0
