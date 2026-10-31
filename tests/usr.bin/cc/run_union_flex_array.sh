#!/bin/sh
# A union may have a flexible array member, gcc's extension since 15:
# of no size, anywhere in the union, which is as large as its other
# members.  cc refused it, and Linux, which puts one in a union where
# the compiler takes it (net/ipv4/ip_output.c, the Arch config), did not
# build.  Sizes, offsets and accesses through it are gcc's, -m64 and
# -m32; a structure's flexible array still has to be its last member.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_union_flex_array: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/u.c" <<'EOF2'
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

union last {
	int n;
	char t[];
};
union only {
	char t[];
};
union first {
	short t[];
	long n;
};
union two {
	char t[];
	long u[];
};
struct hdr {
	int len;
	union {
		unsigned char raw[2];
		unsigned short words[];
	};
};
struct opts {
	int n;
	unsigned char data[];
};
/* as Linux's __DEFINE_FLEX lays one out on the stack */
union onstack {
	unsigned char bytes[sizeof(struct opts) + 6];
	struct opts obj;
};

int main(void)
{
	struct hdr *h = malloc(sizeof(struct hdr) + 8);
	union onstack o;
	union first f;

	memset(&o, 0, sizeof(o));
	o.obj.n = 6;
	memcpy(o.obj.data, "abcdef", 6);
	h->len = 3;
	h->words[0] = 1;
	h->words[1] = 2;
	h->words[2] = 3;
	f.n = 0x0102030405060708L;
	printf("%zu %zu %zu %zu %zu %zu %zu %zu\n", sizeof(union last), sizeof(union only), sizeof(union first),
	       sizeof(union two), sizeof(struct hdr), offsetof(struct hdr, words), _Alignof(union first),
	       sizeof(union onstack));
	printf("%d %.6s %d %d %x\n", o.obj.n, (char *)o.obj.data, h->words[1] + h->words[2], h->raw[0],
	       (unsigned)(unsigned short)f.t[0]);
	free(h);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/u.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_union_flex_array: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/u.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_union_flex_array: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
# a structure's flexible array not last: an error in both
printf 'struct s {\n\tint n;\n\tchar t[];\n\tint m;\n};\n' >"$CC_TMP/n.c"
if gcc -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_union_flex_array: gcc accepts a structure's flexible array not last"
	exit 1
fi
if "$CC_BIN" -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>"$CC_TMP/n.err"; then
	echo "run_union_flex_array: cc accepts a structure's flexible array not last"
	exit 1
fi
if ! grep -q 'flexible array member' "$CC_TMP/n.err"; then
	echo "run_union_flex_array: a structure's flexible array not last: $(head -1 "$CC_TMP/n.err")"
	exit 1
fi
echo "run_union_flex_array: ok"
