#!/bin/sh
# #pragma pack, as gcc lays structures out under it: pack(N) holds every
# member after it to alignment N -- one `aligned` asks more of, and a
# bit-field's storage unit, too -- pack() lifts it, pack(push[, N]) and
# pack(pop) nest, a structure's own `aligned` still counts, and
# _Pragma("pack(...)") is the same.  cc ignored it: ACPI's table
# structures, packed so, were laid out with gaps, and a Linux kernel read
# its MADT wrong ("Error parsing LAPIC NMI entry") and brought up one CPU
# of two.  Sizes, offsets and alignments are gcc's, and a packed record
# written by one compiler is read by the other's code.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pragma_pack: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#pragma pack(1)
struct nmi { unsigned char type, length, processor_id; unsigned short flags; unsigned char lint; };
struct hdr { char sig[4]; unsigned int len; unsigned char rev; unsigned long long addr; };
struct a { char c; int i __attribute__((aligned(8))); };
struct __attribute__((aligned(8))) b { char c; int i; };
struct c { char c; struct { char d; int e; } s; };
#pragma pack()
struct after { char c; int i; };
#pragma pack(push, 4)
struct d { char c; double x; long long y; };
#pragma pack(push, 1)
struct e { char c; short s; };
#pragma pack(pop)
struct f { char c; double x; };
#pragma pack(pop)
struct g { char c; double x; };
#pragma pack(2)
struct h { char c; int i : 3; int j; };
#pragma pack()
#define PACKED_BEGIN _Pragma("pack(push, 1)")
#define PACKED_END _Pragma("pack(pop)")
PACKED_BEGIN
struct k { char c; long l; };
PACKED_END

#define SHOW(T, M) printf(#T " %zu %zu %zu\n", sizeof(struct T), offsetof(struct T, M), _Alignof(struct T))

int main(void)
{
	struct hdr h;
	unsigned char raw[sizeof(struct hdr)];
	unsigned long sum = 0;
	size_t i;

	SHOW(nmi, flags); SHOW(hdr, addr); SHOW(a, i); SHOW(b, i); SHOW(c, s); SHOW(after, i);
	SHOW(d, y); SHOW(e, s); SHOW(f, x); SHOW(g, x); SHOW(h, j); SHOW(k, l);
	memset(&h, 0, sizeof(h));
	memcpy(h.sig, "APIC", 4);
	h.len = 0x12345678u;
	h.rev = 3;
	h.addr = 0x1122334455667788ull;
	memcpy(raw, &h, sizeof(raw));
	for (i = 0; i < sizeof(raw); i++)
		sum = sum * 31 + raw[i];
	printf("%lx %u %llx\n", sum, h.len, h.addr);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/p.c"
	"$CC_TMP/g" >"$CC_TMP/want"
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/p.c"
		"$CC_TMP/c" >"$CC_TMP/got"
		if ! cmp -s "$CC_TMP/got" "$CC_TMP/want"; then
			echo "run_pragma_pack: -m$m -$o: layouts differ from gcc's:"
			diff "$CC_TMP/got" "$CC_TMP/want" | head -8
			exit 1
		fi
	done
done
echo "run_pragma_pack: ok"
