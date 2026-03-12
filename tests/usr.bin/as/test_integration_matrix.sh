#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
SUB_LD="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/as-integration-matrix-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

# 8b.1 and 8b.2: object metadata + as/ld integration flows.
"$ROOT/tests/usr.bin/as/test_integration_rollout.sh"
"$ROOT/tests/usr.bin/as/test_main_32_64.sh"

# 8b.3: cross-arch rejection (x86 source under ARM/AArch64 mode).
cat > "$TMP/x86_only.s" <<'SRC'
.text
.globl x86_only
.type x86_only,@function
x86_only:
    mov $1, %eax
    ret
.size x86_only, .-x86_only
SRC

if "$AS" -march=armv7-a -o "$TMP/x86_as_arm.o" "$TMP/x86_only.s" >"$TMP/arm.out" 2>"$TMP/arm.err"; then
    echo "expected x86 source under -march=armv7-a to fail"
    exit 1
fi
if "$AS" -march=armv8-a -o "$TMP/x86_as_a64.o" "$TMP/x86_only.s" >"$TMP/a64.out" 2>"$TMP/a64.err"; then
    echo "expected x86 source under -march=armv8-a to fail"
    exit 1
fi
grep -Eqi "error|invalid|unsupported|unknown|arm|aarch64" "$TMP/arm.err"
grep -Eqi "error|invalid|unsupported|unknown|arm|aarch64" "$TMP/a64.err"

# 8b.4: objdump round-trip with byte-identical .text output.
cat > "$TMP/roundtrip.s" <<'SRC'
.text
.globl roundtrip
.type roundtrip,@function
roundtrip:
    xor %eax, %eax
    xor %edx, %edx
    ret
.size roundtrip, .-roundtrip
SRC

"$AS" -64 -o "$TMP/roundtrip_a.o" "$TMP/roundtrip.s"
objdump -d --no-addresses --no-show-raw-insn "$TMP/roundtrip_a.o" \
| awk '/^[[:space:]]+[[:alnum:]_.].*$/ { sub(/^[[:space:]]+/, ""); print }' > "$TMP/roundtrip.insn"

cat > "$TMP/roundtrip_b.s" <<'SRC'
.text
.globl roundtrip
.type roundtrip,@function
roundtrip:
SRC
cat "$TMP/roundtrip.insn" >> "$TMP/roundtrip_b.s"
printf '.size roundtrip, .-roundtrip\n' >> "$TMP/roundtrip_b.s"

"$AS" -64 -o "$TMP/roundtrip_b.o" "$TMP/roundtrip_b.s"
objcopy -O binary --only-section=.text "$TMP/roundtrip_a.o" "$TMP/roundtrip_a.text"
objcopy -O binary --only-section=.text "$TMP/roundtrip_b.o" "$TMP/roundtrip_b.text"
cmp "$TMP/roundtrip_a.text" "$TMP/roundtrip_b.text"

# 8b.5: GNU ld <-> Substrate as compatibility in both directions.
"$ROOT/tests/usr.bin/as/test_gnu_compat_surface.sh"
"$ROOT/tests/usr.bin/as/test_intel_dual_syntax.sh"
"$ROOT/tests/usr.bin/as/test_i8086_corpus.sh"
"$ROOT/tests/usr.bin/as/test_x86_32_corpus_intel_roundtrip.sh"
"$ROOT/tests/usr.bin/as/test_x86_64_corpus_intel_roundtrip.sh"
"$ROOT/tests/usr.bin/as/test_output_binary.sh"
gcc -c -x assembler -m64 -o "$TMP/gnu_obj.o" "$TMP/roundtrip.s"
"$SUB_LD" -m64 -r -o "$TMP/sub_ld_on_gnu.o" "$TMP/gnu_obj.o"
ld -r -o "$TMP/gnu_ld_on_sub.o" "$TMP/roundtrip_a.o"

readelf -h "$TMP/sub_ld_on_gnu.o" | grep -q "Type:[[:space:]]*REL"
readelf -h "$TMP/gnu_ld_on_sub.o" | grep -q "Type:[[:space:]]*REL"

echo "ok: integration matrix"
