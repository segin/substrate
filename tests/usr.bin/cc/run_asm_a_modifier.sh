#!/bin/sh
# The %a operand modifier of a symbol's address is `sym+off(%rip)` in
# x86-64 code, as gcc prints it, and the bare address in i386 code; of a
# number, the number.  It was the bare address in both, and Linux's early
# x86-64 startup, which runs where it was loaded rather than where it was
# linked, tests boot_cpu_data by %a in static_cpu_has() before the
# kernel's own addresses are mapped: the machine faulted and reset.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/pa.c" <<'EOF2'
struct cpu {
    int x[4];
    unsigned char caps[16];
} boot_cpu;

int f(void) {
    asm goto("testb %[bit], %a[cap]\n\tjnz %l[yes]" : : [bit] "i"(4), [cap] "i"(&boot_cpu.caps[5]) : : yes);
    return 0;
yes:
    return 1;
}

void g(void) {
    asm volatile("# mark %a0 %a1" : : "i"(0x1234), "i"(&boot_cpu));
}
EOF2
check() {
	if ! grep -Fq "$2" "$CC_TMP/pa.s"; then
		echo "run_asm_a_modifier: $1: no \`$2\` in:"
		grep -E "testb|# mark" "$CC_TMP/pa.s"
		exit 1
	fi
}
for o in O0 O2; do
	"$CC_BIN" -m64 -$o -S -o "$CC_TMP/pa.s" "$CC_TMP/pa.c"
	check "-m64 -$o" 'testb $4, boot_cpu+21(%rip)'
	check "-m64 -$o" '# mark 4660 boot_cpu(%rip)'
	"$CC_BIN" -m32 -$o -S -o "$CC_TMP/pa.s" "$CC_TMP/pa.c"
	check "-m32 -$o" 'testb $4, boot_cpu+21'
	check "-m32 -$o" '# mark 4660 boot_cpu'
	if grep -q "(%rip)" "$CC_TMP/pa.s"; then
		echo "run_asm_a_modifier: -m32 -$o: %rip in i386 code"
		exit 1
	fi
done
echo "run_asm_a_modifier: ok"
