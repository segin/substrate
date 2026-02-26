#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

for mode in c11 c17 c23; do
    "$CC_BIN" -std="$mode" native_abi_mix.c -o "/tmp/cc_postc99_abi_${mode}"
    "/tmp/cc_postc99_abi_${mode}"

    "$CC_BIN" -std="$mode" -c native_abi_mix.c -o "/tmp/cc_postc99_abi_${mode}_64.o"
    file "/tmp/cc_postc99_abi_${mode}_64.o" | grep -q "ELF 64-bit"

    "$CC_BIN" -std="$mode" -m32 -c native_abi_mix.c -o "/tmp/cc_postc99_abi_${mode}_32.o"
    file "/tmp/cc_postc99_abi_${mode}_32.o" | grep -q "ELF 32-bit"
done
