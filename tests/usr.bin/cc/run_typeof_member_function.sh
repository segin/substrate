#!/bin/sh
# A function whose return type is `typeof` of a member access is a
# function definition: the parser probes whether a file-scope
# declaration declares a function on a copy of itself without the
# structures, where the member's type could not be found, so the probe
# failed and the definition was taken for a declaration -- Linux's
# qla_tmpl.c `static inline typeof(qla27xx_fwdt_entry_call->call)
# (qla27xx_find_entry(uint type)) { ... }` was refused with "expected ';'
# after declaration".  The values are gcc's, -m64 and -m32, -O0 and -O2,
# through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_typeof_member_function: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

typedef int (*cb_t)(int);
struct e { int t; cb_t call; };

static int one(int x) { return x + 1; }
static struct e tab[] = { { 1, one }, { -1, 0 } };

static inline typeof(tab->call)(find(int type))
{
	return type == 1 ? tab[0].call : 0;
}

static typeof(tab->t) first(void) { return tab[1].t; }
static typeof(tab[0].call) direct(void) { return one; }

int main(void)
{
	printf("%d %d %d %zu\n", find(1)(4), first(), direct()(9), sizeof(first()));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			$run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_typeof_member_function: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_typeof_member_function: ok"
