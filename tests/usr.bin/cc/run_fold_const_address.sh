#!/bin/sh
# A load from a const object whose initializer gives an address there --
# a pointer, a member or an element of a const structure or array, the
# address of a function, of an object (with an offset) or of a string
# literal -- is that address, as gcc folds it from -O1, and the object,
# read nowhere else, is not emitted.  cc read zstd's
# `static const char *const notErrorCode = "..."` from .data.rel.ro.local,
# whose absolute address is a dynamic relocation in a PIE: Linux's
# arch/x86/boot/compressed/vmlinux, linked -pie, asserts it has none.
# The defined and undefined symbols are gcc's, x86-64 objects have no
# absolute address relocation, and the program prints what gcc's does;
# -m64 and -m32, -fno-pic and -fPIE, -O1 and -O2, every function through
# MIR.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1; then
	echo "run_fold_const_address: skip (no gcc or readelf)"
	exit 0
fi

cat >"$CC_TMP/f.c" <<'EOF2'
extern int foo(void);
extern int gx[4];
struct s { int a; const char *p; int (*f)(void); };

const char *get(int c)
{
	static const char *const notErrorCode = "Unspecified error code";

	switch (c) {
	case 0: return "No error";
	case 1: return "x";
	default: return notErrorCode;
	}
}
static const struct s S = { 7, "hi", foo };
static const struct s SA[2] = { { 1, "a", foo }, { 2, "b", 0 } };
static const char *const names[] = { "zero", "one", "two" };
static int *const gp = &gx[2];
static const int ki = 42;
static const int *const pp = &ki;

const char *member(void) { return S.p; }
int (*function(void))(void) { return S.f; }
const char *element_member(void) { return SA[1].p; }
int (*element_function(void))(void) { return SA[0].f; }
int (*element_null(void))(void) { return SA[1].f; }
const char *element(int i) { return i == 1 ? names[1] : names[2]; }
int *offset(void) { return gp; }
int through(void) { return *pp; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>
struct s;
int gx[4] = { 10, 11, 12, 13 };
int foo(void) { return 5; }
const char *get(int), *member(void), *element_member(void), *element(int);
int (*function(void))(void), (*element_function(void))(void), (*element_null(void))(void);
int *offset(void), through(void);
int main(void)
{
	printf("%s %s %s %s %d %s %d %d %s %s %d %d\n", get(0), get(1), get(2), member(), function()(),
	       element_member(), element_function()(), element_null() == 0, element(1), element(2), *offset(),
	       through());
	return 0;
}
EOF2

syms() {
	nm "$1" | awk '$NF !~ /^\./ && $NF != "_GLOBAL_OFFSET_TABLE_" && $NF !~ /get_pc_thunk/ {
		print ($(NF-1) == "U" ? "U" : "D"), $NF }' | sort -k2 -u
}
fail=0
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/probe.o" "$CC_TMP/f.c" 2>/dev/null || continue
	gcc -m$m -O2 -no-pie -o "$CC_TMP/g" "$CC_TMP/main.c" "$CC_TMP/f.c"
	want_out=$("$CC_TMP/g")
	for pic in -fno-pic -fPIE; do
		for o in O1 O2; do
			what="-m$m $pic -$o"
			gcc -m$m $pic -$o -c -o "$CC_TMP/g.o" "$CC_TMP/f.c"
			want=$(syms "$CC_TMP/g.o")
			if ! env CC_MIR_STATS=2 "$CC_BIN" -m$m $pic -$o -c -o "$CC_TMP/c.o" "$CC_TMP/f.c" 2>"$CC_TMP/c.err"; then
				echo "run_fold_const_address: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_fold_const_address: $what: a function fell back from MIR"
				fail=1
			fi
			got=$(syms "$CC_TMP/c.o")
			if [ "$got" != "$want" ]; then
				echo "run_fold_const_address: $what defines and refers to [$(echo $got)], gcc [$(echo $want)]"
				fail=1
			fi
			if [ $m = 64 ] && readelf -rW "$CC_TMP/c.o" | grep -q 'R_X86_64_64' &&
				! readelf -rW "$CC_TMP/g.o" | grep -q 'R_X86_64_64'; then
				echo "run_fold_const_address: $what: an absolute address relocation"
				readelf -rW "$CC_TMP/c.o" | grep 'R_X86_64_64' | head -3
				fail=1
			fi
			if [ $pic = -fPIE ]; then
				gcc -m$m -pie -o "$CC_TMP/c" "$CC_TMP/main.c" "$CC_TMP/c.o"
			else
				gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/main.c" "$CC_TMP/c.o"
			fi
			got_out=$("$CC_TMP/c" 2>&1 || true)
			if [ "$got_out" != "$want_out" ]; then
				echo "run_fold_const_address: $what prints [$got_out], gcc's [$want_out]"
				fail=1
			fi
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_const_address: ok"
