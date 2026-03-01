#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
CC="$ROOT/usr.bin/cc/cc"
LD="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/as-rollout-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

# Makefile/recursive integration (host + default invocation).
make -C "$ROOT/usr.bin/as" clean >/dev/null
make -C "$ROOT/usr.bin/as" NATIVE_BUILD=1 >/dev/null
make -C "$ROOT/usr.bin/as" >/dev/null

cat > "$TMP/pipe.c" <<'SRC'
int f(int x) { return x + 5; }
SRC

# cc integration: -S pipeline and -c pipeline routed through as.
"$CC" -m64 -S "$TMP/pipe.c" -o "$TMP/pipe64.s"
AS="$AS" "$CC" -m64 -c "$TMP/pipe64.s" -o "$TMP/pipe64.o"
AS="$AS" "$CC" -m64 -c "$TMP/pipe.c" -o "$TMP/pipe64_direct.o"

"$CC" -m32 -S "$TMP/pipe.c" -o "$TMP/pipe32.s"
AS="$AS" "$CC" -m32 -c "$TMP/pipe32.s" -o "$TMP/pipe32.o"

# Native host smoke checks for produced objects.
readelf -h "$TMP/pipe64.o" | grep -q "Type:[[:space:]]*REL"
readelf -h "$TMP/pipe64.o" | grep -q "ELF64"
objdump -dr "$TMP/pipe64.o" >/dev/null

readelf -h "$TMP/pipe32.o" | grep -q "Type:[[:space:]]*REL"
readelf -h "$TMP/pipe32.o" | grep -q "ELF32"
objdump -dr "$TMP/pipe32.o" >/dev/null

# Substrate-target style smoke via ld with freestanding _start objects.
cat > "$TMP/start64.s" <<'SRC'
.text
.globl _start
.type _start,@function
_start:
    mov $60, %rax
    xor %rdi, %rdi
    syscall
.size _start, .-_start
SRC
cat > "$TMP/start32.s" <<'SRC'
.text
.globl _start
.type _start,@function
_start:
    mov $1, %eax
    xor %ebx, %ebx
    int $0x80
.size _start, .-_start
SRC

"$AS" -64 -o "$TMP/start64.o" "$TMP/start64.s"
"$AS" -32 -o "$TMP/start32.o" "$TMP/start32.s"
"$LD" -m64 -o "$TMP/start64.elf" "$TMP/start64.o"
"$LD" -m32 -o "$TMP/start32.elf" "$TMP/start32.o"
readelf -h "$TMP/start64.elf" | grep -q "Type:[[:space:]]*EXEC"
readelf -h "$TMP/start64.elf" | grep -q "Machine:[[:space:]]*Advanced Micro Devices X86-64"
readelf -h "$TMP/start32.elf" | grep -q "Type:[[:space:]]*EXEC"
readelf -h "$TMP/start32.elf" | grep -Eq "Machine:[[:space:]]*Intel 80386|Machine:[[:space:]]*Advanced Micro Devices X86-64"

# ABI metadata contract checks (class/machine/type/section invariants).
for obj in "$TMP/start64.o" "$TMP/start32.o" "$TMP/pipe64.o" "$TMP/pipe32.o"; do
    readelf -h "$obj" | grep -q "Type:[[:space:]]*REL"
    readelf -S "$obj" | grep -q "\\.text"
    readelf -S "$obj" | grep -q "\\.symtab"
    readelf -S "$obj" | grep -q "\\.strtab"
    readelf -S "$obj" | grep -q "\\.shstrtab"
done

echo "ok: integration and rollout validation"
