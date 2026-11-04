#!/bin/sh
# A const object -- an array of const elements too -- is read-only, as
# gcc places it: in .rodata, or, in position-independent code that has
# the loader relocate an address in it, .data.rel.ro (.data.rel.ro.local
# when each address is the module's own), which the loader makes
# read-only after.  cc put them all in .data: Linux's const tables of
# function pointers (file_operations and the like) were writable,
# outside what STRICT_KERNEL_RWX protects.  Each object is in gcc's
# section, with and without -fPIC and -fdata-sections; .rodata is not
# writable; and the program prints what gcc's does.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1; then
	echo "run_const_rodata: skip (no gcc or readelf)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

struct ops { int (*f)(int); const char *name; };
static int twice(int x) { return 2 * x; }
int ext(int x) { return x + 100; }

const int table[4] = { 1, 2, 3, 4 };
const int grid[2][2] = { { 5, 6 }, { 7, 8 } };
const int zero = 0;
const int nothing;
const volatile int cv = 9;
const char msg[] = "const";
const char *const name = "pointer";
const struct ops local_ops = { twice, "local" };
const struct ops extern_ops = { ext, "extern" };
int (*const call)(int) = ext;
static const long hidden = 11;
int counter = 12;
const char *names[2] = { "w", "x" };

int main(void)
{
	printf("%d %d %d %d %d %s %s %d %s %d %s %d %ld %d %s\n", table[3], grid[1][0], zero, nothing, cv, msg,
	       name, local_ops.f(3), local_ops.name, extern_ops.f(1), extern_ops.name, call(2), hidden, counter++,
	       names[1]);
	return 0;
}
EOF2
# symbol -> its section's name (gcc's .data.rel and .data.rel.local, writable data it has relocated, are
# .data's; cc does not name them so)
placement() {
	readelf -SW "$1" | sed -n 's/^ *\[ *\([0-9]*\)\] \([^ ]*\).*/\1 \2/p' >"$CC_TMP/secs"
	readelf -sW "$1" | awk -v want=" table grid zero nothing cv msg name local_ops extern_ops call hidden counter names " \
		'NR == FNR { name[$1] = $2; next }
		index(want, " " $8 " ") {
			s = name[$7]
			if (s !~ /^\.data\.rel\.ro/) sub(/^\.data\.rel(\.local)?/, ".data", s)
			print $8, s
		}' "$CC_TMP/secs" - | sort
}
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/probe.o" "$CC_TMP/b.c" 2>/dev/null || continue
	gcc -m$m -O1 -no-pie -o "$CC_TMP/g" "$CC_TMP/b.c"
	want_out=$("$CC_TMP/g")
	for pic in -fno-pic -fPIC; do
		for ds in "" -fdata-sections; do
			for o in O0 O2; do
				# (at the same level: from -O1 the static one read only as its value is not emitted)
				gcc -m$m -std=gnu17 -$o $pic $ds -c -o "$CC_TMP/g.o" "$CC_TMP/b.c"
				want=$(placement "$CC_TMP/g.o")
				for path in mir legacy; do
					run="env"
					if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
					what="-m$m $pic $ds -$o $path"
					if ! $run "$CC_BIN" -m$m -$o $pic $ds -c -o "$CC_TMP/c.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
						echo "run_const_rodata: $what: $(head -1 "$CC_TMP/c.err")"
						exit 1
					fi
					got=$(placement "$CC_TMP/c.o")
					if [ "$got" != "$want" ]; then
						echo "run_const_rodata: $what places [$(echo "$got" | tr '\n' ';')], gcc [$(echo "$want" | tr '\n' ';')]"
						exit 1
					fi
					if readelf -SW "$CC_TMP/c.o" | grep '\.rodata' | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^[WAXMSIL]+$/) print $i }' |
						grep -q W; then
						echo "run_const_rodata: $what: a .rodata section is writable"
						exit 1
					fi
					if [ $pic = -fPIC ]; then
						gcc -m$m -pie -o "$CC_TMP/c" "$CC_TMP/c.o"
					else
						gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o"
					fi
					if [ "$("$CC_TMP/c")" != "$want_out" ]; then
						echo "run_const_rodata: $what prints [$("$CC_TMP/c")], gcc's [$want_out]"
						exit 1
					fi
				done
			done
		done
	done
done
echo "run_const_rodata: ok"
