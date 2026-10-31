#!/bin/sh
# An asm's "i" operand that is an address -- a symbol and an offset -- is
# written whatever the symbol's length.  It was put together in 64
# bytes, and a longer one was cut short and then, its offset not
# fitting, written as 0: a static key in a static local named for its
# function, `__cc_static_initcall_blacklisted___UNIQUE_ID_ddebug_739_..`,
# gave Linux's jump table `.quad 0 + 1 + 2 - .`, which the assembler
# refuses.  The table's entries are read back and compared with the
# addresses.  Results are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_imm_long_symbol: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/l.c" <<'EOF2'
#include <stdio.h>

#ifdef __x86_64__
#define ADDR ".quad"
#else
#define ADDR ".long"
#endif

struct key {
	int enabled;
	long type;
};
struct pair {
	long a;
	struct key k;
};

extern void *__start_cclj[], *__stop_cclj[];

struct pair an_object_whose_name_runs_on_well_past_sixty_four_characters_of_text = { 1, { 2, 3 } };

static inline __attribute__((always_inline)) void note(struct key *const key)
{
	__asm__(".pushsection cclj, \"aw\"\n\t" ADDR " %c0\n\t.popsection" : : "i"(key));
}

static void *in_a_function_whose_name_is_long_enough_to_matter(int n)
{
	static struct pair a_static_local_with_a_long_name_of_its_own = { 4, { 5, 6 } };

	note(&a_static_local_with_a_long_name_of_its_own.k);
	return n ? &a_static_local_with_a_long_name_of_its_own.k : 0;
}

int main(void)
{
	void *local = in_a_function_whose_name_is_long_enough_to_matter(1);
	void *global = &an_object_whose_name_runs_on_well_past_sixty_four_characters_of_text.k;
	int found = 0;
	void **p;

	note(&an_object_whose_name_runs_on_well_past_sixty_four_characters_of_text.k);
	for (p = __start_cclj; p < __stop_cclj; p++)
		found += (*p == local) * 1 + (*p == global) * 2 + (*p == 0) * 4;
	printf("%d %d\n", (int)(__stop_cclj - __start_cclj), found);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/l.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O1 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/l.o" "$CC_TMP/l.c" 2>"$CC_TMP/c.err"; then
			echo "run_asm_imm_long_symbol: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/l.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_asm_imm_long_symbol: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_asm_imm_long_symbol: ok"
