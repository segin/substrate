#!/bin/sh
# The bits a value is known to have, wherever it is read: a constant's,
# what a mask, an or, an xor or a shift by a constant makes of them, a
# zero-extending load's zeros, a variable's that all its definitions
# agree on -- and an equality compare that a bit known 1 on one side and
# 0 on the other decides, as gcc's bit tracking decides it.  Linux's
# vmx_recalc_pmu_msr_intercepts() clears a bit of a local that starts as
# two VM_EXIT_* bits, then vm_exit_controls_changebit() asks
# BUILD_BUG_ON(!(val & VM_EXIT_CONTROLS_SHADOW_BITS)); i915's
# gen11_gt_irq_postinstall() ors low bits into irqs and asks
# BUILD_BUG_ON(irqs & 0xffff0000): kvm-intel.o and i915.o kept calls of
# undefined __compiletime_assert_N.  A value whose bits are not known
# stays tested, as gcc keeps it.  -O2, -m64 and -m32 (a 64-bit value as
# two words there), both code paths, every function through MIR; and
# the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_range_known_bits: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void mask_bad(void), high_bad(void), shift_bad(void), byte_bad(void);
extern void param_kept(void);
extern int has_save(void), wants(void);
extern void set_ctl(unsigned long long v);

#define SHADOW_BITS (0x4 | 0x8000 | 0x200 | 0x1000 | 0x40000 | 0x40000000)

static inline __attribute__((always_inline)) void setbit(unsigned long long val)
{
	if (!(val & SHADOW_BITS))
		mask_bad();
	set_ctl(val);
}

void recalc(int on)
{
	unsigned long long bits = 0x00001000 | 0x40000000;

	if (!has_save())
		bits &= ~0x40000000ULL;
	if (on)
		setbit(bits);
	else
		setbit(bits << 0);
}

void post(const unsigned char *p)
{
	unsigned int irqs = 1 << 0;
	unsigned int b = p[1];

	if (!wants())
		irqs |= (1u << 3) | (1 << 8) | (1u << 11);
	if (irqs & 0xffff0000)
		high_bad();
	if (((irqs << 16) | 0x5) == 0 || ((irqs ^ 0x80000000u) >> 31) != 1)
		shift_bad();
	if ((b & 0xff00) != 0)
		byte_bad();
	set_ctl(irqs << 16 | irqs | b);
}

/* bits not known: tested */
void param(unsigned int v)
{
	if (v & 0x10)
		param_kept();
	set_ctl(v);
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts, saves;
static unsigned long long ctl;
void mask_bad(void) { bads++; }
void high_bad(void) { bads++; }
void shift_bad(void) { bads++; }
void byte_bad(void) { bads++; }
void param_kept(void) { kepts++; }
int has_save(void) { return saves++ & 1; }
int wants(void) { return saves & 1; }
void set_ctl(unsigned long long v) { ctl += v; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	static const unsigned char bytes[2] = { 3, 0xf7 };

	recalc(1);
	recalc(0);
	post(bytes);
	param(0x13);
	param(3);
	printf("%llx bads %d kepts %d\n", ctl, bads, kepts);
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
			echo "run_range_known_bits: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_known_bits: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "param_kept" ]; then
			echo "run_range_known_bits: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_range_known_bits: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_known_bits: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_range_known_bits: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_range_known_bits: ok"
