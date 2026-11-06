#!/bin/sh
# A declaration with the weak attribute -- not only a definition -- makes
# its symbol weak where the file refers to it, as gcc has it: `.weak` in
# the object, a weak undefined symbol (nm's `w`) that the link resolves
# to 0 when nothing defines it.  cc left it a plain undefined one, so a
# program whose weak reference had no definition did not link, and a
# Linux module's weak reference was a hard one.  So `__attribute__((weak))`
# (and a `__weak` macro of it), `#pragma weak NAME` before the
# declaration or after it, `#pragma weak NAME = TARGET` (a weak alias,
# of a name declared or not), and the attribute on a later
# redeclaration; a weak declaration nothing refers to leaves nothing in
# the object, and one the file goes on to define is a weak definition.
# The address of a weak symbol may be null, so `&obj != 0` and
# `if (fn)` are not folded, through an inlined helper either; and under
# -fPIC a weak undefined object is reached through the GOT, so a shared
# object built of it links without a definition.  `#pragma weak` of a
# static name is gcc's error.  nm's binding of each symbol is gcc's; the
# program, linked with no definition and with them, prints what gcc's
# prints, and so does one using a shared object.  -O0 and -O2, -m64 and
# -m32, both code paths, every function through MIR.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_weak_declarations: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/u.c" <<'EOF2'
#define __weak __attribute__((weak))
extern int wobj __attribute__((weak));
extern const unsigned char wtab[] __attribute__((weak));
extern void wfn(void) __attribute__((weak));
extern int wmac __weak;
extern int unref_obj __attribute__((weak));
extern void unref_fn(void) __attribute__((weak));
#pragma weak pw_before
extern int pw_before;
extern int pw_after;
int pw_fn(void);
#pragma weak pw_after
#pragma weak pw_fn
#pragma weak pw_never_declared
extern int later;
extern int later __attribute__((weak));
extern int wdef __attribute__((weak));
int wdef = 7;
extern int wdef_fn(void) __weak;
int wdef_fn(void) { return 8; }
int target_obj = 9;
extern int pa_obj;
#pragma weak pa_obj = target_obj
#pragma weak pa_undeclared = target_obj
extern int wobj2 __attribute__((weak));
int *wobj2_p = &wobj2;

static inline int nonnull(const void *p) { return p != 0; }

int get(void) { return &wobj ? wobj : -1; }
int tab(void) { return wtab ? wtab[0] : -2; }
int call(void) { if (wfn) { wfn(); return 1; } return 0; }
int mac(void) { return &wmac ? wmac : -3; }
int pb(void) { return &pw_before ? pw_before : -4; }
int pa(void) { return &pw_after ? pw_after : -5; }
int pf(void) { return pw_fn ? pw_fn() : -6; }
int lt(void) { return &later ? later : -7; }
int wd(void) { return wdef + wdef_fn(); }
int al(void) { return pa_obj; }
int cmp(void) { return (&wobj != 0) + 2 * (wfn != 0) + 4 * nonnull(&wobj) + 8 * nonnull(wtab); }
int ip(void) { return wobj2_p != 0; }
EOF2
cat >"$CC_TMP/defs.c" <<'EOF2'
int wobj = 11, wmac = 13, pw_before = 14, pw_after = 15, later = 17, wobj2 = 18;
const unsigned char wtab[] = { 12 };
void wfn(void) {}
int pw_fn(void) { return 16; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>
int get(void), tab(void), call(void), mac(void), pb(void), pa(void), pf(void), lt(void), wd(void), al(void),
    cmp(void), ip(void);
int main(void)
{
	printf("%d %d %d %d %d %d %d %d %d %d %d %d\n", get(), tab(), call(), mac(), pb(), pa(), pf(), lt(), wd(),
	       al(), cmp(), ip());
	return 0;
}
EOF2
# what of the unit's names the object has, and how bound: `w wobj`
bindings() {
	nm "$1" | awk '{ print $(NF-1), $NF }' |
		grep -E ' (wtab|wmac|unref_obj|unref_fn|pw_[a-z_]*|later|wdef|wdef_fn|pa_obj|pa_undeclared|wobj|wobj2|wfn|target_obj)$' |
		sort -k2 || true
}
fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g0" "$CC_TMP/main.c" "$CC_TMP/u.c" 2>/dev/null || continue
	want_none=$("$CC_TMP/g0")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g1" "$CC_TMP/main.c" "$CC_TMP/u.c" "$CC_TMP/defs.c"
	want_defs=$("$CC_TMP/g1")
	gcc -m$m -O2 -fPIC -shared -o "$CC_TMP/libg.so" "$CC_TMP/u.c"
	gcc -m$m -o "$CC_TMP/gs" "$CC_TMP/main.c" "$CC_TMP/libg.so"
	want_so=$(LD_LIBRARY_PATH="$CC_TMP" "$CC_TMP/gs")
	gcc -m$m -fPIC -shared -o "$CC_TMP/libgd.so" "$CC_TMP/defs.c"
	gcc -m$m -o "$CC_TMP/gsd" "$CC_TMP/main.c" "$CC_TMP/libg.so" "$CC_TMP/libgd.so"
	want_sod=$(LD_LIBRARY_PATH="$CC_TMP" "$CC_TMP/gsd")
	for o in O0 O2; do
		for pic in -fno-pie -fPIC; do
			gcc -m$m -$o $pic -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
			want=$(bindings "$CC_TMP/g.o")
			for path in mir legacy; do
				run="env CC_MIR_STATS=2"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				what="-m$m -$o $pic $path"
				rm -f "$CC_TMP/c0" "$CC_TMP/c1" "$CC_TMP/cs" "$CC_TMP/csd"
				if ! $run "$CC_BIN" -m$m -$o $pic -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
					echo "run_weak_declarations: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
					exit 1
				fi
				if grep 'not taken' "$CC_TMP/c.err"; then
					echo "run_weak_declarations: $what: a function fell back from MIR"
					fail=1
				fi
				got=$(bindings "$CC_TMP/c.o")
				if [ "$got" != "$want" ]; then
					echo "run_weak_declarations: $what binds [$(echo $got)], gcc [$(echo $want)]"
					fail=1
				fi
				if [ $pic = -fPIC ]; then
					if ! gcc -m$m -shared -o "$CC_TMP/libc.so" "$CC_TMP/c.o" 2>"$CC_TMP/ld.err"; then
						echo "run_weak_declarations: $what: the shared object does not link: $(head -1 "$CC_TMP/ld.err")"
						fail=1
						continue
					fi
					gcc -m$m -o "$CC_TMP/cs" "$CC_TMP/main.c" "$CC_TMP/libc.so" 2>/dev/null || true
					got_run=$(LD_LIBRARY_PATH="$CC_TMP" "$CC_TMP/cs" 2>&1 || true)
					if [ "$got_run" != "$want_so" ]; then
						echo "run_weak_declarations: $what shared, no definition, prints [$got_run], gcc's [$want_so]"
						fail=1
					fi
					gcc -m$m -o "$CC_TMP/csd" "$CC_TMP/main.c" "$CC_TMP/libc.so" "$CC_TMP/libgd.so" \
						2>/dev/null || true
					got_run=$(LD_LIBRARY_PATH="$CC_TMP" "$CC_TMP/csd" 2>&1 || true)
					if [ "$got_run" != "$want_sod" ]; then
						echo "run_weak_declarations: $what shared, defined, prints [$got_run], gcc's [$want_sod]"
						fail=1
					fi
					continue
				fi
				if ! gcc -m$m -no-pie -o "$CC_TMP/c0" "$CC_TMP/main.c" "$CC_TMP/c.o" 2>"$CC_TMP/ld.err"; then
					echo "run_weak_declarations: $what: no link without the definitions: $(head -1 "$CC_TMP/ld.err")"
					fail=1
				else
					got_run=$("$CC_TMP/c0" 2>&1 || true)
					if [ "$got_run" != "$want_none" ]; then
						echo "run_weak_declarations: $what, no definition, prints [$got_run], gcc's [$want_none]"
						fail=1
					fi
				fi
				if ! gcc -m$m -no-pie -o "$CC_TMP/c1" "$CC_TMP/main.c" "$CC_TMP/c.o" "$CC_TMP/defs.c" \
					2>"$CC_TMP/ld.err"; then
					echo "run_weak_declarations: $what: no link with the definitions: $(head -1 "$CC_TMP/ld.err")"
					fail=1
					continue
				fi
				got_run=$("$CC_TMP/c1" 2>&1 || true)
				if [ "$got_run" != "$want_defs" ]; then
					echo "run_weak_declarations: $what, defined, prints [$got_run], gcc's [$want_defs]"
					fail=1
				fi
			done
		done
	done
done

# #pragma weak of a static name: gcc's error, at the declaration
printf 'static int sw(void);\n#pragma weak sw\nint h(void) { return sw ? 1 : 0; }\nstatic int sw(void) { return 2; }\n' \
	>"$CC_TMP/s.c"
if "$CC_BIN" -c -o "$CC_TMP/s.o" "$CC_TMP/s.c" 2>"$CC_TMP/s.err"; then
	echo "run_weak_declarations: #pragma weak of a static function accepted"
	fail=1
elif ! grep -q "s.c:1:12: error: weak declaration of 'sw' must be public" "$CC_TMP/s.err"; then
	echo "run_weak_declarations: #pragma weak of a static function: $(grep error "$CC_TMP/s.err" | head -1)"
	fail=1
fi
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_weak_declarations: ok"
