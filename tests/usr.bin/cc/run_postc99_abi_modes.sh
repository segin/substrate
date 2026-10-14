#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

for mode in c11 c17 c23; do
    for src in native_abi_mix.c native_c99_struct_pass_value.c native_c99_struct_return_value.c native_c99_variadic_fnptr.c; do
        stem=$(basename "$src" .c)

        "$CC_BIN" -std="$mode" "$src" -o "/tmp/cc_postc99_abi_${mode}_${stem}"
        "/tmp/cc_postc99_abi_${mode}_${stem}"

        "$CC_BIN" -std="$mode" -c "$src" -o "/tmp/cc_postc99_abi_${mode}_${stem}_64.o"
        file "/tmp/cc_postc99_abi_${mode}_${stem}_64.o" | grep -q "ELF 64-bit"

        "$CC_BIN" -std="$mode" -m32 -c "$src" -o "/tmp/cc_postc99_abi_${mode}_${stem}_32.o"
        file "/tmp/cc_postc99_abi_${mode}_${stem}_32.o" | grep -q "ELF 32-bit"
    done
done
