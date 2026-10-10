#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-string-nul-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/embedded_nul.s" <<'SRC'
.data
msg1:
    .asciz "Success\000No match\000"
msg2:
    .ascii "A\000B"
SRC

"$AS" -64 -o "$TMP/embedded_nul.o" "$TMP/embedded_nul.s"
objcopy -O binary --only-section=.data "$TMP/embedded_nul.o" "$TMP/embedded_nul.bin"

printf 'Success\000No match\000\000A\000B' > "$TMP/expected.bin"
cmp "$TMP/expected.bin" "$TMP/embedded_nul.bin"

echo "ok: embedded NUL string directives"
