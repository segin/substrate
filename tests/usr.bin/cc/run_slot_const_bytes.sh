#!/bin/sh
# A load from a local whose bytes it reads are each written only with
# one constant -- by stores of constants of any size, or a zeroing
# memset -- is that constant, even where the stores are wider than the
# load: a structure an inlined function returns from a const table is
# stored as its words, 8 bytes at a time on x86-64, and its fields are
# read 4 bytes at a time.  Linux's amd_pmu_refresh() checks
# BUILD_BUG_ON(x86_feature_cpuid(X86_FEATURE_PERFMON_V2).function !=
# 0x80000022 || ...index), x86_feature_cpuid() returning
# reverse_cpuid[leaf] by value, and kvm-amd.o kept a call of an
# undefined __compiletime_assert_N.  A byte written with a value not
# known, or with two different constants, stays a load, as gcc keeps it.
# -O2, -m64 and -m32, both code paths, every function through MIR; and
# the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_slot_const_bytes: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void cpuid_bad(void), zeroed_bad(void), bytes_bad(void);
extern void varied_kept(void), twice_kept(void);
enum cpuid_regs { CPUID_EAX = 0, CPUID_EBX, CPUID_ECX, CPUID_EDX };
struct cpuid_reg {
	unsigned int function;
	unsigned int index;
	int reg;
};
static const struct cpuid_reg reverse_cpuid[] = {
	[0] = { 1, 0, CPUID_EDX },
	[1] = { 0x80000001, 0, CPUID_EDX },
	[7] = { 0x80000022, 0, CPUID_EAX },
};
static inline __attribute__((always_inline)) unsigned int feature_leaf(int f)
{
	return f / 32;
}
static inline __attribute__((always_inline)) struct cpuid_reg x86_feature_cpuid(unsigned int f)
{
	unsigned int leaf = feature_leaf(f);

	return reverse_cpuid[leaf];
}
struct pair {
	unsigned short lo, hi;
	unsigned char tag[4];
};

int refresh(int *v)
{
	*v = 1;
	if (x86_feature_cpuid(7 * 32 + 20).function != 0x80000022 || x86_feature_cpuid(7 * 32 + 20).index)
		cpuid_bad();
	if (x86_feature_cpuid(32).reg != CPUID_EDX)
		cpuid_bad();
	return *v + x86_feature_cpuid(0).function;
}

/* the result copied whole to a local of its own, and that read */
struct entry {
	unsigned int eax, ebx, ecx, edx;
};
extern struct entry *find_entry(struct entry *e, unsigned int function);
static inline __attribute__((always_inline)) unsigned int *entry_reg(struct entry *e, int reg)
{
	switch (reg) {
	case CPUID_EAX:
		return &e->eax;
	case CPUID_EBX:
		return &e->ebx;
	case CPUID_ECX:
		return &e->ecx;
	case CPUID_EDX:
		return &e->edx;
	default:
		cpuid_bad();
		return 0;
	}
}
static inline __attribute__((always_inline)) unsigned int *entry_get_reg(struct entry *e, unsigned int f)
{
	const struct cpuid_reg cpuid = x86_feature_cpuid(f);

	/* (read in another block than the copy's) */
	e = find_entry(e, cpuid.function);
	if (!e)
		return 0;
	return entry_reg(e, cpuid.reg);
}

unsigned int entry_bit(struct entry *e)
{
	unsigned int *r = entry_get_reg(e, 7 * 32 + 20);

	return r ? *r & (1u << 20) : 0;
}

int zeroed(int k)
{
	struct pair p;

	__builtin_memset(&p, 0, sizeof(p));
	if (p.hi != 0 || p.tag[2] != 0 || *(unsigned int *)&p.tag != 0)
		zeroed_bad();
	return p.lo + k;
}

int bytes(int k)
{
	union {
		unsigned long long q;
		unsigned short h[4];
		signed char c[8];
	} u;

	u.q = 0xfedcba9880706050ULL;
	if (u.h[1] != 0x8070 || u.c[3] != -128 || u.c[7] != (signed char)0xfe)
		bytes_bad();
	return u.c[5] + k;
}

/* a byte written with something not known, or with two values, is not known */
int varied(int k)
{
	struct pair p = { .lo = 7 };

	if (k)
		p.hi = (unsigned short)k;
	if (p.hi != 0)
		varied_kept();
	return p.lo;
}

int twice(int k)
{
	struct pair p;

	p.lo = 1;
	if (k)
		p.lo = 2;
	if (p.lo != 1)
		twice_kept();
	return p.lo;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void cpuid_bad(void) { bads++; }
void zeroed_bad(void) { bads++; }
void bytes_bad(void) { bads++; }
void varied_kept(void) { kepts++; }
void twice_kept(void) { kepts++; }
struct entry;
struct entry *find_entry(struct entry *e, unsigned int function) { return function == 0x80000022 ? e : 0; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	int v;
	struct entry en = { 1u << 20, 2, 3, 4 };
	int a = refresh(&v) + (int)entry_bit(&en);
	int b = zeroed(3);
	int c = bytes(5);
	int d = varied(9) + varied(0);
	int e = twice(1) + twice(0);

	printf("%d %d %d %d %d bads %d kepts %d\n", a, b, c, d, e, bads, kepts);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=2"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_slot_const_bytes: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_slot_const_bytes: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "twice_kept varied_kept" ]; then
			echo "run_slot_const_bytes: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_slot_const_bytes: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_slot_const_bytes: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_slot_const_bytes: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_slot_const_bytes: ok"
