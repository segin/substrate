#!/bin/sh
# A pointer a static const table holds, at a constant index, to a string
# literal or to a function or object the file defines, is not null: gcc
# folds the load to that address and the compare with null to its
# answer.  Linux's rtw89 checks its table of SAR handlers so, with
# BUILD_BUG_ON(!rtw89_sar_handlers[src].descr_sar_source) and the same of
# .query_sar_config, and rtw89_core.o kept four calls of undefined
# __compiletime_assert_N while the loads stayed.  A null entry, a weak
# function's address and one only declared stay what gcc makes of them.
# Under -fno-delete-null-pointer-checks, as Linux builds, which keeps
# gcc from taking a weak or a declared function for non-null.
# -O2, -m64 and -m32, both code paths, every function through MIR; and
# the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_table_pointer: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void descr_bad(void), query_bad(void), object_bad(void);
extern void null_kept(void), weak_kept(void), extern_kept(void);
extern int declared(int x);
int weak_query(int x) __attribute__((weak));
int weak_query(int x) { return x + 9; }
struct handler {
	const char *descr;
	unsigned char factor;
	int (*query)(int x);
	const int *limit;
};
static int q_common(int x) { return x + 1; }
static int q_acpi(int x) { return x + 2; }
static const int limits[2] = { 10, 20 };
enum { SRC_COMMON, SRC_ACPI, SRC_NONE, SRC_WEAK, SRC_EXTERN, SRC_NR };
static const struct handler handlers[SRC_NR] = {
	[SRC_COMMON] = { .descr = "COMMON", .factor = 2, .query = q_common, .limit = &limits[0] },
	[SRC_ACPI] = { .descr = "ACPI", .factor = 3, .query = q_acpi, .limit = &limits[1] },
	[SRC_NONE] = { .descr = "NONE" },
	[SRC_WEAK] = { .descr = "WEAK", .query = weak_query },
	[SRC_EXTERN] = { .descr = "EXTERN", .query = declared },
};
/* and a plain array of them */
static int (*const queries[])(int x) = { q_common, q_acpi };

#define CHECK(src)                                       \
	do {                                             \
		typeof(src) _s = (src);                  \
		if (!handlers[_s].descr)                 \
			descr_bad();                     \
		if (!handlers[_s].query)                 \
			query_bad();                     \
	} while (0)

int apply(int k)
{
	CHECK(SRC_COMMON);
	CHECK(SRC_ACPI);
	if (!handlers[SRC_ACPI].limit || queries[1] == 0)
		object_bad();
	if (!handlers[SRC_NONE].query)
		null_kept();
	if (!handlers[SRC_WEAK].query)
		weak_kept();
	if (handlers[SRC_EXTERN].query == 0)
		extern_kept();
	return handlers[k].query(k) + *handlers[k].limit + queries[k](k);
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void descr_bad(void) { bads++; }
void query_bad(void) { bads++; }
void object_bad(void) { bads++; }
void null_kept(void) { kepts++; }
void weak_kept(void) { kepts++; }
void extern_kept(void) { kepts++; }
int declared(int x) { return x; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	int a = apply(0);
	int b = apply(1);

	printf("%d %d bads %d kepts %d\n", a, b, bads, kepts);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

NDP=-fno-delete-null-pointer-checks
fail=0
for m in 64 32; do
	gcc -m$m -O2 $NDP -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 $NDP -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=1"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 $NDP -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_fold_table_pointer: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_table_pointer: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "extern_kept null_kept weak_kept" ]; then
			echo "run_fold_table_pointer: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 $NDP -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_fold_table_pointer: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_table_pointer: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_fold_table_pointer: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_table_pointer: ok"
