#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
LD="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/as-testperf-$$
mkdir -p "$TMP/corpus"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/corpus/unit_expr.s" <<'SRC'
.text
.globl unit_expr
.type unit_expr,@function
unit_expr:
    mov $(3 + 4 * 5), %eax
    ret
.size unit_expr, .-unit_expr
SRC

cat > "$TMP/corpus/unit_reloc.s" <<'SRC'
.text
.globl unit_reloc
.type unit_reloc,@function
unit_reloc:
    call ext_fn
    mov $ext_fn, %eax
    ret
.size unit_reloc, .-unit_reloc
SRC

cat > "$TMP/corpus/unit_directive.s" <<'SRC'
.data
bytes:
    .byte 1,2,3
    .long 0x11223344
.text
.globl unit_directive
.type unit_directive,@function
unit_directive:
    ret
.size unit_directive, .-unit_directive
SRC

# Unit-like and golden/differential checks vs backend GNU as.
for mode in 32 64; do
    mflag="-m$mode"
    aflag="-$mode"
    for src in "$TMP/corpus/unit_expr.s" "$TMP/corpus/unit_reloc.s" "$TMP/corpus/unit_directive.s"; do
        base=$(basename "$src" .s)
        out="$TMP/${base}_${mode}.o"
        ref="$TMP/${base}_${mode}.ref.o"
        "$AS" "$aflag" -o "$out" "$src"
        gcc -c -x assembler-with-cpp "$mflag" -o "$ref" "$src"
        cmp "$out" "$ref"
        readelf -a "$out" >/dev/null
        objdump -dr "$out" >/dev/null
    done
done

# Relocation unit/edge-overflow checks.
tests/usr.bin/as/test_relocations_elf_32_64.sh

# Integration with cc-generated assembly.
cat > "$TMP/integration.c" <<'SRC'
int integrate(int x) {
    return (x * 7) + 3;
}
SRC
gcc -m64 -O2 -S -o "$TMP/integration64.s" "$TMP/integration.c"
gcc -m32 -O2 -S -o "$TMP/integration32.s" "$TMP/integration.c"
"$AS" -64 -o "$TMP/integration64.o" "$TMP/integration64.s"
"$AS" -32 -o "$TMP/integration32.o" "$TMP/integration32.s"

# Integration with ld for real link flows.
"$LD" -m64 -r -o "$TMP/integration64.r.o" "$TMP/integration64.o"
"$LD" -m32 -r -o "$TMP/integration32.r.o" "$TMP/integration32.o"
cat > "$TMP/main64.c" <<'SRC'
int integrate(int);
int main(void) { return integrate(5) == 38 ? 0 : 1; }
SRC
gcc -m64 -O2 -c -o "$TMP/main64.o" "$TMP/main64.c"
gcc -m64 -o "$TMP/integration64.bin" "$TMP/main64.o" "$TMP/integration64.o"
"$TMP/integration64.bin"

# Differential corpus check vs GNU as for selected generated corpus.
for i in 1 2 3 4 5; do
    cat > "$TMP/corpus/diff_$i.s" <<SRC
.text
.globl diff_$i
.type diff_$i,@function
diff_$i:
    mov \$${i}, %eax
    add \$$(($i * 3)), %eax
    ret
.size diff_$i, .-diff_$i
SRC
    "$AS" -64 -o "$TMP/diff_$i.o" "$TMP/corpus/diff_$i.s"
    gcc -c -x assembler-with-cpp -m64 -o "$TMP/diff_$i.ref.o" "$TMP/corpus/diff_$i.s"
    cmp "$TMP/diff_$i.o" "$TMP/diff_$i.ref.o"
done

# Fuzzing harness smoke for parser/directive handling (no crashes).
n=0
while [ "$n" -lt 40 ]; do
    f="$TMP/fuzz_$n.s"
    {
        echo ".text"
        echo ".globl fuzz_$n"
        echo "fuzz_$n:"
        echo "    mov \$$n, %eax"
        echo "    .byte $((n % 256))"
        echo "    .long $((n * 17 + 3))"
        echo "    ret"
        dd if=/dev/urandom bs=64 count=1 2>/dev/null | base64 | tr -d '\n'
        echo
    } > "$f"
    rc=0
    if "$AS" -64 -o "$TMP/fuzz_$n.o" "$f" >"$TMP/fuzz_$n.out" 2>"$TMP/fuzz_$n.err"; then
        rc=0
    else
        rc=$?
    fi
    if [ "$rc" -ge 128 ]; then
        echo "unexpected crash-like exit for fuzz input $n"
        exit 1
    fi
    n=$((n + 1))
done

# Large-file throughput benchmark (non-gating, reported metric).
{
    echo ".text"
    echo ".globl bench"
    echo "bench:"
    i=0
    while [ "$i" -lt 50000 ]; do
        echo "    nop"
        i=$((i + 1))
    done
    echo "    ret"
} > "$TMP/bench_large.s"
start=$(date +%s)
"$AS" -64 -o "$TMP/bench_large.o" "$TMP/bench_large.s"
end=$(date +%s)
bench_sec=$((end - start))
[ -s "$TMP/bench_large.o" ]

# Deterministic regression stability suite.
for src in "$TMP/corpus/unit_expr.s" "$TMP/corpus/unit_reloc.s" "$TMP/corpus/unit_directive.s" "$TMP/bench_large.s"; do
    base=$(basename "$src" .s)
    "$AS" -64 -o "$TMP/${base}.det1.o" "$src"
    "$AS" -64 -o "$TMP/${base}.det2.o" "$src"
    cmp "$TMP/${base}.det1.o" "$TMP/${base}.det2.o"
done

echo "ok: testing/fuzz/perf matrix (bench_large_64=${bench_sec}s)"
