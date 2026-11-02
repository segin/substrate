#!/bin/sh
# An asm label, `extern int foo(void) __asm__("bar")`, is the name of
# the declaration's symbol: gcc calls bar, takes bar's address, reads
# and defines the labelled object or function under its label, static
# or not, and a label a later declaration gives counts too.  cc ignored
# the label and used the C name.  Linux's fortify-string.h depends on it:
# `__real_strscpy` is labelled `sized_strscpy` and called from an extern
# inline gnu_inline `sized_strscpy`, so a module (amd-uncore.ko) was
# left referencing a nonexistent `__real_strscpy` and modpost refused it.
# The inline body must still be inlined, not taken for the label's
# function.  glibc writes its labels with the empty
# __USER_LABEL_PREFIX__ gcc predefines, which cc did not.  The defined and undefined symbols are gcc's, and a program
# linked with the definitions prints what gcc's does, -O0 and -O2, -m64
# and -m32, both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_label_symbol: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/u.c" <<'EOF2'
typedef unsigned long size_t;
extern int foo(void) __asm__("bar");
int x(void) { return foo(); }
int (*y(void))(void) { return foo; }
extern int var __asm__("real_var");
int rd(void) { return var; }
int *addr(void) { return &var; }
int *pinit = &var;
int (*finit)(void) = foo;
int def(void) __asm__("def_label");
int def(void) { return 7; }
static int sfn(int) __asm__("sfn_label");
static int sfn(int v) { return v * 3; }
int (*sfn_addr(void))(int) { return sfn; }
static int svar __asm__("svar_label") = 5;
int *svar_addr(void) { return &svar; }
extern int later(void);
extern int later(void) __asm__("later_label");
int callm(void) { return later(); }
extern long __real_cpy(char *, const char *, size_t) __asm__("sized_cpy");
extern inline __attribute__((always_inline, gnu_inline)) long sized_cpy(char *p, const char *q, size_t n)
{
	if (n == 0)
		return -1;
	return __real_cpy(p, q, n) + 100;
}
long fort(char *d, const char *s) { return sized_cpy(d, s, 8); }
#define STR_(x) #x
#define ASMNAME2(prefix, name) STR_(prefix) name
#define ASMNAME(name) ASMNAME2(__USER_LABEL_PREFIX__, name)
extern int pfx(void) __asm__(ASMNAME("bar"));
int callp(void) { return pfx(); }
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
int bar(void) { return 42; }
int real_var = 9;
int later_label(void) { return 5; }
long sized_cpy(char *p, const char *q, size_t n) { strncpy(p, q, n); return (long)strlen(p) + 1000; }
int x(void);
int (*y(void))(void);
int rd(void);
int *addr(void);
extern int *pinit;
extern int (*finit)(void);
int def_label(void);
int (*sfn_addr(void))(int);
int *svar_addr(void);
int callm(void);
long fort(char *d, const char *s);
int callp(void);
int main(void)
{
	char buf[16] = { 0 };
	long f = fort(buf, "hello");

	printf("%d %d %d %d %d %d %d %d %d %d %ld %s %d\n", x(), y() == bar, rd(), addr() == &real_var,
	       pinit == &real_var, finit == bar, def_label(), sfn_addr()(2), *svar_addr(), callm(), f, buf,
	       callp());
	return 0;
}
EOF2
# (cc reaches an extern object through the GOT on x86-64, as gcc's
# default PIE code does; that is not what this checks)
syms() {
	nm "$1" | awk '$NF != "_GLOBAL_OFFSET_TABLE_" { if (NF == 2) print $1, $2; else print $2, $3 }' |
		sort | tr '\n' ' '
}
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/m.o" "$CC_TMP/m.c" 2>/dev/null || continue
	for o in -O0 -O2; do
		gcc -m$m $o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
		gcc -m$m -no-pie -o "$CC_TMP/g" "$CC_TMP/g.o" "$CC_TMP/m.o"
		want=$("$CC_TMP/g")
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			$run "$CC_BIN" -m$m $o -c -o "$CC_TMP/c.o" "$CC_TMP/u.c"
			if [ "$(syms "$CC_TMP/c.o")" != "$(syms "$CC_TMP/g.o")" ]; then
				echo "run_asm_label_symbol: -m$m $o $path: symbols [$(syms "$CC_TMP/c.o")], gcc's [$(syms "$CC_TMP/g.o")]"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o" "$CC_TMP/m.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_asm_label_symbol: -m$m $o $path prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_asm_label_symbol: ok"
