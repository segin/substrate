#!/bin/sh
# A packed structure whose bit-field leaves no storage unit inside it is
# laid out and accessed as gcc does: Linux's dm-vdo `struct
# open_chapter_zone_slot { unsigned int record_number : 23; bool deleted
# : 1; } __packed` is three bytes, the field taking all 24 bits, and no
# unit of 1, 2, 4 or 8 bytes fits, so cc refused it ("unsupported packed
# bit-field layout").  Such a field is now read and written by byte,
# only its own bytes, so nothing past the object is touched.  Arrays of
# them, a member before the field, static initializers, a structure
# passed by value: the bytes and values are gcc's, -m64 and -m32, -O0
# and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_packed_bitfield_unit: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

struct s { unsigned int r : 23; bool d : 1; } __attribute__((packed));
struct t { char c; unsigned int r : 23; bool d : 1; } __attribute__((packed));

struct s tab[3] = { { 5, 1 }, { 0x7fffff, 0 }, { 0x123456, 1 } };
static struct t one = { 9, 0x654321, 1 };

static int by_value(struct s v) { return (int)v.r + v.d; }

static void dump(const void *p, size_t n)
{
	const unsigned char *b = p;
	size_t i;

	for (i = 0; i < n; i++)
		printf("%02x", b[i]);
	printf(" ");
}

int main(void)
{
	struct s a[3];

	memset(a, 0xee, sizeof a);
	a[1].r = 0x7fffff;
	a[1].d = 1;
	a[0].r = 3;
	a[0].d = 0;
	a[2].r = a[1].r - 1;
	a[2].d = !a[0].d;
	dump(a, sizeof a);
	dump(tab, sizeof tab);
	dump(&one, sizeof one);
	one.r += 1;
	one.d = 0;
	dump(&one, sizeof one);
	printf("%zu %zu %u %d %u %d %d\n", sizeof(struct s), sizeof(struct t), tab[2].r, tab[2].d, a[2].r, a[2].d,
	       by_value(tab[0]));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			$run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_packed_bitfield_unit: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_packed_bitfield_unit: ok"
