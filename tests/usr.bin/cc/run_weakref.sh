#!/bin/sh
# `static T x __attribute__((weakref("y")))`, or weakref with alias("y"),
# is a name for a weak reference to y and defines nothing, as gcc has it:
# `.weakref x, y`, so y is weak undefined in the object (nm's `w`) when x
# is referred to, and nothing when it is not; y stays a strong reference
# where the file names it itself.  cc took the attribute and did nothing
# with it: x was a static object of its own in .bss (`b x`), its address
# never null, and a function's references stayed an undefined `x` that
# nothing defines -- libgcc's gthr-posix.h reaches the pthread functions
# so.  The address may be null, so `&x ? ...` and `if (f)` are left to
# the program; under -fPIC x is reached through the GOT and the PLT, so
# a shared object built of it links with nothing defining y.  A weakref
# not static is gcc's error.  nm's binding of each symbol is gcc's; the
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
	echo "run_weakref: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/u.c" <<'EOF2'
static int rx __attribute__((weakref("ry")));
static void rf(void) __attribute__((weakref("rg")));
static int rh(void) __attribute__((weakref, alias("rk")));
static int unref __attribute__((weakref("unref_target")));
extern int strong_y;
static int sy __attribute__((weakref("strong_y")));
int *rx_p = &rx;

int gr(void) { return &rx ? rx : -1; }
int cr(void) { int n = 0; if (rf) { rf(); n = 1; } return rh ? n + rh() : n - 2; }
int gs(void) { return sy + strong_y; }
int gp(void) { return rx_p != 0; }
EOF2
cat >"$CC_TMP/defs.c" <<'EOF2'
int ry = 21, strong_y = 3;
void rg(void) {}
int rk(void) { return 22; }
EOF2
cat >"$CC_TMP/strong.c" <<'EOF2'
int strong_y = 3;
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>
int gr(void), cr(void), gs(void), gp(void);
int main(void)
{
	printf("%d %d %d %d\n", gr(), cr(), gs(), gp());
	return 0;
}
EOF2
bindings() {
	nm "$1" | awk '{ print $(NF-1), $NF }' | grep -E ' (r[xfhgky]|unref|unref_target|strong_y|sy|rx_p)$' | sort -k2 || true
}
fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g0" "$CC_TMP/main.c" "$CC_TMP/u.c" "$CC_TMP/strong.c" 2>/dev/null ||
		continue
	want_none=$("$CC_TMP/g0")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g1" "$CC_TMP/main.c" "$CC_TMP/u.c" "$CC_TMP/defs.c"
	want_defs=$("$CC_TMP/g1")
	gcc -m$m -fPIC -shared -o "$CC_TMP/libstrong.so" "$CC_TMP/strong.c"
	gcc -m$m -fPIC -shared -o "$CC_TMP/libgd.so" "$CC_TMP/defs.c"
	gcc -m$m -O2 -fPIC -shared -o "$CC_TMP/libg.so" "$CC_TMP/u.c"
	gcc -m$m -o "$CC_TMP/gs" "$CC_TMP/main.c" "$CC_TMP/libg.so" "$CC_TMP/libstrong.so"
	want_so=$(LD_LIBRARY_PATH="$CC_TMP" "$CC_TMP/gs")
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
					echo "run_weakref: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
					exit 1
				fi
				if grep 'not taken' "$CC_TMP/c.err"; then
					echo "run_weakref: $what: a function fell back from MIR"
					fail=1
				fi
				got=$(bindings "$CC_TMP/c.o")
				if [ "$got" != "$want" ]; then
					echo "run_weakref: $what binds [$(echo $got)], gcc [$(echo $want)]"
					fail=1
				fi
				if [ $pic = -fPIC ]; then
					if ! gcc -m$m -shared -o "$CC_TMP/libc.so" "$CC_TMP/c.o" 2>"$CC_TMP/ld.err"; then
						echo "run_weakref: $what: the shared object does not link: $(head -1 "$CC_TMP/ld.err")"
						fail=1
						continue
					fi
					gcc -m$m -o "$CC_TMP/cs" "$CC_TMP/main.c" "$CC_TMP/libc.so" "$CC_TMP/libstrong.so" \
						2>/dev/null || true
					got_run=$(LD_LIBRARY_PATH="$CC_TMP" "$CC_TMP/cs" 2>&1 || true)
					if [ "$got_run" != "$want_so" ]; then
						echo "run_weakref: $what shared, no definition, prints [$got_run], gcc's [$want_so]"
						fail=1
					fi
					gcc -m$m -o "$CC_TMP/csd" "$CC_TMP/main.c" "$CC_TMP/libc.so" "$CC_TMP/libgd.so" \
						2>/dev/null || true
					got_run=$(LD_LIBRARY_PATH="$CC_TMP" "$CC_TMP/csd" 2>&1 || true)
					if [ "$got_run" != "$want_sod" ]; then
						echo "run_weakref: $what shared, defined, prints [$got_run], gcc's [$want_sod]"
						fail=1
					fi
					continue
				fi
				if ! gcc -m$m -no-pie -o "$CC_TMP/c0" "$CC_TMP/main.c" "$CC_TMP/c.o" "$CC_TMP/strong.c" \
					2>"$CC_TMP/ld.err"; then
					echo "run_weakref: $what: no link without the definitions: $(head -1 "$CC_TMP/ld.err")"
					fail=1
				else
					got_run=$("$CC_TMP/c0" 2>&1 || true)
					if [ "$got_run" != "$want_none" ]; then
						echo "run_weakref: $what, no definition, prints [$got_run], gcc's [$want_none]"
						fail=1
					fi
				fi
				if ! gcc -m$m -no-pie -o "$CC_TMP/c1" "$CC_TMP/main.c" "$CC_TMP/c.o" "$CC_TMP/defs.c" \
					2>"$CC_TMP/ld.err"; then
					echo "run_weakref: $what: no link with the definitions: $(head -1 "$CC_TMP/ld.err")"
					fail=1
					continue
				fi
				got_run=$("$CC_TMP/c1" 2>&1 || true)
				if [ "$got_run" != "$want_defs" ]; then
					echo "run_weakref: $what, defined, prints [$got_run], gcc's [$want_defs]"
					fail=1
				fi
			done
		done
	done
done

# a weakref with external linkage: gcc's error, at the declaration
printf 'extern int nx __attribute__((weakref("ny")));\nint g(void) { return nx; }\n' >"$CC_TMP/e.c"
if "$CC_BIN" -c -o "$CC_TMP/e.o" "$CC_TMP/e.c" 2>"$CC_TMP/e.err"; then
	echo "run_weakref: an external weakref accepted"
	fail=1
elif ! grep -q "e.c:1:12: error: 'weakref' symbol 'nx' must have static linkage" "$CC_TMP/e.err"; then
	echo "run_weakref: an external weakref: $(grep error "$CC_TMP/e.err" | head -1)"
	fail=1
fi
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_weakref: ok"
