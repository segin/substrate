#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

for mode in c11 c17 c23; do
    for src in native_abi_mix.c native_c99_struct_pass_value.c native_c99_struct_return_value.c native_c99_variadic_fnptr.c; do
        stem=$(basename "$src" .c)

        "$CC_BIN" -std="$mode" "$src" -o "$CC_TMP/cc_postc99_abi_${mode}_${stem}"
        "$CC_TMP/cc_postc99_abi_${mode}_${stem}"

        "$CC_BIN" -std="$mode" -c "$src" -o "$CC_TMP/cc_postc99_abi_${mode}_${stem}_64.o"
        file "$CC_TMP/cc_postc99_abi_${mode}_${stem}_64.o" | grep -q "ELF 64-bit"

        "$CC_BIN" -std="$mode" -m32 -c "$src" -o "$CC_TMP/cc_postc99_abi_${mode}_${stem}_32.o"
        file "$CC_TMP/cc_postc99_abi_${mode}_${stem}_32.o" | grep -q "ELF 32-bit"
    done
done
