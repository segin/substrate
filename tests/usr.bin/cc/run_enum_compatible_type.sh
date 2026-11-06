#!/bin/sh
# An enumeration is the integer type gcc makes it compatible with:
# unsigned int when no enumerator is negative, int when one is, (unsigned)
# long -- long long where long has 32 bits -- for values beyond 32 bits,
# and as small as its values allow when packed, or the size a mode
# attribute names; one with a fixed underlying type (C23) is that type.
# Its size, signedness, promotion and what
# __builtin_types_compatible_p and _Generic say of it follow.  cc made
# every enumeration a 4-byte signed type, so Linux's max(e->num_engines,
# engine->legacy_idx + 1) -- an unsigned int against an enumeration plus
# one, unsigned int to gcc -- kept the call to its undefined
# __compiletime_assert_N in i915_gem_context.o, from max()'s signedness
# check.  gcc is the judge: no call to an undefined function where gcc
# leaves none; the program's sizes, signedness and compatibilities print
# what gcc's do; an enumeration named before its definition agrees with
# the type it gets; -g describes each enumeration as gcc does (its size
# and underlying type).  -m64 and -m32, every function through MIR.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v nm >/dev/null 2>&1; then
	echo "run_enum_compatible_type: skip (no gcc or nm)"
	exit 0
fi

# the shape of Linux's max(): its operands' signedness must agree
cat >"$CC_TMP/f.c" <<'EOF2'
enum engine_id { RCS0, BCS0, VCS0, NUM_ENGINES };
struct engine { enum engine_id legacy_idx; };
struct engines { unsigned int num_engines; };
__attribute__((noreturn)) extern void signedness_differs(void);
__attribute__((noreturn)) extern void wrong_type(void);
#define is_signed_type(t) (((t)(-1)) < (t)1)

void grow(struct engines *e, const struct engine *engine)
{
	__auto_type x = e->num_engines;
	__auto_type y = engine->legacy_idx + 1;

	if (is_signed_type(typeof(x)) != is_signed_type(typeof(y)))
		signedness_differs();
	e->num_engines = x > y ? x : y;
}

enum neg { NEG = -1, POS };
enum __attribute__((packed)) small { S0, S1 = 200 };
void types(void)
{
	if (!__builtin_types_compatible_p(enum engine_id, unsigned int) ||
	    !__builtin_types_compatible_p(enum neg, int) ||
	    !__builtin_types_compatible_p(enum small, unsigned char) ||
	    !__builtin_types_compatible_p(typeof((enum small)0 + 1), int) ||
	    _Generic((enum engine_id)0, unsigned int: 0, default: 1))
		wrong_type();
}
EOF2

# the rule, case by case
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>
#define TC(a, b) __builtin_types_compatible_p(a, b)
#define SIGNED(t) ((t)-1 < (t)1)
enum u { U0, U1 = 5 };
enum s { S0 = -1, S1 };
enum big { B0, B1 = 0x100000000 };
enum sbig { SB0 = -1, SB1 = 0x100000000 };
enum ubig2 { UB = 0x80000000 };
enum mixed { M0 = -1, M1 = 0x80000000 };
enum __attribute__((packed)) pu { PU0, PU1 = 200 };
enum __attribute__((packed)) ps { PS0 = -1, PS1 = 100 };
enum __attribute__((packed)) ps2 { PS20 = -1, PS21 = 200 };
enum pu3 { PU30, PU31 = 70000 } __attribute__((packed));
enum __attribute__((packed)) pbig { PB = 0x100000000 };
enum __attribute__((mode(byte))) mb { MB0, MB1 };
enum __attribute__((__mode__(__byte__))) mbs { MBS0 = -1, MBS1 };
enum __attribute__((mode(HI))) mh { MH0 };
enum __attribute__((mode(DI))) md { MD0 };
enum __attribute__((mode(SI))) ms { MS0 = -2 };
enum __attribute__((mode(word))) mw { MW0 };
typedef enum { T0, T1 } __attribute__((mode(QI))) mt;
enum fwd;
struct all {
	enum u u; enum s s; enum big big; enum sbig sbig; enum ubig2 ubig2; enum mixed mixed;
	enum pu pu; enum ps ps; enum ps2 ps2; enum pu3 pu3; enum pbig pbig;
	enum mb mb; enum mbs mbs; enum mh mh; enum md md; enum ms ms; enum mw mw; mt t;
	enum s bf : 2; enum u ubf : 2;
} g_all;
int ck[] = {
	sizeof(enum u), TC(enum u, unsigned int), TC(enum u, int), SIGNED(enum u),
	sizeof(enum s), TC(enum s, int), SIGNED(enum s),
	sizeof(enum big), TC(enum big, unsigned long), TC(enum big, unsigned long long), SIGNED(enum big),
	sizeof(enum sbig), TC(enum sbig, long), TC(enum sbig, long long), SIGNED(enum sbig),
	sizeof(enum ubig2), TC(enum ubig2, unsigned int),
	sizeof(enum mixed), TC(enum mixed, long), TC(enum mixed, long long),
	sizeof(enum pu), TC(enum pu, unsigned char), SIGNED(enum pu),
	sizeof(enum ps), TC(enum ps, signed char), TC(enum ps, char), SIGNED(enum ps),
	sizeof(enum ps2), TC(enum ps2, short),
	sizeof(enum pu3), TC(enum pu3, unsigned int),
	sizeof(enum pbig), TC(enum pbig, unsigned long), TC(enum pbig, unsigned long long),
	sizeof(enum mb), TC(enum mb, unsigned char),
	sizeof(enum mbs), TC(enum mbs, signed char),
	sizeof(enum mh), TC(enum mh, unsigned short),
	sizeof(enum md), TC(enum md, unsigned long), TC(enum md, unsigned long long),
	sizeof(enum ms), TC(enum ms, int),
	sizeof(enum mw), TC(enum mw, unsigned long),
	sizeof(mt), TC(mt, unsigned char),
	TC(typeof(U1 + 1), unsigned int), TC(typeof(PU1 + 1), int), TC(typeof((enum u)0 + 1), unsigned int),
	TC(typeof((enum pu)0 + 1), int), TC(typeof(U1), int),
	_Generic((enum u)0, unsigned int: 1, default: 0), _Generic((enum s)0, int: 1, default: 0),
	_Alignof(enum big), _Alignof(enum pu), sizeof(struct all),
	TC(enum fwd *, unsigned int *), TC(enum fwd *, int *),
};
int main(void)
{
	unsigned i;

	g_all.bf = -1;
	g_all.ubf = 3;
	printf("%d %d ", g_all.bf < 0, g_all.ubf > 2);
	for (i = 0; i < sizeof ck / sizeof ck[0]; i++)
		printf("%d ", ck[i]);
	printf("\n");
	return 0;
}
EOF2

# C23's fixed underlying types: the enumeration is that type
cat >"$CC_TMP/c23.c" <<'EOF2'
#include <stdio.h>
#define TC(a, b) __builtin_types_compatible_p(a, b)
enum a : unsigned long long { A0 };
enum b : signed char { B0 = -1 };
enum c : int { C0 };
enum d : unsigned short { D0 };
struct s { enum a a; enum b b; enum c c; enum d d; } g;
int ck[] = { sizeof(enum a), TC(enum a, unsigned long long), sizeof(enum b), TC(enum b, signed char),
	     TC(enum c, int), sizeof(enum d), TC(typeof((enum d)0 + 1), int), sizeof(struct s) };
int main(void)
{
	for (unsigned i = 0; i < sizeof ck / sizeof ck[0]; i++)
		printf("%d ", ck[i]);
	printf("\n");
	return 0;
}
EOF2

# an enumeration named before its definition is that enumeration
cat >"$CC_TMP/fwd.c" <<'EOF2'
enum e;
enum e2;
extern enum e2 gv;
void g(enum e2 *p);
enum e { A, B };
enum e2 { C = -1, D };
void f(enum e x);
void f(enum e x) { (void)x; }
void g(enum e2 *p) { *p = C; }
enum e2 gv = D;
EOF2

# each enumeration DIE: its name, size and underlying type
enum_dies() {
	llvm-dwarfdump --debug-info "$1" | awk '
		/DW_TAG/ { if (n != "") print n, sz, ty; n = ""; inenum = ($2 == "DW_TAG_enumeration_type"); sz = ""; ty = "" }
		inenum && /DW_AT_name/ { n = $2 }
		inenum && /DW_AT_byte_size/ { sz = $2; gsub(/[()]/, "", sz); sub(/^0x0*/, "", sz) }
		inenum && /DW_AT_type/ { sub(/.*\(0x[0-9a-f]* /, ""); ty = $0 }
		END { if (n != "") print n, sz, ty }' | sort
}

und() {
	nm "$1" | awk '$1 == "U" { print $2 }' | sort
}
fail=0
for m in 64 32; do
	gcc -m$m -O2 -c -o "$CC_TMP/g.o" "$CC_TMP/f.c" 2>/dev/null || continue
	want=$(und "$CC_TMP/g.o")
	for o in O1 O2; do
		what="-m$m -$o"
		if ! env CC_MIR_STATS=2 "$CC_BIN" -m$m -$o -c -o "$CC_TMP/c.o" "$CC_TMP/f.c" 2>"$CC_TMP/c.err"; then
			echo "run_enum_compatible_type: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_enum_compatible_type: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(und "$CC_TMP/c.o")
		if [ "$got" != "$want" ]; then
			echo "run_enum_compatible_type: $what calls undefined [$(echo $got)], gcc [$(echo $want)]"
			fail=1
		fi
	done

	for ps in p:gnu11 c23:c23; do
		src="$CC_TMP/${ps%%:*}.c"
		std=${ps#*:}
		what="-m$m -std=$std"
		gcc -m$m -std=$std -w -o "$CC_TMP/gp" "$src"
		want_out=$("$CC_TMP/gp")
		if ! "$CC_BIN" -m$m -std=$std -w -c -o "$CC_TMP/cp.o" "$src" 2>"$CC_TMP/c.err"; then
			echo "run_enum_compatible_type: $what: the rule's program refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/cp" "$CC_TMP/cp.o"
		got_out=$("$CC_TMP/cp" 2>&1 || true)
		if [ "$got_out" != "$want_out" ]; then
			echo "run_enum_compatible_type: $what prints [$got_out]"
			echo "run_enum_compatible_type: $what gcc's    [$want_out]"
			fail=1
		fi

		if command -v llvm-dwarfdump >/dev/null 2>&1; then
			gcc -m$m -std=$std -w -g -c -o "$CC_TMP/gd.o" "$src"
			"$CC_BIN" -m$m -std=$std -w -g -c -o "$CC_TMP/cd.o" "$src"
			enum_dies "$CC_TMP/gd.o" >"$CC_TMP/want_dies"
			enum_dies "$CC_TMP/cd.o" >"$CC_TMP/got_dies"
			if ! cmp -s "$CC_TMP/want_dies" "$CC_TMP/got_dies"; then
				echo "run_enum_compatible_type: $what -g: the enumerations' DIEs differ from gcc's:"
				diff "$CC_TMP/want_dies" "$CC_TMP/got_dies" | head -20
				fail=1
			fi
		fi
	done

	if ! "$CC_BIN" -m$m -c -o "$CC_TMP/fwd.o" "$CC_TMP/fwd.c" 2>"$CC_TMP/c.err"; then
		echo "run_enum_compatible_type: -m$m: an enumeration named before its definition: $(grep error "$CC_TMP/c.err" | head -1)"
		fail=1
	fi
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_enum_compatible_type: ok"
