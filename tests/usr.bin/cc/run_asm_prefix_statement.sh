#!/bin/sh
# A prefix an asm writes as a statement of its own stays one: cc joined
# `rep; vmmcall` into `rep vmmcall`, an instruction the assembler
# refuses (gas and ours: vmmcall takes no rep prefix), and Linux's
# VMGEXIT() is that `rep; vmmcall` (arch/x86/coco/sev, the Arch
# config).  The object is compared with gas's encoding of the same, and
# `lock;` and `rep;` before the instructions they prefix still work, as
# gcc's do.  -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_asm_prefix_statement: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

printf 'void vmgexit(void)\n{\n\tasm volatile("rep; vmmcall\\n\\r");\n}\n' >"$CC_TMP/v.c"
gcc -O2 -c -o "$CC_TMP/vg.o" "$CC_TMP/v.c"
if ! "$CC_BIN" -O2 -c -o "$CC_TMP/vc.o" "$CC_TMP/v.c" 2>"$CC_TMP/v.err"; then
	echo "run_asm_prefix_statement: rep; vmmcall: $(head -1 "$CC_TMP/v.err")"
	exit 1
fi
want=$(objdump -d "$CC_TMP/vg.o" | grep -c 'f3 0f 01 d9')
got=$(objdump -d "$CC_TMP/vc.o" | grep -c 'f3 0f 01 d9')
if [ "$got" != "$want" ] || [ "$got" -ne 1 ]; then
	echo "run_asm_prefix_statement: rep; vmmcall is not vmgexit (f3 0f 01 d9)"
	objdump -d "$CC_TMP/vc.o"
	exit 1
fi

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

int main(void)
{
	int n = 5;
	char src[8] = "abcdefg";
	char dst[8] = { 0 };
	char *d = dst;
	const char *s = src;
	unsigned long c = 7;

	asm volatile("lock; incl %0" : "+m"(n));
	asm volatile("lock\n\tincl %0" : "+m"(n));
	asm volatile("cld; rep; movsb" : "+D"(d), "+S"(s), "+c"(c) : : "memory");
	printf("%d %s %lu\n", n, dst, c);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/p.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_asm_prefix_statement: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_asm_prefix_statement: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_asm_prefix_statement: ok"
