#!/bin/sh
# A loop's counter is within what its start, its steps and the loop's test
# allow, as gcc's value-range propagation knows an induction variable from
# -O2: `i` of `for (i = 0; i < 3; i++)` only rises from 0, and rises only
# where the test holds it below 3, so inside the loop it is within [0, 2]
# -- also as an unsigned int, and under -fwrapv, where nothing says it
# cannot wrap but the test.  Linux's KVM inlines intel_get_fixed_pmc_eventsel(),
# whose BUILD_BUG_ON asks that its unsigned index is below 3, into such a
# loop (arch/x86/kvm/vmx/pmu_intel.c), and cc left a call of an undefined
# __compiletime_assert_N.  What must fold leaves no reference gcc's code
# does not; a counter the loop sets otherwise, or one starting from an
# unknown value, stays as gcc keeps it.  -O2 and -O2 -fwrapv (gcc folds
# none of it at -O1), -m64 and -m32, every function through MIR; and the
# program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_range_induction: skip (no gcc)"
	exit 0
fi

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void index_bad(void), down_bad(void), step_bad(void);
extern void reset_kept(void), start_kept(void), after_kept(void);
extern unsigned long event(int id);
extern int pick(int i);
struct counter { int type; void *owner; int idx; unsigned long ev; };
struct pmu { struct counter fixed[3]; int w[8]; };

static inline __attribute__((always_inline)) unsigned long fixed_event(unsigned int index)
{
	const int ids[] = { 1, 2, 9 };

	if (index >= 3)
		index_bad();
	return event(ids[index]);
}

void init(struct pmu *p, void *owner)
{
	int i;

	for (i = 0; i < 3; i++) {
		p->fixed[i].type = 2;
		p->fixed[i].owner = owner;
		p->fixed[i].idx = i + 32;
		p->fixed[i].ev = fixed_event(i);
	}
}

int down(struct pmu *p)
{
	int i, s = 0;

	for (i = 7; i > 0; i--) {
		if (i > 7 || i < 1)
			down_bad();
		s += p->w[i];
	}
	return s;
}

int step(struct pmu *p)
{
	int i, s = 0;

	for (i = 0; i < 7; i += 2) {
		if (i > 8)
			step_bad();
		s += p->w[i];
	}
	return s;
}

int reset(int n)
{
	int i, s = 0;

	for (i = 0; i < n; i++) {
		if (i == 2)
			i = pick(i);
		if ((unsigned int)i >= 3)
			reset_kept();
		s += i;
	}
	return s;
}

int start(int n)
{
	int i, s = 0;

	for (i = n; i < 3; i++) {
		if ((unsigned int)i >= 3)
			start_kept();
		s += i;
	}
	if (i != 3)
		after_kept();
	return s;
}
EOF2
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void index_bad(void) { bads++; }
void down_bad(void) { bads++; }
void step_bad(void) { bads++; }
void reset_kept(void) { kepts++; }
void start_kept(void) { kepts++; }
void after_kept(void) { kepts++; }
unsigned long event(int id) { return 0x100UL + (unsigned long)id; }
int pick(int i) { return i + 5; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	static struct pmu p = { { { 0 } }, { 1, 2, 3, 4, 5, 6, 7, 8 } };
	int r;

	init(&p, &p);
	printf("%d %d %lx %lx %lx %d\n", p.fixed[2].type, p.fixed[1].idx, p.fixed[0].ev, p.fixed[1].ev,
	       p.fixed[2].ev, p.fixed[0].owner == &p);
	r = down(&p);
	printf("%d %d %d %d %d\n", r, step(&p), reset(10), start(-2), start(5));
	printf("bads %d kepts %d\n", bads, kepts);
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
	for o in "-O2" "-O2 -fwrapv"; do
		what="-m$m $o"
		gcc -m$m $o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
		want=$(undefined "$CC_TMP/g.o")
		gcc -m$m $o -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
		want_run=$("$CC_TMP/g")
		if ! env CC_MIR_STATS=2 "$CC_BIN" -m$m $o -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_range_induction: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_induction: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$got" != "$want" ]; then
			echo "run_range_induction: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! env CC_MIR_STATS=2 "$CC_BIN" -m$m $o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_range_induction: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_induction: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_range_induction: $what prints"
			echo "$got_run"
			echo "gcc's:"
			echo "$want_run"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_range_induction: ok"
