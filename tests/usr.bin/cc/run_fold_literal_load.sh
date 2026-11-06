#!/bin/sh
# A load from a string literal at a constant offset is the byte the
# literal has there: gcc folds `"..."[k]`.  Linux's iwlwifi wraps printk
# in macros that check BUILD_BUG_ON(fmt[sizeof(fmt) - 2] != '\n') of
# their literal format, and the drivers left 2600 calls of undefined
# __compiletime_assert_N while the byte stayed a load -- the iwlwifi,
# iwlmvm, iwldvm and iwlmld modules unloadable.  Escapes as the backend
# writes them (octal, hex, the letter ones), a wide literal's bytes, the
# null byte at the end, a wider load, through an inlined function's
# parameter; what gcc folds leaves no reference, and an index not known
# stays, as gcc keeps it.  -O2, -m64 and -m32, both code paths, every
# function through MIR; and the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_literal_load: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void newline_bad(void), escape_bad(void), wide_bad(void), nul_bad(void), word_bad(void);
extern void inlined_bad(void), index_kept(void);
extern int log_it(const char *fmt, ...);

#define LOG(fmt, ...)                                                    \
	do {                                                             \
		if (fmt[sizeof(fmt) - 2] != '\n')                        \
			newline_bad();                                   \
		log_it(fmt, ##__VA_ARGS__);                              \
	} while (0)

static inline int ends_with(const char *s, unsigned long n, char c)
{
	return s[n - 2] == c;
}

int report(int code, const char *what)
{
	LOG("starting\n");
	LOG("code %d (%s)\n", code, what);
	if ("\x41\102\t\033z"[0] != 'A' || "\x41\102\t\033z"[1] != 'B' || "\x41\102\t\033z"[2] != 9 ||
	    "\x41\102\t\033z"[3] != 27 || "\x41\102\t\033z"[4] != 'z')
		escape_bad();
	if (L"wide"[2] != L'd' || u"ab"[1] != u'b')
		wide_bad();
	if ("abc"[3] != 0)
		nul_bad();
	if (*(const unsigned int *)"\001\002\003\004" != 0x04030201u ||
	    *(const unsigned short *)("xy\377\377" + 2) != 0xffffu)
		word_bad();
	if (!ends_with("done\n", sizeof("done\n"), '\n'))
		inlined_bad();
	return code;
}

/* an index the code does not know: the load stays */
int pick(unsigned int i)
{
	if ("0123456789"[i % 10] == '7')
		index_kept();
	return "0123456789"[i % 10];
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
#include <stdarg.h>
static int bads, kepts;
void newline_bad(void) { bads++; }
void escape_bad(void) { bads++; }
void wide_bad(void) { bads++; }
void nul_bad(void) { bads++; }
void word_bad(void) { bads++; }
void inlined_bad(void) { bads++; }
void index_kept(void) { kepts++; }
int log_it(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vprintf(fmt, ap);
	va_end(ap);
	return n;
}
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	int a = report(7, "seven");
	int b = pick(17);
	int c = pick(3);

	printf("%d %d %d bads %d kepts %d\n", a, b, c, bads, kepts);
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
			echo "run_fold_literal_load: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_literal_load: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "index_kept" ]; then
			echo "run_fold_literal_load: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_fold_literal_load: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_literal_load: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_fold_literal_load: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_literal_load: ok"
