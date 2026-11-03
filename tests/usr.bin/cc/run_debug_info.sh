#!/bin/sh
# -g writes DWARF debug information gcc's tools read as they read gcc's:
# a compile unit (.debug_info, .debug_abbrev, .debug_str) describing the
# unit's types, objects and functions, not only the line table.  cc took
# -g, -gdwarf-4 and -gdwarf-5 and wrote .debug_line and .debug_frame
# alone, so pahole found no types -- a Linux built with
# CONFIG_DEBUG_INFO_BTF stopped at its first vmlinux link, pahole -J
# making no BTF.  gcc is the judge: for every structure and union of a
# type-rich unit, pahole's layout of cc's object -- sizes, member
# offsets, bit-fields, holes, typedef and enum names, function-pointer
# members -- must be gcc's; llvm-dwarfdump --verify (or readelf) must
# accept the object; pahole -J must make BTF of it; -gdwarf-4 and
# -gdwarf-5 give a unit of that version, and -g0 none.  -m64 and -m32,
# both code paths (the machine IR's and the slot layout's).
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1; then
	echo "run_debug_info: skip (no gcc or readelf)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/t.c" <<'EOF2'
#include <stddef.h>
typedef unsigned int u32;
typedef unsigned char u8;
typedef const u32 cu32;
typedef struct { int counter; } atomic_t;
typedef struct { long v __attribute__((aligned(16))); } wide_t;
typedef void (*handler_t)(int, void *);
enum color { RED, GREEN = 5, BLUE };
enum sign { NEG = -2, POS = 2 };
enum __attribute__((packed)) small { S0, S1 = 200 };
struct list { struct list *next, *prev; };
struct opaque;
union opaque_u;
struct bits {
	u32 a : 3, b : 5;
	char c;
	long long d : 40;
	_Bool e : 1;
	unsigned : 0;
	int f : 7;
	u8 tail[];
};
struct holes { char c; long l; short s; int i; char z; };
struct nested {
	struct list node;
	union { int i; float f; struct { short lo, hi; }; } u;
	struct { u8 kind; u32 len; } hdr;
	const struct opaque *op;
	const union opaque_u *ou;
	enum color col;
	enum sign sg;
	enum small sm;
	cu32 id;
	volatile u32 *vp;
	u32 *restrict rp;
	handler_t h;
	int (*cb)(struct nested *, const char *, ...);
	void (*old)();
	double d[2][3];
	atomic_t refs;
	wide_t w;
	long double ld;
	_Float16 half;
	float _Complex z;
	u8 flex[0];
};
struct packed { char c; int i; short s; } __attribute__((packed));
struct aligned { char c; int i __attribute__((aligned(8))); } __attribute__((aligned(32)));

struct nested g_nested;
struct bits g_bits;
struct holes g_holes;
struct packed g_packed;
struct aligned g_aligned;
static struct list g_head = { &g_head, &g_head };
extern int ext_counter;

static inline int helper(const struct holes *h) { return h->c + h->i; }
int use(struct nested *n, int k, ...)
{
	struct holes local = { 1, 2, 3, 4, 5 };
	static int calls;

	calls++;
	return helper(&local) + n->refs.counter + k + calls + ext_counter + (g_head.next == &g_head);
}
EOF2

# NAME [OBJ]: pahole's layout of each of the unit's structures and unions
layouts() {
	for t in list bits holes nested packed aligned; do
		pahole -C "$t" "$1" 2>&1
	done
}

fail=0
have_pahole=0
command -v pahole >/dev/null 2>&1 && have_pahole=1
for m in 64 32; do
	gcc -m$m -std=gnu17 -g -O0 -c -o "$CC_TMP/g.o" "$CC_TMP/t.c" 2>/dev/null || continue
	[ $have_pahole -eq 0 ] || layouts "$CC_TMP/g.o" >"$CC_TMP/want"
	for path in mir legacy; do
		run=env
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for v in 5 4; do
			what="-m$m -gdwarf-$v $path"
			if ! $run "$CC_BIN" -m$m -std=gnu17 -gdwarf-$v -O0 -c -o "$CC_TMP/c.o" "$CC_TMP/t.c" 2>"$CC_TMP/c.err"; then
				echo "run_debug_info: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if ! readelf --debug-dump=info "$CC_TMP/c.o" >"$CC_TMP/info" 2>"$CC_TMP/info.err" ||
			    [ -s "$CC_TMP/info.err" ]; then
				echo "run_debug_info: $what: readelf: $(head -1 "$CC_TMP/info.err")"
				fail=1
			fi
			if ! grep -q "Version: *$v\$" "$CC_TMP/info"; then
				echo "run_debug_info: $what: the unit is not of DWARF $v"
				fail=1
			fi
			if ! grep -q 'DW_TAG_structure_type' "$CC_TMP/info" || ! grep -q 'DW_TAG_subprogram' "$CC_TMP/info"; then
				echo "run_debug_info: $what: no structure or function described"
				fail=1
			fi
			if command -v llvm-dwarfdump >/dev/null 2>&1 &&
			    ! llvm-dwarfdump --verify "$CC_TMP/c.o" >"$CC_TMP/verify" 2>&1; then
				echo "run_debug_info: $what: llvm-dwarfdump --verify:"
				grep -i error "$CC_TMP/verify" | head -5
				fail=1
			fi
			if [ $have_pahole -eq 1 ]; then
				layouts "$CC_TMP/c.o" >"$CC_TMP/got"
				if ! cmp -s "$CC_TMP/got" "$CC_TMP/want"; then
					echo "run_debug_info: $what: pahole's layouts differ from gcc's:"
					diff "$CC_TMP/want" "$CC_TMP/got" | head -20
					fail=1
				fi
				cp "$CC_TMP/c.o" "$CC_TMP/b.o"
				if ! pahole -J "$CC_TMP/b.o" >"$CC_TMP/btf" 2>&1 || grep -qi error "$CC_TMP/btf" ||
				    ! readelf -S "$CC_TMP/b.o" | grep -q '\.BTF'; then
					echo "run_debug_info: $what: pahole -J made no BTF: $(head -1 "$CC_TMP/btf")"
					fail=1
				fi
			fi
		done
	done
	# -g0 after -g: none
	"$CC_BIN" -m$m -g -g0 -c -o "$CC_TMP/n.o" "$CC_TMP/t.c"
	if readelf -S "$CC_TMP/n.o" | grep -q '\.debug_info'; then
		echo "run_debug_info: -m$m -g -g0 still writes .debug_info"
		fail=1
	fi
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_debug_info: ok"
