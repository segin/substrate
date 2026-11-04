#!/bin/sh
# A field of a structure an inlined function takes by value, copied from a
# const table, is the table's constant there, as gcc folds it: Linux's
# __cpuid_entry_get_reg() switches on reverse_cpuid[leaf].reg with a
# BUILD_BUG() in its default arm, reached through x86_feature_cpuid(),
# cpuid_entry_get_reg(), guest_cpuid_has() and raw_cpuid_get().  cc kept the
# call of the undefined __compiletime_assert_N in kvm.ko where the copy
# went through a second slot (i386 passes the structure in memory), through
# a move of the parameter's address, under an asm goto, or -- in a function
# past the inliner's size limit, kvm_initialize_cpu_caps() -- into an
# always_inline body left out of line.  The undefined symbols are gcc's, no
# function falls back from MIR, and the program prints what gcc's does;
# -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_struct_by_value: skip (no gcc)"
	exit 0
fi

# the shared part: a const table of structures, read through inlined functions
cat >"$CC_TMP/head.h" <<'EOF2'
struct reg { unsigned function, index; int reg; };
struct ent { unsigned eax, ebx, ecx, edx; };
static const struct reg tab[] = {
	[0] = {1, 0, 3}, [1] = {0x80000001, 0, 3}, [2] = {7, 0, 1}, [3] = {0xd, 1, 0},
	[4] = {6, 0, 0}, [5] = {7, 1, 2}, [6] = {0x12, 0, 0}, [7] = {7, 2, 3},
};
extern void undefined_bad(void);
extern unsigned caps[8];
extern unsigned kcaps[8];
extern struct ent *find(unsigned f, unsigned i);
extern void cpuid_count(unsigned f, unsigned i, unsigned *a, unsigned *b, unsigned *c, unsigned *d);
#define AI static inline __attribute__((always_inline))
AI unsigned leafof(unsigned f)
{
	unsigned leaf = f / 32;

	if (leaf >= sizeof(tab) / sizeof(tab[0]))
		undefined_bad();
	if (tab[leaf].function == 0)
		undefined_bad();
	return leaf;
}
AI struct reg lookup(unsigned f) { return tab[leafof(f)]; }
AI unsigned bit(unsigned f) { leafof(f); return 1u << (f & 31); }
AI unsigned *getr(struct ent *e, unsigned r)
{
	switch (r) {
	case 0: return &e->eax;
	case 1: return &e->ebx;
	case 2: return &e->ecx;
	case 3: return &e->edx;
	default: undefined_bad(); return 0;
	}
}
AI unsigned raw_get(struct reg c)
{
	struct ent e = {0, 0, 0, 0};

	if ((c.function & 0xffff0000) && (c.function & 0xffff0000) != 0x80000000)
		return 0;
	cpuid_count(c.function, c.index, &e.eax, &e.ebx, &e.ecx, &e.edx);
	return *getr(&e, c.reg);
}
EOF2
cat >"$CC_TMP/small.c" <<'EOF2'
#include "head.h"
/* the structure by value, copied by the caller and by the callee */
int by_value(int x)
{
	if (!x)
		return 0;
	return raw_get(lookup(2 * 32));
}
/* the guest_cpuid_has() shape: fields read around a call, then a switch */
AI int has(unsigned f)
{
	const struct reg c = lookup(f);
	struct ent *e = find(c.function, c.index);
	unsigned *r;

	if (!e)
		return 0;
	r = getr(e, c.reg);
	return *r & (1u << (f & 31));
}
/* and under an asm goto, which writes none of it */
int with_asm_goto(int x)
{
	asm goto("" : : : : out);
	if (x && has(5 * 32 + 3))
		return 3;
	return 1;
out:
	return 2;
}
EOF2
# past the inliner's size limit: blocks of features, each a structure read from the table
{
	echo '#include "head.h"'
	echo '#define F(l, n) do { if (leafof((l) * 32 + (n)) != l) undefined_bad(); feat |= bit((l) * 32 + (n)); } while (0)'
	echo '#define INIT(l, body) do { const struct reg c = lookup((l) * 32); unsigned feat = 0; body; \'
	echo '	caps[l] = feat; caps[l] &= kcaps[l]; caps[l] &= raw_get(c); } while (0)'
	echo 'void init_caps(void)'
	echo '{'
	b=0
	while [ $b -lt 6 ]; do
		l=$((b % 8))
		printf '\tINIT(%d, ' $l
		j=0
		while [ $j -lt 300 ]; do
			printf 'F(%d, %d); ' $l $((j % 32))
			j=$((j + 1))
		done
		printf ');\n'
		b=$((b + 1))
	done
	echo '}'
} >"$CC_TMP/big.c"
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>
struct ent { unsigned eax, ebx, ecx, edx; };
unsigned caps[8];
unsigned kcaps[8] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };
static struct ent ents[2] = { { 1, 2, 3, 4 }, { 5, 6, 0x88, 8 } };
struct ent *find(unsigned f, unsigned i) { return f == 7 ? &ents[i & 1] : 0; }
void cpuid_count(unsigned f, unsigned i, unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{
	*a = f; *b = f + i + 0x10; *c = f ^ 0x5555; *d = ~f;
}
int by_value(int), with_asm_goto(int);
void init_caps(void);
int main(void)
{
	int k;

	init_caps();
	printf("%d %d %d %d", by_value(0), by_value(1), with_asm_goto(0), with_asm_goto(1));
	for (k = 0; k < 8; ++k)
		printf(" %x", caps[k]);
	printf("\n");
	return 0;
}
EOF2

undefs() {
	nm "$1" | awk '$(NF-1) == "U" && $NF !~ /^_GLOBAL_OFFSET_TABLE_$|^__stack_chk_fail|get_pc_thunk/ { print $NF }' | sort -u
}
fail=0
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/probe.o" "$CC_TMP/small.c" -I"$CC_TMP" 2>/dev/null || continue
	gcc -m$m -O2 -o "$CC_TMP/g" "$CC_TMP/main.c" "$CC_TMP/small.c" "$CC_TMP/big.c" -I"$CC_TMP"
	want_out=$("$CC_TMP/g")
	objs=
	for f in small big; do
		gcc -m$m -O2 -c -o "$CC_TMP/g_$f.o" -I"$CC_TMP" "$CC_TMP/$f.c"
		want=$(undefs "$CC_TMP/g_$f.o")
		if ! env CC_MIR_STATS=1 "$CC_BIN" -m$m -O2 -I"$CC_TMP" -c -o "$CC_TMP/c_$f.o" "$CC_TMP/$f.c" \
			2>"$CC_TMP/c.err"; then
			echo "run_fold_struct_by_value: -m$m $f.c refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_struct_by_value: -m$m $f.c: a function fell back from MIR"
			fail=1
		fi
		got=$(undefs "$CC_TMP/c_$f.o")
		if [ "$got" != "$want" ]; then
			echo "run_fold_struct_by_value: -m$m $f.c refers to [$(echo $got)], gcc [$(echo $want)]"
			fail=1
		fi
		objs="$objs $CC_TMP/c_$f.o"
	done
	gcc -m$m -o "$CC_TMP/c" "$CC_TMP/main.c" $objs 2>"$CC_TMP/l.err" || {
		echo "run_fold_struct_by_value: -m$m does not link: $(head -1 "$CC_TMP/l.err")"
		fail=1
		continue
	}
	got_out=$("$CC_TMP/c" 2>&1 || true)
	if [ "$got_out" != "$want_out" ]; then
		echo "run_fold_struct_by_value: -m$m prints [$got_out], gcc's [$want_out]"
		fail=1
	fi
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_struct_by_value: ok"
