#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-fuzz-matrix-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

# Reuse existing grammar-aware + differential + crash/perf matrix.
"$ROOT/tests/usr.bin/as/test_testing_fuzz_perf.sh"

# Byte-level ELF output validation: mutate assembled objects and ensure tools do not crash.
cat > "$TMP/base.s" <<'SRC'
.text
.globl fuzz_base
.type fuzz_base,@function
fuzz_base:
    mov $7, %eax
    add $9, %eax
    ret
.size fuzz_base, .-fuzz_base
SRC
"$AS" -64 -o "$TMP/base.o" "$TMP/base.s"

i=0
while [ "$i" -lt 32 ]; do
    obj="$TMP/mut_$i.o"
    cp "$TMP/base.o" "$obj"
    size=$(wc -c < "$obj")
    off=$(( (i * 67 + 11) % size ))
    dd if=/dev/urandom of="$obj" bs=1 seek="$off" count=16 conv=notrunc 2>/dev/null

    rc=0
    if readelf -a "$obj" >/dev/null 2>"$TMP/readelf_$i.err"; then
        rc=0
    else
        rc=$?
    fi
    if [ "$rc" -ge 128 ]; then
        echo "readelf crashed on mutated object $i"
        exit 1
    fi

    rc=0
    if objdump -dr "$obj" >/dev/null 2>"$TMP/objdump_$i.err"; then
        rc=0
    else
        rc=$?
    fi
    if [ "$rc" -ge 128 ]; then
        echo "objdump crashed on mutated object $i"
        exit 1
    fi

    i=$((i + 1))
done

# Crash-free guarantee on arbitrary binary input as source.
i=0
while [ "$i" -lt 64 ]; do
    src="$TMP/random_$i.s"
    dd if=/dev/urandom of="$src" bs=256 count=1 2>/dev/null

    rc=0
    if "$AS" -64 -o "$TMP/random_$i.o" "$src" >"$TMP/random_$i.out" 2>"$TMP/random_$i.err"; then
        rc=0
    else
        rc=$?
    fi
    if [ "$rc" -ge 128 ]; then
        echo "assembler crashed on random input $i"
        exit 1
    fi

    i=$((i + 1))
done

echo "ok: fuzz matrix"
