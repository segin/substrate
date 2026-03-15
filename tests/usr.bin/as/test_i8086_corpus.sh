#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
CORPUS_DIR="$ROOT/tests/usr.bin/as/corpus"
TMP=${TMPDIR:-/tmp}/as-i8086-corpus-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

check_corpus() {
    name=$1
    src=$2
    obj="$TMP/$name.o"

    "$AS" --32 -o "$obj" "$src"
    objdump -dr "$obj" >/dev/null
}

check_corpus valid "$CORPUS_DIR/i8086_gas_all_valid_assembles.s"
check_corpus opcodes "$CORPUS_DIR/i8086_gas_all_opcodes_assembles.s"

echo "ok: i8086 corpus assembly"
