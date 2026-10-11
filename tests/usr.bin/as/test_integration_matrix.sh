#!/bin/sh
# Three things at the edges of the assembler:
#
#   - x86 source given under an ARM or AArch64 -march is refused, and the
#     message says something;
#   - what objdump prints of an object, assembled again, is the same
#     bytes;
#   - an object of this assembler's is one GNU ld takes, and one of GNU
#     as's is one substrate's linker takes.
#
# This test used to run seven other tests of the suite as well, which
# the suite runs itself, and one of those built in the source directory.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS=${AS:-"$ROOT/usr.bin/as/as"}
TMP=${TMPDIR:-/tmp}/as-integration-matrix-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/x86_only.s" <<'SRC'
.text
.globl x86_only
.type x86_only,@function
x86_only:
    mov $1, %eax
    ret
.size x86_only, .-x86_only
SRC
for march in armv7-a armv8-a; do
    if "$AS" -march=$march -o "$TMP/x86_as_$march.o" "$TMP/x86_only.s" > "$TMP/out" 2> "$TMP/err"; then
        echo "FAIL: x86 source was assembled under -march=$march"
        exit 1
    fi
    grep -Eqi "error|invalid|unsupported|unknown|arm|aarch64" "$TMP/err" ||
        { echo "FAIL: under -march=$march x86 source is refused with no message"; exit 1; }
done

cat > "$TMP/roundtrip.s" <<'SRC'
.text
.globl roundtrip
.type roundtrip,@function
roundtrip:
    xor %eax, %eax
    xor %edx, %edx
    mov %rsp, %rbp
    lea 8(%rdi,%r12,4), %rax
    movzbl %sil, %ecx
    ret
.size roundtrip, .-roundtrip
SRC
"$AS" -64 -o "$TMP/roundtrip_a.o" "$TMP/roundtrip.s"
{
    printf '.text\n.globl roundtrip\n.type roundtrip,@function\nroundtrip:\n'
    objdump -d --no-addresses --no-show-raw-insn "$TMP/roundtrip_a.o" |
        awk '/^[[:space:]]+[[:alnum:]_.].*$/ { sub(/^[[:space:]]+/, ""); print }'
    printf '.size roundtrip, .-roundtrip\n'
} > "$TMP/roundtrip_b.s"
"$AS" -64 -o "$TMP/roundtrip_b.o" "$TMP/roundtrip_b.s"
objcopy -O binary --only-section=.text "$TMP/roundtrip_a.o" "$TMP/roundtrip_a.text"
objcopy -O binary --only-section=.text "$TMP/roundtrip_b.o" "$TMP/roundtrip_b.text"
[ -s "$TMP/roundtrip_a.text" ] || { echo "FAIL: the object has no code"; exit 1; }
cmp "$TMP/roundtrip_a.text" "$TMP/roundtrip_b.text" ||
    { echo "FAIL: objdump's listing does not assemble to the bytes it was taken from"; exit 1; }

ld -r -o "$TMP/gnu_ld_on_sub.o" "$TMP/roundtrip_a.o"
readelf -h "$TMP/gnu_ld_on_sub.o" | grep -q "Type:[[:space:]]*REL"
if [ -n "${LD:-}" ] && [ -x "$LD" ]; then
    gcc -c -x assembler -m64 -o "$TMP/gnu_obj.o" "$TMP/roundtrip.s"
    "$LD" -m64 -r -o "$TMP/sub_ld_on_gnu.o" "$TMP/gnu_obj.o"
    readelf -h "$TMP/sub_ld_on_gnu.o" | grep -q "Type:[[:space:]]*REL"
fi

echo "ok: integration matrix"
