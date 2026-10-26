#!/bin/sh
# gcc's x86 named address spaces, __seg_fs and __seg_gs: qualifiers of a
# type, and every load and store of an object in one -- through a pointer,
# a member of one, a global declared in one, ++, +=, and an asm memory
# operand ("m", "+m") -- reaches it through the segment, %fs: or %gs:
# before the address.  Linux's per-CPU data, with USE_X86_SEG_SUPPORT,
# is all such accesses, its asm leaving the segment to the compiler.
#
# A Linux process's %gs base is 0, so on x86-64 the program reads and
# writes what it would without the segment, and runs against gcc's build;
# each function's accesses are checked to go through %gs.  On i386 %fs is
# no data segment there, so the code is only checked to use %fs.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_named_address_space: skip (no gcc)"
	exit 0
fi

# Linux's Kconfig: CC_HAS_NAMED_AS
printf 'int __seg_fs fs; int __seg_gs gs;\n' >"$CC_TMP/probe.c"
"$CC_BIN" -x c -S -o "$CC_TMP/probe.s" "$CC_TMP/probe.c"

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

typedef unsigned long ul;

#define SEG(v) (*(__typeof__(v) SEG_AS *)(ul)&(v))

struct pc { int a; long b; unsigned char c[4]; };

ul counter = 40;
struct pc obj = { 1, 2, { 3, 4, 5, 6 } };
extern int SEG_AS gvar;
int SEG_AS gvar = 7;

ul __attribute__((noinline)) rd(void) { return SEG(counter); }
void __attribute__((noinline)) wr(ul v) { SEG(counter) = v; }
void __attribute__((noinline)) inc(void) { SEG(counter)++; }
void __attribute__((noinline)) add(ul v) { SEG(counter) += v; }
int __attribute__((noinline)) rdg(void) { return gvar; }
void __attribute__((noinline)) wrg(int x) { gvar = x * 3; }
long __attribute__((noinline)) mem(struct pc SEG_AS *p) { return p->b * 10 + p->a + p->c[2]; }
void __attribute__((noinline)) memw(struct pc SEG_AS *p, int k) { p->c[1] = (unsigned char)k; p->b -= k; }
void __attribute__((noinline)) addm(ul v) { asm("add" SFX " %1, %0" : "+m"(SEG(counter)) : "r"(v)); }
ul __attribute__((noinline)) rdm(void) { ul r; asm("mov" SFX " %1, %0" : "=r"(r) : "m"(SEG(counter))); return r; }
void __attribute__((noinline)) incg(void) { asm("incl %0" : "+m"(gvar)); }
/* an object declared in the address space: its members through `.`, and Linux's this_cpu_read() shape */
struct pc SEG_AS gobj = { 5, 6, { 7, 8, 9, 10 } };
long __attribute__((noinline)) rdo(void) { return gobj.b * 100 + gobj.c[3]; }
void __attribute__((noinline)) wro(int k) { gobj.a += k; gobj.c[0] = (unsigned char)k; }
long __attribute__((noinline)) rdt(void) { return *(volatile __typeof__(gobj.b) *)&gobj.b; }

int main(void)
{
	struct pc SEG_AS *p = (struct pc SEG_AS *)(ul)&obj;
	ul a = rd();

	wr(a + 2);
	inc();
	add(5);
	addm(100);
	wrg(rdg() + 1);
	incg();
	memw(p, 9);
	wro(3);
	printf("%lu %lu %d %ld %d %d %ld %ld\n", a, rdm(), rdg(), mem(p), obj.c[1], gvar, rdo(), rdt());
	return 0;
}
EOF2

# check FILE FUNC SEG: every memory access of FUNC's body that is not on the stack goes through SEG
check() {
	awk -v f="$2" '$0 == f ":" {x=1; next} $0 ~ "^\\.size " f "," || /^\t\.size/ {x=0} x' "$1" |
		grep -E '\(%[re](ax|bx|cx|dx|si|di)|\(%r(8|9|1[0-5])|[a-z_]+(\(%rip\))?, |, [a-z_]+(\(%rip\))?$|%[fg]s:' |
		grep -vE '%[re]sp|%[re]bp|^\s+lea|^\s+mov[lq]? \$' >"$CC_TMP/acc.txt" || true
	if [ ! -s "$CC_TMP/acc.txt" ]; then
		echo "run_named_address_space: $2() makes no access to check"
		exit 1
	fi
	if grep -v "%$3:" "$CC_TMP/acc.txt" | grep -qE '\(%|counter|gvar|obj'; then
		echo "run_named_address_space: $2() reaches memory without %$3:"
		grep -v "%$3:" "$CC_TMP/acc.txt"
		exit 1
	fi
}

gcc -O1 -DSEG_AS=__seg_gs -DSFX='"q"' -o "$CC_TMP/g" "$CC_TMP/s.c"
want=$("$CC_TMP/g")
for o in O0 O1 O2 Os; do
	"$CC_BIN" -$o -DSEG_AS=__seg_gs -DSFX='"q"' -o "$CC_TMP/c" "$CC_TMP/s.c"
	got=$("$CC_TMP/c") || { echo "run_named_address_space: -$o: the program fails"; exit 1; }
	if [ "$got" != "$want" ]; then
		echo "run_named_address_space: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
	"$CC_BIN" -$o -fno-pic -DSEG_AS=__seg_gs -DSFX='"q"' -S -o "$CC_TMP/c.s" "$CC_TMP/s.c"
	for f in rd wr inc add rdg wrg mem memw addm rdm incg rdo wro rdt; do
		check "$CC_TMP/c.s" $f gs
	done
done
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	for o in O0 O2; do
		"$CC_BIN" -m32 -$o -fno-pic -DSEG_AS=__seg_fs -DSFX='"l"' -S -o "$CC_TMP/c32.s" "$CC_TMP/s.c"
		for f in rd wr inc add rdg wrg mem memw addm rdm incg rdo wro rdt; do
			check "$CC_TMP/c32.s" $f fs
		done
	done
fi

# a structure copied into a named address space is refused, not copied without the segment
printf 'struct s { long a[4]; };\nvoid f(struct s __seg_gs *p, struct s v) { *p = v; }\n' >"$CC_TMP/bad.c"
if "$CC_BIN" -O2 -c -o "$CC_TMP/bad.o" "$CC_TMP/bad.c" 2>"$CC_TMP/bad.err"; then
	echo "run_named_address_space: a structure assigned through a __seg_gs pointer compiles"
	exit 1
fi
if ! grep -q "named address space" "$CC_TMP/bad.err"; then
	echo "run_named_address_space: the structure copy is refused for another reason: $(cat "$CC_TMP/bad.err")"
	exit 1
fi
echo "run_named_address_space: ok"
