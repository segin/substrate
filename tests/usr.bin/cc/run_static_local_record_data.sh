#!/bin/sh
# A static local structure or union is initialized as data, as a
# file-scope one is: cc stored its initializer on the function's first
# call, behind a guard, and so put it in .bss whatever its section.
# Linux's dynamic debug finds its descriptors -- static locals in
# __dyndbg, initialized with __func__, bitfields and a compound literal
# -- by that section, and they were not in it.  (Each is aligned(8), as
# Linux's are, or gcc pads a large object to 32 and the section is no
# array.)  Names of the enclosing
# blocks' statics are their symbols (`{ &l, &l }` for a list head), and
# a static key's address as an asm's "i" operand is that symbol.  The
# section is read before any of the functions runs.  Results are gcc's,
# -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_static_local_record_data: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

#define NI __attribute__((noinline))
#ifdef __x86_64__
#define ADDR ".quad"
#else
#define ADDR ".long"
#endif

struct key {
	int enabled;
	union {
		unsigned long type;
		void *entries;
	};
};
struct dd {
	const char *mod;
	const char *fn;
	unsigned int lineno:18;
	unsigned int cls:6;
	unsigned int flags:8;
	union {
		struct key t;
		struct key f;
	} key;
} __attribute__((aligned(8)));
struct list {
	struct list *next, *prev;
};

extern struct dd __start_ccdd[], __stop_ccdd[];
extern void *__start_ccjt[], *__stop_ccjt[];

NI static int use(int n)
{
	static struct dd d __attribute__((aligned(8), section("ccdd"), used)) = {
		.mod = "mod",
		.fn = __func__,
		.lineno = __LINE__,
		.cls = 63,
		.flags = 1,
		.key.t = ((struct key){ .enabled = 1, .type = 2UL }),
	};

	__asm__(".pushsection ccjt, \"aw\"\n\t" ADDR " %c0\n\t.popsection" : : "i"(&d.key.t));
	return n + d.flags;
}

NI static int other(int n)
{
	static struct dd d __attribute__((aligned(8), section("ccdd"), used)) = { "other", __func__, 7, 1, 2, { { 0, { 0 } } } };

	return n * d.flags;
}

NI static int self(void)
{
	static struct list l = { &l, &l };

	return l.next == &l && l.prev == &l;
}

NI static int pointed(void)
{
	static int x = 5;
	static struct {
		int *p;
		int v;
	} s = { &x, 3 };

	return *s.p + s.v;
}

NI static unsigned onion(void)
{
	static union {
		unsigned i;
		unsigned char c[4];
	} u = { .c = { 1, 2, 3, 4 } };

	return u.i;
}

int main(void)
{
	struct dd *p;

	for (p = __start_ccdd; p < __stop_ccdd; p++)
		printf("%s %s %u %u %u %d %lu\n", p->mod, p->fn, p->lineno > 0, p->cls, p->flags, p->key.t.enabled,
		       p->key.t.type);
	printf("%d %d\n", (int)(__stop_ccjt - __start_ccjt), __start_ccjt[0] == (void *)&__start_ccdd[0].key.t ||
	       __start_ccjt[0] == (void *)&__start_ccdd[1].key.t);
	printf("%d %d %d %d %u\n", use(4), other(3), self(), pointed(), onion());
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/s.c" 2>/dev/null || continue
	want=$("$CC_TMP/g" | sort)
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/s.o" "$CC_TMP/s.c" 2>"$CC_TMP/c.err"; then
			echo "run_static_local_record_data: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/s.o"
		got=$("$CC_TMP/c" | sort)
		if [ "$got" != "$want" ]; then
			echo "run_static_local_record_data: -m$m -$o prints"
			printf '%s\n' "$got"
			echo "gcc's"
			printf '%s\n' "$want"
			exit 1
		fi
	done
done
echo "run_static_local_record_data: ok"
