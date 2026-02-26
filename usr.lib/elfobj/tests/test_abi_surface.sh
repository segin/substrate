#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
HEADER="$TOP/include/elfobj.h"
EXPECTED="$SCRIPT_DIR/abi_api_v1.txt"
ACTUAL="$SCRIPT_DIR/.abi_api_actual.txt"

awk '
    {
        if (match($0, /elf_[A-Za-z0-9_]+[[:space:]]*\(/)) {
            name = substr($0, RSTART, RLENGTH)
            sub(/[[:space:]]*\(.*/, "", name)
            print name
        }
    }
' "$HEADER" | sort -u > "$ACTUAL"

if ! diff -u "$EXPECTED" "$ACTUAL"; then
    echo "ABI surface mismatch for include/elfobj.h" >&2
    rm -f "$ACTUAL"
    exit 1
fi

rm -f "$ACTUAL"
exit 0
