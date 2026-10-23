#!/bin/sh
# An asm "i" operand that is an address and a constant is that symbol
# plus the constant.  cc resolved a symbol or a number only, and put a
# placeholder $0 for anything else: Linux's static_cpu_has() tests
# `%a[cap_byte]`, "i" (&boot_cpu_data.x86_capability[bit >> 3]), and
# the kernel tested the byte at address 0 and faulted in early boot.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/ais.c" <<'EOF'
struct cpuinfo {
	int family;
	int pad[9];
	unsigned int caps[8];
};
struct cpuinfo boot_info;

static inline __attribute__((always_inline)) int has(int bit) {
	asm goto("testb %[bitnum], %a[cap_byte]\n\tjnz %l[yes]"
		 : : [bitnum] "i" (1 << (bit & 7)),
		     [cap_byte] "i" (&((const char *)boot_info.caps)[bit >> 3])
		 : : yes);
	return 0;
yes:
	return 1;
}

int probe(void) { return has(13); }
EOF
status=0
# (at -O2, where the always_inline helper is inlined, as Linux builds it)
for o in O2; do
	"$CC_BIN" -m32 -$o -fno-pic -S -o "$CC_TMP/ais.s" "$CC_TMP/ais.c"
	if ! grep -q 'testb $32, boot_info+41' "$CC_TMP/ais.s"; then
		echo "FAIL -$o: the operand is not boot_info+41: $(grep testb "$CC_TMP/ais.s")"
		status=1
	fi
done
[ "$status" -eq 0 ] && echo "run_asm_imm_symbol: ok"
exit "$status"
