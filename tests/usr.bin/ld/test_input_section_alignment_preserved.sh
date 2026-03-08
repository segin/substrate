#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
LD_BIN=${LD_BIN:-"$ROOT/usr.bin/ld/ld"}
AS_BIN=${AS_BIN:-/usr/bin/as}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/align.c" <<'EOF'
#include <stdalign.h>
#include <stdint.h>

static char lead;
static alignas(8) char aligned_char;
static alignas(8) int aligned_int;

int main(void) {
    return ((uintptr_t)&aligned_char % 8u) | ((uintptr_t)&aligned_int % 8u);
}
EOF

AS="$AS_BIN" LD="$LD_BIN" "$CC_BIN" "$TMP/align.c" -o "$TMP/align"
"$TMP/align"

char_addr=$(nm -n "$TMP/align" | awk '/ aligned_char$/ { print $1; exit }')
int_addr=$(nm -n "$TMP/align" | awk '/ aligned_int$/ { print $1; exit }')
[ -n "$char_addr" ]
[ -n "$int_addr" ]
[ $((0x$char_addr % 8)) -eq 0 ]
[ $((0x$int_addr % 8)) -eq 0 ]
