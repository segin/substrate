#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-cpp-path-$$
mkdir -p "$TMP/bin"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/fail_cpp.sh" <<'SRC'
#!/bin/sh
echo "unexpected cpp invocation" >&2
exit 99
SRC
chmod +x "$TMP/fail_cpp.sh"

cat > "$TMP/plain.s" <<'SRC'
.text
.globl plain
.type plain,@function
plain:
    mov $7, %eax
    ret
.size plain, .-plain
SRC

AS_CPP="$TMP/fail_cpp.sh" "$AS" -32 -o "$TMP/plain.o" "$TMP/plain.s"

cat > "$TMP/bin/cpp" <<'SRC'
#!/bin/sh
echo cpp >> "$AS_CPP_LOG"
exec /usr/bin/cpp "$@"
SRC
chmod +x "$TMP/bin/cpp"

cat > "$TMP/bin/cc" <<'SRC'
#!/bin/sh
echo cc >> "$AS_CPP_LOG"
exit 97
SRC
chmod +x "$TMP/bin/cc"

cat > "$TMP/bin/gcc" <<'SRC'
#!/bin/sh
echo gcc >> "$AS_CPP_LOG"
exit 98
SRC
chmod +x "$TMP/bin/gcc"

cat > "$TMP/macro.S" <<'SRC'
#define IMM 42
.text
.globl macro
.type macro,@function
macro:
    mov $IMM, %eax
    ret
.size macro, .-macro
SRC

: > "$TMP/cpp.log"
AS_CPP="$TMP/bin/cpp" AS_CPP_LOG="$TMP/cpp.log" PATH="$TMP/bin:$PATH" "$AS" -32 -o "$TMP/macro.o" "$TMP/macro.S"

grep -qx 'cpp' "$TMP/cpp.log"
if grep -Eq '^(cc|gcc)$' "$TMP/cpp.log"; then
    echo "assembler fell back to cc/gcc instead of cpp" >&2
    exit 1
fi

objdump -dr "$TMP/macro.o" > "$TMP/macro.dump"
grep -Eq '\$0x2a|,42' "$TMP/macro.dump"

echo "ok: cpp path and no cc recursion"
