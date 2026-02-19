#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as.x86"
LD="$ROOT/usr.bin/ld/ld.x86"
TMP=${TMPDIR:-/tmp}/asld-main-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int main(void) {
    return 0;
}
SRC

# Generate assembly with GCC for both ABIs.
gcc -m64 -O2 -fomit-frame-pointer -fno-asynchronous-unwind-tables -fno-stack-protector \
    -S -o "$TMP/main64.s" "$TMP/main.c"

gcc -m32 -O2 -fomit-frame-pointer -fno-asynchronous-unwind-tables -fno-stack-protector \
    -S -o "$TMP/main32.s" "$TMP/main.c"

# Assemble through as.x86.
"$AS" -64 -o "$TMP/main64.o" "$TMP/main64.s"
"$AS" -32 -o "$TMP/main32.o" "$TMP/main32.s"

# Relocatable link smoke tests through ld.x86.
"$LD" -r -o "$TMP/main64.r.o" "$TMP/main64.o"
"$LD" -r -o "$TMP/main32.r.o" "$TMP/main32.o"

# Final linked 64-bit binary strip test requested by user.
gcc -m64 -s -o "$TMP/main64.bin" "$TMP/main.c"

echo "ok: generated and assembled 32/64-bit GCC main() assembly"
echo "ok: linked relocatable outputs via ld.x86"
echo "ok: validated gcc -m64 -s final link"
