#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-main-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int main(void) { return 0; }
SRC

gcc -m64 -c -o "$TMP/main64.o" "$TMP/main.c"
gcc -m32 -c -o "$TMP/main32.o" "$TMP/main.c"

"$LDX" -m64 -r -o "$TMP/out64.o" "$TMP/main64.o"
"$LDX" -m32 -r -o "$TMP/out32.o" "$TMP/main32.o"

echo "ok: ld linked 64-bit relocatable output"
echo "ok: ld linked 32-bit relocatable output"
