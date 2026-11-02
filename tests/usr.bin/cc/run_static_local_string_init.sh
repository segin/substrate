#!/bin/sh
# A static local pointer initialized with a string's address -- a literal,
# or __func__, __FUNCTION__, __PRETTY_FUNCTION__ -- is laid out in the
# data, as gcc lays it out: the pointer is there before the function ever
# runs.  cc set it the first time through behind a guard, and took
# `__func__` there for a symbol of that name, which nothing defines:
# Linux's tracepoint_string(__func__), `static const char *___tp_str
# __used __section("__tracepoint_str") = __func__;`, left vmlinux.o with
# an undefined `__func__` (lib/maple_tree.o), and the kernel reads that
# section without calling the function.  The undefined symbols are gcc's
# (none of those names, no guard), a program reading a pointer through
# its section before the function runs prints what gcc's does, and so
# does one calling the functions; -O0 and -O2, -m64 and -m32, both code
# paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_static_local_string_init: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/s.c" <<'EOF2'
#define tp_str(str) ({ static const char *tp __attribute__((used, section("tps_str"))) = str; tp; })
const char *lit(void) { static const char *s = "literal"; return s; }
const char *fn(void) { static const char *s = __func__; return s; }
const char *fn2(void) { return ({ static const char *t = __FUNCTION__; t; }); }
const char *fn3(void) { static const char *const u = __PRETTY_FUNCTION__; return u; }
static inline const char *in(void) { return tp_str(__func__); }
const char *viain(void) { return in(); }
const char *traced(void) { return tp_str("traced string"); }
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>
const char *lit(void);
const char *fn(void);
const char *fn2(void);
const char *fn3(void);
const char *viain(void);
const char *traced(void);
extern const char *const __start_tps_str[], *const __stop_tps_str[];
int main(void)
{
	const char *const *p;

	/* the section's pointers, read before their functions run (in no order the language gives them) */
	for (p = __start_tps_str; p < __stop_tps_str; ++p)
		printf("section: %s\n", *p != 0 ? *p : "(null)");
	printf("%s %s %s %s %s %s\n", lit(), fn(), fn2(), fn3(), viain(), traced());
	return 0;
}
EOF2
names() {
	nm "$1" | awk '$1 == "U" || $2 == "U" { print $NF }' | grep -E '^(__func__|__FUNCTION__|__PRETTY_FUNCTION__)$|guard' |
		sort | tr '\n' ' '
}
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/m.o" "$CC_TMP/m.c" 2>/dev/null || continue
	for o in -O0 -O2; do
		gcc -m$m $o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/s.c"
		gcc -m$m -no-pie -o "$CC_TMP/g" "$CC_TMP/m.o" "$CC_TMP/g.o"
		want=$("$CC_TMP/g" | sort)
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			$run "$CC_BIN" -m$m $o -c -o "$CC_TMP/c.o" "$CC_TMP/s.c"
			if [ -n "$(names "$CC_TMP/c.o")" ]; then
				echo "run_static_local_string_init: -m$m $o $path: object names [$(names "$CC_TMP/c.o")]"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/m.o" "$CC_TMP/c.o"
			got=$("$CC_TMP/c" | sort)
			if [ "$got" != "$want" ]; then
				echo "run_static_local_string_init: -m$m $o $path prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_static_local_string_init: ok"
